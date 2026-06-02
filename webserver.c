#include "cJSON.h"

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "webserver.h"
#include "flash_manager.h"
#include "build/dashboard_html_embed.h"
#include "build/config_html_embed.h"
#include "io_manager.h"
#include <string.h>
#include <stdio.h>

#define PORT 80

static struct tcp_pcb *sse_pcb = NULL;

// ==================== SAFARI / iOS FRIENDLY HEADERS ====================
#define HEADER_HTML                                          \
    "HTTP/1.1 200 OK\r\n"                                    \
    "Content-Type: text/html; charset=UTF-8\r\n"             \
    "Cache-Control: no-cache, no-store, must-revalidate\r\n" \
    "Pragma: no-cache\r\n"                                   \
    "Expires: 0\r\n"                                         \
    "Connection: close\r\n"                                  \
    "Content-Length: %d\r\n\r\n"

#define HEADER_SSE                                       \
    "HTTP/1.1 200 OK\r\n"                                \
    "Content-Type: text/event-stream; charset=UTF-8\r\n" \
    "Cache-Control: no-cache\r\n"                        \
    "Connection: keep-alive\r\n"                         \
    "Access-Control-Allow-Origin: *\r\n\r\n"

#define HEADER_JSON                                     \
    "HTTP/1.1 200 OK\r\n"                               \
    "Content-Type: application/json; charset=UTF-8\r\n" \
    "Cache-Control: no-cache, no-store\r\n"             \
    "Pragma: no-cache\r\n"                              \
    "Connection: close\r\n"                             \
    "Content-Length: %d\r\n\r\n"

#define HEADER_REDIRECT                       \
    "HTTP/1.1 302 Found\r\n"                  \
    "Location: http://192.168.4.1/config\r\n" \
    "Connection: close\r\n\r\n"

