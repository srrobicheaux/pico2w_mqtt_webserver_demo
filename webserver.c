#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

#include "flash.h"
#include "html.dashboard.h"
#include "html.wifiform.h"
#include "html.wifisuccess.h"

#include "system_info.h"

#define HTTP_PORT 80
// #define DEBUG 1

// DeviceSettings *settings;

// other responses defined n the html files
#define RESPOND_NOT_FOUND "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 - File Not Found</h1></body></html>"
#define RESPOND_SSE "data: {\"trend\":\"STARTED\",\"uptime\":0}\r\n\r\n"
#define HEADER_CONTINUE "HTTP/1.1 100 Continue\r\n\r\n"
// this one needs to be modifiable to add length
#define HEADER_SUCCESS            \
    "HTTP/1.1 200 OK\r\n"         \
    "Content-Type: text/html\r\n" \
    "Connection: close\r\n"       \
    "Cache-Control: no-cache\r\n" \
    "Content-Length:%d\r\n\r\n"
#define HEADER_JSON                      \
    "HTTP/1.1 200 OK\r\n"                \
    "Content-Type: application/json\r\n" \
    "Connection: close\r\n"              \
    "Cache-Control: no-cache\r\n"        \
    "Content-Length:%d\r\n\r\n"
#define HEADER_SSE                        \
    "HTTP/1.1 200 OK\r\n"                 \
    "Content-Type: text/event-stream\r\n" \
    "Cache-Control: no-cache\r\n"         \
    "Connection: keep-alive\r\n"          \
    "Access-Control-Allow-Origin: *\r\n\r\n"
#define HEADER_REDIRECT                \
    "HTTP/1.1 302 Found\r\n"           \
    "Location: http://192.168.4.1\r\n" \
    "Connection: close\r\n"            \
    "Content-Length: %d\r\n\r\n"
#define HEADER_NOT_FOUND                         \
    "HTTP/1.1 404 Not Found\r\n"                 \
    "Content-Type: text/html; charset=utf-8\r\n" \
    "Content-Length: %d\r\n\r\n"

// SSE clients (simple single-client for now – extend to list if needed)
static struct tcp_pcb *sse_client = NULL;
// confusing global parameter needs to be eliminatged
static bool provisioning;
static volatile bool in_recv_handler = false; // ← prevents overlapping calls

static err_t http_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    // Close after send complete for non-SSE
    if (tpcb != sse_client)
    {
        tcp_close(tpcb);
    }
    return ERR_OK;
}

