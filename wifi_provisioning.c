#include "pico/cyw43_arch.h"
#include "wifi_provisioning.h"
#include "secrets.h"
#include "lwip/tcp.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "system_info.h"

#define RESET_TIME 15000000 * 100

//confusing global parameter needs to be eliminatged
static dhcp_server_t dhcp_srv;
wifi_t *wifi_settings;

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

bool wifi_provisioning_start()
{
	cyw43_arch_lwip_begin();

    if (cyw43_arch_init())
    {
        printf("WiFi init failed\n");
        reset();
    }
    const char *ap_ssid = PROJECT_ID;
    printf("Starting AP: %s (Open)\n", ap_ssid);
    cyw43_arch_enable_ap_mode(ap_ssid, NULL, CYW43_AUTH_OPEN);

    start_dhcp_server();
    dns_server_init();

    printf("Provision url:\t");
//    cyw43_arch_lwip_end();

    return true;
}

bool wifi_init(wifi_t *_wifi_settings)
{
   wifi_settings=_wifi_settings;

    if (cyw43_arch_init())
    {
        printf("WiFi init failed\n\n");
        reset();
    }
    cyw43_arch_enable_sta_mode();
    
    // Set the hostname for DHCP requests
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    netif_set_hostname(n, _wifi_settings->network_name);
    printf("Attempting to connect to WiFi SSID: %s\t", _wifi_settings->ssid);

    int error = cyw43_arch_wifi_connect_timeout_ms(_wifi_settings->ssid, _wifi_settings->password, CYW43_AUTH_WPA3_WPA2_AES_PSK, 30000);
    if (error)
    {
        printf("Failed to connect to %s. Error# %d\n", _wifi_settings->ssid, error);
        cyw43_arch_deinit();
        cyw43_arch_lwip_end();
        return false;
    }
	else
	{
		printf("Connected:\t%s\n", _wifi_settings->ssid);
		
		// Disable power management for better responsiveness
		cyw43_arch_lwip_begin();
		cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

		return true;
	}
	cyw43_arch_lwip_end();
}
