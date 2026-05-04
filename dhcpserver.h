#ifndef _DHCPSERVER_H_
#define _DHCPSERVER_H_

#include "lwip/ip_addr.h"

typedef struct dhcp_entry_t {
    uint8_t mac[6];
    ip4_addr_t addr;
    uint32_t leased_until;
} dhcp_entry_t;

typedef struct {
    ip4_addr_t start_ip;
    ip4_addr_t end_ip;
    dhcp_entry_t entries[8]; // Max 8 clients
} dhcp_server_t;

void dhcp_server_init(dhcp_server_t *srv, ip4_addr_t *base_addr, ip4_addr_t *nm);
void dhcp_server_deinit(dhcp_server_t *dhcp);

#endif