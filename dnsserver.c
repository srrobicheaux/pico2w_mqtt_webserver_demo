#include <stdint.h>
#include <string.h>
#include "pico/stdio.h"
#include "lwip/udp.h"
#include "lwip/prot/dns.h"

#define DNS_PORT 53

static struct udp_pcb *dns_pcb;

// The DNS Reply Header + Answer for "Everything points to 192.168.4.1"
static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (p->len < 12) { pbuf_free(p); return; }

    uint8_t *req = (uint8_t *)p->payload;

    // 1. Find the end of the Question section safely
    int ptr = 12;
    while (ptr < p->len && req[ptr] != 0) {
        ptr++; 
    }
    
    // Check if we ran off the end of the packet before finding the null terminator
    if (ptr + 5 > p->len) { pbuf_free(p); return; }
    
    int question_len = ptr - 12 + 1 + 4; // name + null byte + type + class
    int total_res_len = 12 + question_len + 16; // Header + Question + Answer

    // 2. Allocate exactly what we need
    struct pbuf *res = pbuf_alloc(PBUF_TRANSPORT, total_res_len, PBUF_RAM);
    if (!res) { pbuf_free(p); return; }
    
    uint8_t *ans = (uint8_t *)res->payload;
    memset(ans, 0, total_res_len);

    // --- Header ---
    memcpy(ans, req, 2);            // Transaction ID
    ans[2] = 0x81; ans[3] = 0x80;   // Standard query response
    ans[4] = 0x00; ans[5] = 0x01;   // 1 Question
    ans[6] = 0x00; ans[7] = 0x01;   // 1 Answer

    // --- Copy Question Section ---
    memcpy(&ans[12], &req[12], question_len);

    // --- Answer Section (starts after the copied question) ---
    uint8_t *ans_ptr = &ans[12 + question_len];
    *ans_ptr++ = 0xc0; *ans_ptr++ = 0x0c; // Pointer to name at offset 12
    *ans_ptr++ = 0x00; *ans_ptr++ = 0x01; // Type A
    *ans_ptr++ = 0x00; *ans_ptr++ = 0x01; // Class IN
    *ans_ptr++ = 0x00; *ans_ptr++ = 0x00; // TTL (4 bytes)
    *ans_ptr++ = 0x00; *ans_ptr++ = 0x3c; 
    *ans_ptr++ = 0x00; *ans_ptr++ = 0x04; // Data Length 4
    
    // IP: 192.168.4.1
    *ans_ptr++ = 192; *ans_ptr++ = 168; *ans_ptr++ = 4; *ans_ptr++ = 1;

    udp_sendto(pcb, res, addr, port);
    
    pbuf_free(res);
    pbuf_free(p);
}

void dns_server_init() {
    dns_pcb = udp_new();
    udp_bind(dns_pcb, IP_ADDR_ANY, DNS_PORT);
    udp_recv(dns_pcb, dns_recv, NULL);
    printf("DNS Redirector initialized (Port 53)\n");
}