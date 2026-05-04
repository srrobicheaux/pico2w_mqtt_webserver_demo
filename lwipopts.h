#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H
#include <stdint.h>


// Basic mode (poll, no OS)
#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

// Memory – critical for raw TCP server in AP mode
#define MEM_LIBC_MALLOC 0  // Use libc malloc (required for poll mode)
#define MEMP_MEM_MALLOC 1
#define MEM_ALIGNMENT 4
#define MEM_SIZE 65536  // 32KB minimum; bump to 65536 if OOM issues

// TCP settings – essential for reliable HTTP
#define LWIP_TCP 1
#define TCP_MSS 1460
#define TCP_WND (8 * TCP_MSS)
#define TCP_SND_BUF (16 * TCP_MSS)
#define TCP_SND_QUEUELEN (8 * TCP_SND_BUF / TCP_MSS)
#define TCP_LISTEN_BACKLOG 1  // Allows queued connections

// Pbufs – more for AP + multiple clients
#define PBUF_POOL_SIZE 48

// Core protocols
#define LWIP_DHCP 1  // For STA mode (AP DHCP is built-in to cyw43)
#define LWIP_DNS 1
#define LWIP_ICMP 1
#define LWIP_UDP 1
#define LWIP_ARP 1
#define LWIP_IGMP 1

// Netif
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1

#define LWIP_NETIF_HOSTNAME     1
#define LWIP_NETCONN            0 // Set to 1 only if using FreeRTOS

// ALTCP/TLS not needed for provisioning
#define LWIP_ALTCP 0

//#define DHCP_DOES_ARP_CHECK 1
//#define DHCP_AUTOIP_CHECK_LINK_UP 1

// Debugging (optional – comment out for production)
#define LWIP_DEBUG 0
//#define TCP_DEBUG LWIP_DBG_ON

#define LWIP_SNTP 1
#define SNTP_SERVER_DNS 1

// 1. ADD THIS PROTOTYPE: This tells LwIP "This function exists, don't panic"
void sntp_set_system_time_us(uint32_t sec, uint32_t us);
// This tells LwIP how to update the internal system time 
// when the NTP packet arrives
#define SNTP_SET_SYSTEM_TIME(sec) sntp_set_system_time_us(sec, 0)

#define MQTT_REQ_MAX_IN_FLIGHT 20
// Add these to lwipopts.h
#define MQTT_OUTPUT_RINGBUF_SIZE 2048  // Default is often too small
#define MQTT_VAR_HEADER_BUFFER_LEN 256  // Helps with long topic names

#endif
















// #ifndef _LWIPOPTS_H
// #define _LWIPOPTS_H

// // Generally you would define your own explicit list of lwIP options
// // (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html)
// //
// // This example uses a common include to avoid repetition
// //#include "lwipopts_examples_common.h"

// // The following is needed to test mDns
// #define LWIP_MDNS_RESPONDER 1
// #define LWIP_IGMP 1
// #define LWIP_NUM_NETIF_CLIENT_DATA 1
// #define MDNS_RESP_USENETIF_EXTCALLBACK  1
// #define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 3)
// #define MEMP_NUM_TCP_PCB 12

// // Enable some httpd features
// #define LWIP_HTTPD_CGI 1
// #define LWIP_HTTPD_SSI 1
// #define LWIP_HTTPD_SSI_MULTIPART 1
// #define LWIP_HTTPD_SUPPORT_POST 0// previously 1
// #define LWIP_HTTPD_SSI_INCLUDE_TAG 1//previously 0

// // Generated file containing html data
// //#define HTTPD_FSDATA_FILE "pico_fsdata.inc"

// #endif