// Define this at the top of your file or in a header
int hex_char_to_int(char c)
{
    c = tolower((unsigned char)c);
    if (isdigit(c))
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

void url_decode(char *dst, size_t dst_len, const char *src)
{
    char *p_dst = dst;
    const char *p_src = src;
    size_t written = 0;

    while (*p_src && written < dst_len - 1) // Added bounds check here
    {
        if (*p_src == '%' && isxdigit(p_src[1]) && isxdigit(p_src[2]))
        {
            int high = hex_char_to_int(p_src[1]);
            int low = hex_char_to_int(p_src[2]);
            *p_dst++ = (char)((high << 4) | low);
            p_src += 3;
        }
        else if (*p_src == '+')
        {
            *p_dst++ = ' ';
            p_src++;
        }
        else
        {
            *p_dst++ = *p_src++;
        }
        written++;
    }
    *p_dst = '\0';
}

// Remove url_decode from here!
bool parser(char *haystack, const char *needle, char *destination, size_t dest_max_len)
{
    char *ptr = haystack;
    size_t needle_len = strlen(needle);

    while ((ptr = strstr(ptr, needle)) != NULL)
    {
        bool is_start = (ptr == haystack || *(ptr - 1) == '?' || *(ptr - 1) == '&');

        if (is_start && ptr[needle_len] == '=')
        {
            char *value = ptr + needle_len + 1;
            int len = strcspn(value, "&\0");
            if (len == 0)
            {
                destination[0] = '\0';
            }
            else
            {
                size_t copy_len = (len < dest_max_len - 1) ? len : dest_max_len - 1;
                strncpy(destination, value, copy_len);
                destination[copy_len] = '\0';
            }
            return true;
        }
        ptr++;
    }
    return false;
}

static void http_err_cb(void *arg, err_t err)
{
    struct tcp_pcb *tpcb = (struct tcp_pcb *)arg;
    if (tpcb == sse_client)
    {
        printf("SSE client disconnected via error\n");
        sse_client = NULL;
    }
}

static char response_buffer[1024] = {0}; // increased size
// ──────────────────────────────────────────────────────────────
// Main callback - heavily improved
static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (in_recv_handler)
    { // ← re-entrancy guard
        pbuf_free(p);
        return ERR_OK;
    }
    in_recv_handler = true;

    if (p == NULL)
    {
        in_recv_handler = false;
        return ERR_BUF;
    }

    if (err != ERR_OK)
    {
        printf("HTTP error:%d\n", err);
        if (tpcb == sse_client)
            sse_client = NULL;
        pbuf_free(p);
        in_recv_handler = false;
        return ERR_RST;
    }

    tcp_recved(tpcb, p->tot_len);

    // Use a larger static request buffer (safe for typical browser requests)
    static char req[2048];
    req[0] = '\0';

    int length = MIN(p->len, sizeof(req) - 1);
    memcpy(req, p->payload, length);
    req[length] = '\0';

    // ───── Local buffers for this request (eliminates most races) ─────
    char header_buffer[512] = {0};
    char *header = header_buffer;
    char *response = response_buffer;

    // ───── POST handling (cleaner state machine) ─────
    static bool responding = false;

    if (responding || req[0] == 'P')
    {
        char posted[512] = {0};
        char *body = responding ? req : (strstr(req, "\r\n\r\n") ? strstr(req, "\r\n\r\n") + 4 : req);
        responding = true;

        if (strncmp(req, "POST /save_settings", 19) == 0)
        {
            printf("Processing JSON Settings Save...\n");
//###############################################################################
            // For JSON, we do NOT use url_decode. We save the raw body.
           // save_settings_to_flash(body);

            // Respond with Success JSON
            header = HEADER_JSON;
            response = "{\"status\":\"ok\"}";
        }
        else
        {
            url_decode(posted, sizeof(posted), body);

            responding = false;

            if (provisioning) // && settings->wifi.ssid[0] != '\0'
            {
                provisioning = false;
                response = RESPOND_SUCCESS; // you have this defined somewhere
            }
            else
            {
                response = RESPOND_DASHBOARD;
            }
        }
    }
    // ───── GET handling (clean switch + local buffers) ─────
    else if (req[0] == 'G')
    {
        switch (req[5])
        {
        case 'e': // GET /events
            if (strncmp(req, "GET /events ", 12) == 0)
            {
                // If there's an existing client, don't just overwrite!
                // Logic: The browser is refreshing, the old PCB is now invalid.
                if (sse_client != NULL && sse_client != tpcb)
                {
                    tcp_abort(sse_client);
                }

                sse_client = tpcb;

                // Register the error callback so we know if this client dies
                tcp_arg(tpcb, tpcb);
                tcp_err(tpcb, http_err_cb);

                header = HEADER_SSE;
                response = RESPOND_SSE;
                printf("SSE client connected\n");
            }
            break;
        case 's': // /settings
            if (strncmp(req, "GET /settings ", 14) == 0)
            {
                printf("Serving Settings JSON\n");

                // We use a pointer to keep track of where we are writing in the buffer
                char *p = response_buffer;
                size_t rem = sizeof(response_buffer);
                int len;
                p[0] = '{';
                p += 1;
                rem -= 1;

                // 1. Root and WiFi/MQTT info
                len = wifi_settings_JSON(p, rem);
                p += len;
                rem -= len;
                len = mqtt_settings_JSON(p, rem);
                p += len;
                rem -= len;
                len = IOs_settings_JSON(p, rem);
                p += len;
                rem -= len;

                // 4. Close JSON
                len = snprintf(p, rem, "}");
                p += len;
                rem -= len;

                int total_json_len = p - response_buffer;

                snprintf(header_buffer, sizeof(header_buffer), HEADER_JSON, total_json_len);
                header = header_buffer;
                response = response_buffer;
            }
            break;

        case 'p': // GET /pio=
            if (strncmp(req, "GET /pio=", 9) == 0)
            {
                int pio = atoi(req + 9);
                gpio_action_t action = GPIO_ACTION_READ;

                if (strstr(req, "toggle"))
                    action = GPIO_ACTION_TOGGLE;
                else if (strstr(req, "press"))
                    action = GPIO_ACTION_PRESS;

                bool final_state = pin_action(pio, action);

                header = HEADER_JSON;
                snprintf(response_buffer, sizeof(response_buffer),
                         "{\"pio\":%d, \"value\":%s}\r\n",
                         pio, (final_state ? "true" : "false"));
                response = response_buffer;
            }
            break;

        case 'd': // /dashboard
            if (strncmp(req, "GET /dashboard ", 15) == 0)
                response = RESPOND_DASHBOARD;
            break;

        case 'c': // /config
            if (strncmp(req, "GET /config ", 12) == 0)
                response = RESPOND_CONFIG;
            break;

        default: // root or captive portal
            if (provisioning)
            {
                if (strstr(req, "captive.apple.com"))
                    snprintf(header_buffer, sizeof(header_buffer), HEADER_REDIRECT, 0);
                else
                {
                    header = HEADER_SUCCESS;
                    response = RESPOND_CONFIG;
                }
            }
            else
            {
                header = HEADER_SUCCESS;
                response = RESPOND_DASHBOARD;
            }
            break;
        }
    }

    // Fallback 404
    if (response[0] == '\0')
    {
        header = HEADER_NOT_FOUND;
        response = RESPOND_NOT_FOUND;
    }

    // Add Content-Length if needed
    if (strstr(header, "Content-Length:") != NULL)
    {
        snprintf(header_buffer, sizeof(header_buffer), header, strlen(response));
        header = header_buffer;
    }

    // ───── Send response safely ─────
    cyw43_arch_lwip_begin();

    tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    if (response && response[0])
    {
        tcp_write(tpcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
    }
    tcp_output(tpcb);

    cyw43_arch_lwip_end();

    pbuf_free(p);
    in_recv_handler = false;

    return ERR_OK;
}

static err_t http_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    if (err != ERR_OK || newpcb == NULL)
        return ERR_MEM;

    tcp_recv(newpcb, http_recv_cb);
    tcp_sent(newpcb, http_sent_cb);
    tcp_nagle_disable(newpcb);
    return ERR_OK;
}

