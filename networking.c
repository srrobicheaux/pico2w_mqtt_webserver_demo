#include "pico/stdio.h"
#include "pico/cyw43_arch.h"
#include "lwip/prot/dns.h"
#include "networking.h"
#include "hardware/watchdog.h"

#define DNS_PORT 53
#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68

#include "lwip/ip_addr.h"
#include "cJSON.h"

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
    if (dhcp_pcb == NULL)
    {
        return;
    }
    else
    {
        dhcp_server_deinit(&dhcp_srv);
        printf("DHCP server stopped\n");
    }
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

wifi_mode wifi_Connect(cJSON *_networks)
{
    stop_dhcp_server();
    cyw43_arch_enable_sta_mode();

    cJSON *wifi;
    cJSON_ArrayForEach(wifi, _networks)
    {
        if (!cJSON_IsTrue(cJSON_GetObjectItem(wifi, "enabled")))
        {
            break;
        }
        if (!cJSON_IsTrue(cJSON_GetObjectItem(wifi, "found")))
        {
            break;
        }
        int error = PICO_ERROR_NONE;

        char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "ssid"));
        char *password = cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "password"));
        char *network_name = cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "network_name"));
        if (ssid == NULL)
        {
            break; // SSID is required to connect to a network
        }
        if (strlen(network_name) == 0)
        {
            network_name = "batmon"; // Default network name if not provided
        };

        struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
        netif_set_hostname(n, network_name);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        printf("Connectting %s to %s ", network_name, ssid);
        error = cyw43_arch_wifi_connect_blocking(ssid, password, CYW43_AUTH_WPA2_AES_PSK);
        // error = cyw43_arch_wifi_connect_async(ssid, password, CYW43_AUTH_WPA2_AES_PSK);

        if (error != PICO_ERROR_NONE)
        {
            printf(" ... failed with error code: %d\n", error);
            break; // COnitnue others
        }

        return WIFI_CONNECTING;
    }

    return WIFI_SCANED;
}

// Add these above wifi_poll so it knows they exist
bool target_wifi = false;

static int scan_result(void *env, const cyw43_ev_scan_result_t *result)
{
    cJSON *_networks = (cJSON *)env;

    if (result)
    {
        bool new_wifi = true;

        cJSON *wifi_item;
        cJSON_ArrayForEach(wifi_item, _networks)
        {
            char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(wifi_item, "ssid"));
            bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(wifi_item, "enabled"));
            cJSON *fnd = cJSON_GetObjectItem(wifi_item, "found");
            if (!fnd)
            {
                fnd = cJSON_AddBoolToObject(wifi_item, "found", 0);
            }
            cJSON_SetIntValue(fnd, 0);

            // If we find a match and haven't already selected one
            if (ssid && strlen(ssid) > 0 && strcmp(ssid, (char *)result->ssid) == 0)
            {
                if (enabled)
                {
                    //                    printf("Found target network: %s, RSSI: %d, Mode: %d\n", result->ssid, result->rssi, result->auth_mode);
                    // Save the target, but DO NOT connect here.
                    cJSON_SetBoolValue(fnd, 1);

                    target_wifi = true;

                    cJSON *auth = cJSON_GetObjectItem(wifi_item, "auth_mode");
                    if (!auth)
                    {
                        auth = cJSON_AddNumberToObject(wifi_item, "auth_mode", result->auth_mode);
                    }
                    cJSON_SetIntValue(auth, result->auth_mode);
                    cJSON *rssi = cJSON_GetObjectItem(wifi_item, "rssi");
                    if (!rssi)
                    {
                        rssi = cJSON_AddNumberToObject(wifi_item, "rssi", result->rssi);
                    }
                    cJSON_SetIntValue(rssi, result->rssi);
                }
                new_wifi = false;
            }
        }
        printf("%s%-14s\t|\t%d\t|\t%d\t\t|\t%02X:%02X:%02X:%02X:%02X:%02X\t|\t%d\n", new_wifi ? "*" : "", result->ssid, result->rssi, result->auth_mode,result->bssid[0], result->bssid[1], result->bssid[2], result->bssid[3], result->bssid[4], result->bssid[5], result->channel);
        fflush(stdout);

        if (new_wifi)
        {
            cJSON *new_wifi = cJSON_CreateObject();
            cJSON_AddItemToObject(new_wifi, "ssid", cJSON_CreateString((char *)result->ssid));
            cJSON_AddItemToObject(new_wifi, "auth_mode", cJSON_CreateNumber(result->auth_mode));
            cJSON_AddItemToObject(new_wifi, "rssi", cJSON_CreateNumber(result->rssi));
            cJSON_AddItemToObject(new_wifi, "enabled", cJSON_CreateBool(0));
            cJSON_AddItemToObject(new_wifi, "found", cJSON_CreateBool(1));
            cJSON_AddItemToObject(new_wifi, "password", cJSON_CreateString((char *)"NotSet"));
            cJSON_AddItemToObject(_networks, result->ssid, new_wifi);
        }
    }
    return 0;
}

