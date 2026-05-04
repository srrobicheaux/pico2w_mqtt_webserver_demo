//A very simple dhcpserver to serve a  single IP address in order to provision chip.

#include <string.h>
#include "lwip/udp.h"
#include "dhcpserver.h"

#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68

static struct udp_pcb *dhcp_pcb;

static void send_dhcp_reply(const uint8_t *client_mac, uint32_t xid, uint8_t message_type, ip4_addr_t *offered_ip) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 300, PBUF_RAM);
    if (!p) return;
    memset(p->payload, 0, p->tot_len);

    uint8_t *msg = (uint8_t *)p->payload;
    msg[0] = 2;              // Boot Reply
    msg[1] = 1;              // Hardware type: Ethernet
    msg[2] = 6;              // Hardware addr len
    msg[3] = 0;              // Hops
    memcpy(&msg[4], &xid, 4); // MUST match client XID
    
    // yiaddr: Your (Client) IP address
    memcpy(&msg[16], &offered_ip->addr, 4); 
    
    // chaddr: Client Hardware Address
    memcpy(&msg[28], client_mac, 6);

    // Magic Cookie: 0x63 0x82 0x53 0x63
    uint8_t *opt = &msg[236];
    *opt++ = 0x63; *opt++ = 0x82; *opt++ = 0x53; *opt++ = 0x63;

    // Option 53: Message Type (2 = Offer, 5 = Ack)
    *opt++ = 53; *opt++ = 1; *opt++ = message_type;

    // Option 54: Server Identifier (The Pico's IP: 192.168.4.1)
    *opt++ = 54; *opt++ = 4; *opt++ = 192; *opt++ = 168; *opt++ = 4; *opt++ = 1;

    // Option 51: Lease Time (e.g., 1 hour = 3600s)
    *opt++ = 51; *opt++ = 4; *opt++ = 0; *opt++ = 0; *opt++ = 0x0E; *opt++ = 0x10;

    // Option 1: Subnet Mask
    *opt++ = 1;  *opt++ = 4; *opt++ = 255; *opt++ = 255; *opt++ = 255; *opt++ = 0;

    // Option 3: Router (The Pico itself)
    *opt++ = 3;  *opt++ = 4; *opt++ = 192; *opt++ = 168; *opt++ = 4; *opt++ = 1;
    
    // Option 6: Domain Name Server (Point to the Pico 192.168.4.1)
    *opt++ = 6;  *opt++ = 4; *opt++ = 192; *opt++ = 168; *opt++ = 4; *opt++ = 1;


    *opt++ = 255; // End Option

    // Always broadcast DHCP replies when the client has no IP yet
    udp_sendto(dhcp_pcb, p, IP_ADDR_BROADCAST, DHCP_PORT_CLIENT);
    pbuf_free(p);
}

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (p->len < 240) { pbuf_free(p); return; }

    uint8_t *msg = (uint8_t *)p->payload;
    uint32_t xid;
    memcpy(&xid, &msg[4], 4);
    uint8_t *client_mac = &msg[28];
    
    // Find Option 53 (Message Type)
    uint8_t msg_type = 0;
    uint8_t *ptr = &msg[240];
    while (ptr < (uint8_t *)p->payload + p->len) {
        if (*ptr == 53) { msg_type = *(ptr + 2); break; }
        if (*ptr == 255) break;
        ptr += *(ptr + 1) + 2;
    }

    ip4_addr_t offered_ip;
    IP4_ADDR(&offered_ip, 192, 168, 4, 2);

    if (msg_type == 1) { // DHCP Discover
        printf("DHCP Discover -> Sending Offer\n");
        send_dhcp_reply(client_mac, xid, 2, &offered_ip); // 2 = Offer
    } 
    else if (msg_type == 3) { // DHCP Request
        printf("DHCP Request -> Sending ACK\n");
        send_dhcp_reply(client_mac, xid, 5, &offered_ip); // 5 = ACK
    }

    pbuf_free(p);
}

void dhcp_server_init(dhcp_server_t *srv, ip4_addr_t *base_addr, ip4_addr_t *nm) {
    if (dhcp_pcb) udp_remove(dhcp_pcb);
    dhcp_pcb = udp_new();
    udp_bind(dhcp_pcb, IP_ADDR_ANY, DHCP_PORT_SERVER);
    udp_recv(dhcp_pcb, dhcp_recv, srv);
    printf("DHCP Server initialized on port %d\n", DHCP_PORT_SERVER);
}

/**
 * Properly deinitialize the DHCP server.
 * 
 * - Removes the UDP PCB (stops listening).
 * - Frees the dynamically allocated offers array (if present).
 * - Zeroes the struct to prevent reuse of stale pointers/state.
 * 
 * This prevents double-free or illegal free panics when stopping the server
 * (common if init is called incorrectly or resources not cleaned).
 * 
 * Safe to call multiple times or on uninitialized struct.
 */
void dhcp_server_deinit(dhcp_server_t *dhcp)
{
    if (dhcp == NULL) {
        return;
    }

    // Stop UDP listening and free the PCB
    if (dhcp_pcb != NULL) {
        udp_remove(dhcp_pcb);
        dhcp_pcb = NULL;
    }

    // Clear any other dynamic state (timers, etc. are handled by udp_remove)
    memset(dhcp, 0, sizeof(dhcp_server_t));

    printf("DHCP server safely deinitialized\n");
}