char buffer[4000];
static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    // TODO is_provisioning
    bool is_provisioning = false;

    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    static bool posting = false;
    cJSON *config = (cJSON *)arg;

    tcp_recved(tpcb, p->tot_len);
    char *req = (char *)p->payload;

    static int contentLength = 0;
    //    printf("Received request (%d bytes): %.20s ... %.20s\n", p->tot_len, req, req + p->tot_len - 20);

    // POST save settings
    if (strncmp(req, "GET ", 4) == 0)
    {
        posting = false;
        contentLength = 0;
    }

    // restart
    if (strncmp(req, "POST /restart", 12) == 0)
    {
        printf("Rebooting from request\n");
        watchdog_reboot(0, 0, 1500);

        pbuf_free(p);
        return ERR_OK;
    }

    if (strncmp(req, "POST /save", 10) == 0)
    {
        posting = true;
        buffer[0] = '\0';
        contentLength = atoi(strstr(req, "Content-Length: ") + sizeof("Content-Length: ") - 1);
        printf("Content-Length:%d\n", contentLength);
        return ERR_OK;
    }

    if (posting)
    {
        // keep appending onto the end of buffer until you have Content-Length
        int len = strlen(buffer) + p->tot_len;
        if (len < sizeof(buffer))
        {
            memcpy(buffer + strlen(buffer), req, p->tot_len);
            buffer[len] = '\0';
        }
        else
        {
            printf("Buffer Overflow Overted!\n");
            buffer[0] = '\0';
            posting = false;
            return ERR_MEM;
        }

        if (len == contentLength)
        {
            // position at the last of the received data that is the length of Content-Length
            cJSON *new_values = cJSON_Parse(buffer + len - contentLength);
            if (new_values)
            {
                printf("TODO: validate saving of passwords\n");
                cJSON *new_wifi_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(new_values, "wifi"), "password");
                cJSON *old_wifi_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(config, "wifi"), "password");
                if (strncmp(cJSON_GetStringValue(new_wifi_pw), "*****", 64) == 0)
                {
                    cJSON_SetValuestring(new_wifi_pw, cJSON_GetStringValue(old_wifi_pw));
                }
                cJSON *new_mqtt_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(new_values, "mqtt"), "password");
                cJSON *old_mqtt_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(config, "mqtt"), "password");
                if (strncmp(cJSON_GetStringValue(new_mqtt_pw), "*****", 64) == 0)
                {
                    cJSON_SetValuestring(new_mqtt_pw, cJSON_GetStringValue(old_mqtt_pw));
                }

                cJSON_Delete(config);            // free the old config
                flash_save_settings(new_values); // print json string to flash storage
                config = load_configuration();
                cJSON_Delete(new_values); // free the old config

                posting = false;
                buffer[0] = '\0';
                contentLength = 0;

                const char *resp = "{\"status\":\"ok\",\"message\":\"Saved.\"}";
                char header[256];
                snprintf(header, sizeof(header), HEADER_JSON, strlen(resp));
                tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
                tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
                tcp_output(tpcb);
                pbuf_free(p);
                return ERR_OK;
            }
            else
            {
                printf("Buffer Error:%s\n", buffer);
                cJSON_Delete(new_values);
                pbuf_free(p);
                return ERR_ABRT;
            }
            return ERR_OK;
        }
    }

    // Captive portal redirects (Apple, Android, etc.)
    else if (strstr(req, "captive.apple.com") || strstr(req, "hotspot-detect.html") || strstr(req, "connectivity-check"))
    {
        tcp_write(tpcb, HEADER_REDIRECT, strlen(HEADER_REDIRECT), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        pbuf_free(p);
        return ERR_OK;
    }

    // Settings
    else if (strncmp(req, "GET /settings", 11) == 0)
    {
        config = load_configuration();
        // todo need to scrap passwords before sending config to client
        // scrape passwords for setings response
        char temp_wifi[64];
        char temp_mqtt[64];

        cJSON *wifi_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(config, "wifi"), "password");
        cJSON *mqtt_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(config, "mqtt"), "password");
        strncpy(temp_wifi, cJSON_GetStringValue(wifi_pw), 64);
        strncpy(temp_mqtt, cJSON_GetStringValue(mqtt_pw), 64);

        cJSON_SetValuestring(wifi_pw, "*****");
        cJSON_SetValuestring(mqtt_pw, "*****");

        if (cJSON_PrintPreallocated(config, buffer, sizeof(buffer), 0))
        {

            printf("Serving settings(%d bytes)\n", strlen(buffer));
            char header[256];
            snprintf(header, sizeof(header), HEADER_HTML, strlen(buffer));
            tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
            tcp_write(tpcb, buffer, strlen(buffer), TCP_WRITE_FLAG_COPY);
            tcp_output(tpcb);
            cJSON_SetValuestring(wifi_pw, temp_wifi);
            cJSON_SetValuestring(mqtt_pw, temp_mqtt);

            pbuf_free(p);
            return ERR_OK;
        }
        else
        {
            cJSON_SetValuestring(wifi_pw, temp_wifi);
            cJSON_SetValuestring(mqtt_pw, temp_mqtt);
            printf("Failed Serving settings(%d bytes)\n", strlen(buffer));
            return ERR_MEM;
        }
    }

    // start SSE Events
    else if (strncmp(req, "GET /events", 11) == 0)
    {
        printf("Starting SSE Events\n");
        sse_pcb = tpcb;
        tcp_write(tpcb, HEADER_SSE, strlen(HEADER_SSE), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        pbuf_free(p);
        return ERR_OK; // Keep connection open
    }

    // Config page
    else if (strncmp(req, "GET /config", 11) == 0 || is_provisioning)
    {
        printf("Serving config page (%d bytes) of %d\n", config_html_len, tcp_sndbuf(tpcb));

        char header[256];
        snprintf(header, sizeof(header), HEADER_HTML, config_html_len);

        err_t h_err = tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        err_t d_err = tcp_write(tpcb, config_html, config_html_len, TCP_WRITE_FLAG_COPY);
        // printf("Header send status: %d, Data send status: %d\n", h_err, d_err);

        tcp_output(tpcb);
        pbuf_free(p);
        return ERR_OK;
    }
    // toggle pin
    else if (strncmp(req, "GET /pio=", 9) == 0)
    {
        int pin = atoi(req+9);
        printf("Todo: set settings value and then mark channel dirty. Toggling %d\n",pin);
        toggle_pin(pin);

        pbuf_free(p);
        return ERR_OK;
    }
    // ignore favicon
    else if (strncmp(req, "GET /favicon.ico", 16) == 0)
    {
        pbuf_free(p);
        return ERR_OK;
    }
    // ignore favicon
    else if (strncmp(req, "GET /addchannel", 15) == 0)
    {
        pbuf_free(p);
        return ERR_OK;
    }

    if (!posting)
    {
        printf("Request:%.20s\n", req);
        // Dashboard (default)
        printf("Serving dashboard page (%d bytes)\n", dashboard_html_len);
        char header[256];
        snprintf(header, sizeof(header), HEADER_HTML, dashboard_html_len);
        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, dashboard_html, dashboard_html_len, TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        pbuf_free(p);
    }
    return ERR_OK;
}

static err_t http_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    // Pass the config pointer (arg) down to the new connection
    tcp_arg(newpcb, arg);
    tcp_recv(newpcb, http_recv_cb);
    return ERR_OK;
}

void start_webserver(cJSON *config)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
        return;

    // Bind the argument BEFORE listening
    tcp_arg(pcb, config);
    tcp_bind(pcb, IP_ADDR_ANY, PORT);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, http_accept_cb);

    printf("http://%s:80\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
}

// Call this from main loop to push updates
void webserver_send_sse_update(cJSON *updates)
{
    if (!sse_pcb)
        return;

    char payload[512] = "data: ";
    if (cJSON_PrintPreallocated(updates, payload + strlen(payload), sizeof(payload) - strlen(payload), false))
    {
        strcat(payload, "\n\n");
        tcp_write(sse_pcb, payload, strlen(payload), TCP_WRITE_FLAG_COPY);
        tcp_output(sse_pcb);
    }
}