static struct tcp_pcb *listen_pcb = NULL;

bool webserver_init(bool _provisioning)
{
    static bool running = false;
    if (running)
        return true;
    running = true;

    provisioning = _provisioning;
    //    settings = _settings;

    listen_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!listen_pcb)
    {
        printf("tcp_new failed\n");
        return false;
    }

    err_t err = tcp_bind(listen_pcb, IP_ADDR_ANY, HTTP_PORT);
    if (err != ERR_OK)
    {
        printf("tcp_bind failed: %d\n", err);
        return false;
    }

    listen_pcb = tcp_listen_with_backlog(listen_pcb, TCP_DEFAULT_LISTEN_BACKLOG);
    if (!listen_pcb)
    {
        printf("tcp_listen failed\n");
        return false;
    }

    tcp_accept(listen_pcb, http_accept_cb);

    printf("Web server listening on http://%s:80\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    return true;
}

// In webserver.c

void webserver_push_update(const char *topic, const char *json_payload)
{
    // Safety check for client and connection state
    if (!sse_client || sse_client->state != ESTABLISHED)
    {
        sse_client = NULL;
        return;
    }

    // Allocate a buffer on the stack for the SSE frame
    // 1024 is safer for the large 'analog' array payload
    char sse_buffer[1024];

    // This wraps the payload so: topic="temperature", payload="98.1"
    // becomes: data: {"temperature":98.1}
    int len = snprintf(sse_buffer, sizeof(sse_buffer), "data: {\"%s\":%s}\r\n\r\n", topic, json_payload);

    if (len < 0 || len >= sizeof(sse_buffer))
    {
        printf("SSE Payload too large!\n");
        return;
    }

    // Check if the TCP buffer has enough space to avoid blocking
    if (tcp_sndbuf(sse_client) >= len)
    {
        cyw43_arch_lwip_begin();
        err_t err = tcp_write(sse_client, sse_buffer, len, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK)
        {
            tcp_output(sse_client); // Force the packet out immediately
        }
        else
        {
            // If write fails, the client likely disconnected
            printf("SSE Write failed: %d\n", err);
            sse_client = NULL;
        }
        cyw43_arch_lwip_end();
    }
    else
    {
        // Optional: Log that we are skipping a frame because the network is slow
        printf("SSE Buffer full, skipping frame\n");
    }
}