wifi_mode AP_Start()
{
    char *network_name = PROJECT_NAME_STRING;
    printf("Starting AP: http://%s/config (Open)\n", network_name);
    // Ensure STA mode is fully disabled before standing up the AP to prevent lwIP conflicts
    cyw43_arch_disable_sta_mode();
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    netif_set_hostname(n, network_name);
    cyw43_arch_enable_ap_mode(network_name, NULL, CYW43_AUTH_OPEN);

    start_dhcp_server();
    dns_server_init();
    printf("AP initialization complete\n");
    return WIFI_AP_STARTING;
}

wifi_mode start_wifi_scan(cJSON *networks)
{
    cyw43_arch_enable_sta_mode();
    target_wifi = false;
    cyw43_wifi_scan_options_t scan_options = {0};
    int err = cyw43_wifi_scan(&cyw43_state, &scan_options, networks, scan_result);
    if (err == 0)
    {
        printf("SSID\t\t|\tRSSI\t|\tAuth Mode\t|\t\tBSSID\t\t|\tChannel\n");
        return WIFI_SCANNING;
    }
    else
    {
        printf("Failed to start scan: %d\n", err);
        return WIFI_ERROR;
    }
}

/**
 * @brief Gets the current Wi-Fi RSSI (signal strength) in dBm.
 * @return int32_t RSSI value (e.g., -50 to -80), or 0 if not connected/error.
 */
int32_t get_wifi_rssi(void)
{
    int32_t rssi = 0;

    // cyw43_wifi_get_rssi takes the driver state (&cyw43_state)
    // and a pointer to store the resulting integer.
    int err = cyw43_wifi_get_rssi(&cyw43_state, &rssi);

    if (err != 0)
    {
        // Return 0 or an error flag if the call failed or chip isn't ready
        return 0;
    }

    return rssi;
}

wifi_mode wifi_poll(wifi_mode current_mode, cJSON *networks)
{
    switch (current_mode)
    {
    case WIFI_NOT_INITIALIZE:
        if (cyw43_arch_init() == 0)
        {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            return WIFI_DISCONNECTED;
        }
        printf("WiFi init failed\n");
        return WIFI_ERROR;

    case WIFI_ERROR:
        if (cyw43_arch_async_context() != NULL)
        {
            cyw43_arch_deinit();
        }
        return WIFI_NOT_INITIALIZE;

        case WIFI_DISCONNECTED:
        if (!networks)
        {
            printf("No enabled Wifi networks.\n");
            return WIFI_SCANED;
        }

        printf("Looking for enabled Wifi...\n");
        return start_wifi_scan(networks);

    case WIFI_SCANNING:
        cyw43_arch_poll();

        if (cyw43_wifi_scan_active(&cyw43_state))
        {
            return WIFI_SCANNING;
        }
        else
        {
            if (target_wifi == true)
            {
                return WIFI_FOUND;
            }
            return WIFI_SCANED;
        }
    case WIFI_FOUND:
//        cyw43_arch_poll();
        printf("* - Discovered network temporarily stored (not enabled) in configuration.\n");
        return wifi_Connect(networks);

    case WIFI_CONNECTING:
        cyw43_arch_poll();
//        cyw43_arch_wait_for_work_until(make_timeout_time_ms(50));

        int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        // link status | Meaning -------------------|-------- CYW43_LINK_DOWN | Wifi down CYW43_LINK_JOIN | Connected to wifi CYW43_LINK_NOIP | Connected to wifi, but no IP address CYW43_LINK_UP | Connect to wifi with an IP address CYW43_LINK_FAIL | Connection failed CYW43_LINK_NONET | No matching SSID found (could be out of range, or down) CYW43_LINK_BADAUTH | Authenticatation failure

        switch (link_status)
        {
        case CYW43_LINK_DOWN:
        case CYW43_LINK_JOIN:
        case CYW43_LINK_NOIP:
            return WIFI_CONNECTING;
        case CYW43_LINK_UP:
            printf(" ... done in %d seconds\n", time_us_32() / 1000000);
            fflush(stdout);
            sleep_ms(20);

            return WIFI_CONNECTED;
        case CYW43_LINK_FAIL:
        case CYW43_LINK_NONET:
        case CYW43_LINK_BADAUTH:
            return WIFI_DISCONNECTED;
        default:
            return WIFI_DISCONNECTED;
        }

    case WIFI_CONNECTED:
        cyw43_arch_poll();
        if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP)
        {
            return WIFI_DISCONNECTED;
        }
        return WIFI_CONNECTED;

    case WIFI_SCANED:
        printf("Access Point Start\n");
        return AP_Start();


    case WIFI_AP_STARTING:
        cyw43_arch_poll();

        if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_AP) == CYW43_LINK_UP)
        {
            return WIFI_AP;
        }
        return WIFI_AP_STARTING;

    case WIFI_AP:
        cyw43_arch_poll();

        // Stable state, do nothing
        // probably need a timer to retry connection after a while if no clients are connected
        return WIFI_AP;

    default:
        return current_mode;
    }
}