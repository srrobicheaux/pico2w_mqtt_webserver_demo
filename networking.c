#include "pico/stdio.h"
#include "pico/cyw43_arch.h"
#include "lwip/prot/dns.h"
#include "networking.h"
#include "hardware/watchdog.h"

#define DNS_PORT 53
#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68

#include "lwip/ip_addr.h"

typedef struct dhcp_entry_t
{
    uint8_t mac[6];
    ip4_addr_t addr;
    uint32_t leased_until;
} dhcp_entry_t;

typedef struct
{
    ip4_addr_t start_ip;
    ip4_addr_t end_ip;
    dhcp_entry_t entries[8]; // Max 8 clients
} dhcp_server_t;

// confusing global parameter needs to be eliminatged
static dhcp_server_t dhcp_srv;
// A very simple dhcpserver to serve a  single IP address in order to provision chip.
static struct udp_pcb *dhcp_pcb;

static void send_dhcp_reply(const u8_t *client_mac, u32_t xid, u8_t message_type, ip4_addr_t *offered_ip)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 300, PBUF_RAM);
    if (!p)
        return;
    memset(p->payload, 0, p->tot_len);

    u8_t *msg = (u8_t *)p->payload;
    msg[0] = 2;               // Boot Reply
    msg[1] = 1;               // Hardware type: Ethernet
    msg[2] = 6;               // Hardware addr len
    msg[3] = 0;               // Hops
    memcpy(&msg[4], &xid, 4); // MUST match client XID

    // yiaddr: Your (Client) IP address
    memcpy(&msg[16], &offered_ip->addr, 4);

    // chaddr: Client Hardware Address
    memcpy(&msg[28], client_mac, 6);

    // Magic Cookie: 0x63 0x82 0x53 0x63
    u8_t *opt = &msg[236];
    *opt++ = 0x63;
    *opt++ = 0x82;
    *opt++ = 0x53;
    *opt++ = 0x63;

    // Option 53: Message Type (2 = Offer, 5 = Ack)
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = message_type;

    // Option 54: Server Identifier (The Pico's IP: 192.168.4.1)
    *opt++ = 54;
    *opt++ = 4;
    *opt++ = 192;
    *opt++ = 168;
    *opt++ = 4;
    *opt++ = 1;

    // Option 51: Lease Time (e.g., 1 hour = 3600s)
    *opt++ = 51;
    *opt++ = 4;
    *opt++ = 0;
    *opt++ = 0;
    *opt++ = 0x0E;
    *opt++ = 0x10;

    // Option 1: Subnet Mask
    *opt++ = 1;
    *opt++ = 4;
    *opt++ = 255;
    *opt++ = 255;
    *opt++ = 255;
    *opt++ = 0;

    // Option 3: Router (The Pico itself)
    *opt++ = 3;
    *opt++ = 4;
    *opt++ = 192;
    *opt++ = 168;
    *opt++ = 4;
    *opt++ = 1;

    // Option 6: Domain Name Server (Point to the Pico 192.168.4.1)
    *opt++ = 6;
    *opt++ = 4;
    *opt++ = 192;
    *opt++ = 168;
    *opt++ = 4;
    *opt++ = 1;

    *opt++ = 255; // End Option

    // Always broadcast DHCP replies when the client has no IP yet
    udp_sendto(dhcp_pcb, p, IP_ADDR_BROADCAST, DHCP_PORT_CLIENT);
    pbuf_free(p);
}

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (p->len < 240)
    {
        pbuf_free(p);
        return;
    }

    u8_t *msg = (u8_t *)p->payload;
    u32_t xid;
    memcpy(&xid, &msg[4], 4);
    u8_t *client_mac = &msg[28];

    // Find Option 53 (Message Type)
    u8_t msg_type = 0;
    u8_t *ptr = &msg[240];
    while (ptr < (u8_t *)p->payload + p->len)
    {
        if (*ptr == 53)
        {
            msg_type = *(ptr + 2);
            break;
        }
        if (*ptr == 255)
            break;
        ptr += *(ptr + 1) + 2;
    }

    ip4_addr_t offered_ip;
    IP4_ADDR(&offered_ip, 192, 168, 4, 2);

    if (msg_type == 1)
    { // DHCP Discover
        printf("DHCP Discover -> Sending Offer\n");
        send_dhcp_reply(client_mac, xid, 2, &offered_ip); // 2 = Offer
    }
    else if (msg_type == 3)
    { // DHCP Request
        printf("DHCP Request -> Sending ACK\n");
        send_dhcp_reply(client_mac, xid, 5, &offered_ip); // 5 = ACK
    }

    pbuf_free(p);
}

void dhcp_server_init(dhcp_server_t *srv, ip4_addr_t *base_addr, ip4_addr_t *nm)
{
    if (dhcp_pcb)
        udp_remove(dhcp_pcb);
    dhcp_pcb = udp_new();
    udp_bind(dhcp_pcb, IP_ADDR_ANY, DHCP_PORT_SERVER);
    udp_recv(dhcp_pcb, dhcp_recv, srv);
    printf("DHCP Server initialized on port %d\n", DHCP_PORT_SERVER);
}

