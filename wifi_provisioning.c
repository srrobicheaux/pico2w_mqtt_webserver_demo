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
    cyw43_arch_lwip_end();

    return true;
}

int wifi_settings_JSON(char *payload, size_t len)
{
    return snprintf(payload, len, 
                   "\"wifi\":{\"ssid\":\"%s\",\"net_name\":\"%s\",\"dev_id\":\"%s\"},",
                   wifi_settings->ssid, wifi_settings->network_name, wifi_settings->device_id);
}

bool wifi_init(wifi_t *_wifi_settings)
{
    wifi_settings=_wifi_settings;

    if (_wifi_settings->ssid[0] =='\0') {
        //if ssid isnt set then assign defaults
        printf("Using default:%s\t", SSID);
        strncpy(_wifi_settings->ssid, SSID, 32);
        strncpy(_wifi_settings->password, SSID_PW, 64);
        snprintf(_wifi_settings->network_name, sizeof(_wifi_settings->network_name), "Pico%s", _wifi_settings->device_id);
    }
    if (cyw43_arch_init())
    {
        printf("WiFi init failed\n\n");
        reset();
    }
    cyw43_arch_enable_sta_mode();
    
    // Set the hostname for DHCP requests
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    netif_set_hostname(n, _wifi_settings->network_name);

    int error = cyw43_arch_wifi_connect_timeout_ms(_wifi_settings->ssid, _wifi_settings->password, CYW43_AUTH_WPA3_WPA2_AES_PSK, 30000);
    if (error)
    {
        cyw43_arch_deinit();
        cyw43_arch_lwip_end();

        printf("Failed to connect to %s. Error# %d\n", _wifi_settings->ssid, error);
        wifi_provisioning_start();
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