void dhcp_server_deinit(dhcp_server_t *dhcp)
{
    if (dhcp == NULL)
    {
        return;
    }

    // Stop UDP listening and free the PCB
    if (dhcp_pcb != NULL)
    {
        udp_remove(dhcp_pcb);
        dhcp_pcb = NULL;
    }

    // Clear any other dynamic state (timers, etc. are handled by udp_remove)
    memset(dhcp, 0, sizeof(dhcp_server_t));

    printf("DHCP server safely deinitialized\n");
}

static void start_dhcp_server(void)
{
    ip4_addr_t gw, mask;
    IP4_ADDR(&gw, 192, 168, 4, 1);     // Gateway / server IP
    IP4_ADDR(&mask, 255, 255, 255, 0); // Standard /24 netmask

    dhcp_server_init(&dhcp_srv, &gw, &mask);
    printf("DHCP server started\n");
}

static void stop_dhcp_server(void)
{
    dhcp_server_deinit(&dhcp_srv);
    printf("DHCP server stopped\n");
}

static struct udp_pcb *dns_pcb;

// The DNS Reply Header + Answer for "Everything points to 192.168.4.1"
static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (p->len < 12)
    {
        pbuf_free(p);
        return;
    }

    u8_t *req = (u8_t *)p->payload;

    // 1. Find the end of the Question section safely
    int ptr = 12;
    while (ptr < p->len && req[ptr] != 0)
    {
        ptr++;
    }

    // Check if we ran off the end of the packet before finding the null terminator
    if (ptr + 5 > p->len)
    {
        pbuf_free(p);
        return;
    }

    int question_len = ptr - 12 + 1 + 4;        // name + null byte + type + class
    int total_res_len = 12 + question_len + 16; // Header + Question + Answer

    // 2. Allocate exactly what we need
    struct pbuf *res = pbuf_alloc(PBUF_TRANSPORT, total_res_len, PBUF_RAM);
    if (!res)
    {
        pbuf_free(p);
        return;
    }

    u8_t *ans = (u8_t *)res->payload;
    memset(ans, 0, total_res_len);

    // --- Header ---
    memcpy(ans, req, 2); // Transaction ID
    ans[2] = 0x81;
    ans[3] = 0x80; // Standard query response
    ans[4] = 0x00;
    ans[5] = 0x01; // 1 Question
    ans[6] = 0x00;
    ans[7] = 0x01; // 1 Answer

    // --- Copy Question Section ---
    memcpy(&ans[12], &req[12], question_len);

    // --- Answer Section (starts after the copied question) ---
    u8_t *ans_ptr = &ans[12 + question_len];
    *ans_ptr++ = 0xc0;
    *ans_ptr++ = 0x0c; // Pointer to name at offset 12
    *ans_ptr++ = 0x00;
    *ans_ptr++ = 0x01; // Type A
    *ans_ptr++ = 0x00;
    *ans_ptr++ = 0x01; // Class IN
    *ans_ptr++ = 0x00;
    *ans_ptr++ = 0x00; // TTL (4 bytes)
    *ans_ptr++ = 0x00;
    *ans_ptr++ = 0x3c;
    *ans_ptr++ = 0x00;
    *ans_ptr++ = 0x04; // Data Length 4

    // IP: 192.168.4.1
    *ans_ptr++ = 192;
    *ans_ptr++ = 168;
    *ans_ptr++ = 4;
    *ans_ptr++ = 1;

    udp_sendto(pcb, res, addr, port);

    pbuf_free(res);
    pbuf_free(p);
}

void dns_server_init()
{
    dns_pcb = udp_new();
    udp_bind(dns_pcb, IP_ADDR_ANY, DNS_PORT);
    udp_recv(dns_pcb, dns_recv, NULL);
    printf("DNS Redirector initialized (Port 53)\n");
}

void wifi_poll()
{
    cyw43_arch_poll();
}

bool wifi_init(const char *ssid, const char *password, const char *network_name)
{
    if (cyw43_arch_init())
    {
        printf("WiFi init failed\n\n");
        return false;
    }
    if (strlen(network_name) == 0)
    {
        network_name = "batmon";
    }
    cyw43_arch_lwip_begin();

    //error if no SSID provided, otherwise try connecting to WiFi and fall back to AP mode if it fails after multiple attempts
    int error= (strlen(ssid) == 0);
    if (!error)
    {
        struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
        netif_set_hostname(n, network_name);

        cyw43_arch_enable_sta_mode();
        printf("SSID %s:", ssid);
        int timeout = 10;
        error = PICO_ERROR_TIMEOUT;
        while (error != PICO_ERROR_NONE && timeout < 60)
        {
            watchdog_update();
            printf("(%ds timeout).\n\t", timeout);
            error = cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA3_WPA2_AES_PSK, timeout * 1000);
            if(error == PICO_ERROR_TIMEOUT){
                timeout = timeout * 2;
            }
            else {
                timeout = 61; // if not a timeout then exit out of loop
            }
        }
    }
    if (error != PICO_ERROR_NONE)
    {
        printf("Starting AP: %s (Open)\n", network_name);
        cyw43_arch_enable_ap_mode(network_name, NULL, CYW43_AUTH_OPEN);

        start_dhcp_server();
        dns_server_init();
    }
    else
    {
        printf("\nConnected @ http://%s or ", network_name);
        // Disable power management for better responsiveness
        cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
    }

printf("WiFi initialization complete\n");

    cyw43_arch_lwip_end();
    return (error == PICO_ERROR_NONE);
}
