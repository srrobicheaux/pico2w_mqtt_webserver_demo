#include "cJSON.h"

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <stdlib.h> // Required for calloc/free
#include "webserver.h"
#include "flash_manager.h"
#include "build/config_embed.h"
#include "build/dashboard_embed.h"
#include "build/alpine.min_embed.h"
#include "build/tailwind.min_embed.h"
#include "build/chart_embed.h"
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

#define HEADER_HTML_GZIP                                     \
    "HTTP/1.1 200 OK\r\n"                                    \
    "Content-Type: text/html; charset=UTF-8\r\n"             \
    "Content-Encoding: gzip\r\n"                             \
    "Cache-Control: no-cache, no-store, must-revalidate\r\n" \
    "Pragma: no-cache\r\n"                                   \
    "Expires: 0\r\n"                                         \
    "Connection: close\r\n"                                  \
    "Content-Length: %d\r\n\r\n"

// ==================== STATE MACHINE ====================

// Tracks the state of chunked file transfers per connection
typedef struct
{
    cJSON *config;
    const uint8_t *file_data;
    size_t total_len;
    size_t bytes_sent;
} http_state_t;

// Sends the next chunk of data that fits in the lwIP buffer
static void send_next_chunk(struct tcp_pcb *tpcb, http_state_t *state)
{
    if (!state->file_data || state->bytes_sent >= state->total_len)
    {
        return; // File transfer complete, wait for client to close socket
    }

    size_t bytes_left = state->total_len - state->bytes_sent;
    size_t space_avail = tcp_sndbuf(tpcb);
    size_t chunk = (bytes_left < space_avail) ? bytes_left : space_avail;

    if (chunk > 0)
    {
        err_t err = tcp_write(tpcb, state->file_data + state->bytes_sent, chunk, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK)
        {
            state->bytes_sent += chunk;
            tcp_output(tpcb); // Fire immediately
        }
    }
}

// Callback triggered by lwIP whenever the client ACKs previous data
static err_t http_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    http_state_t *state = (http_state_t *)arg;
    if (state)
    {
        send_next_chunk(tpcb, state);
    }
    return ERR_OK;
}

// Cleans up state memory if connection is aborted (e.g. RST from client)
static void http_err_cb(void *arg, err_t err)
{
    if (arg)
    {
        free(arg);
    }
}

// ==================== HTTP SERVER ====================

static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    static char buffer[5000];
    char header[256];
    static bool posting = false;
    static int contentLength = 0;
    static char *buffer_ptr;

    http_state_t *state = (http_state_t *)arg;
    bool is_provisioning = false;

    // Client closed the connection
    if (!p)
    {
        if (tpcb == sse_pcb)
            sse_pcb = NULL; // Prevent dangling pointer
        // do we need to check if this free the json config object?  I think not, since it is managed by the main loop and not the webserver
        if (state)
            free(state);
        tcp_close(tpcb);
        return ERR_OK;
    }

    // maybe this should be p->len
    tcp_recved(tpcb, p->len);
    //    tcp_recved(tpcb, p->tot_len);

    char *req = (char *)p->payload;

    if (p->tot_len > sizeof(buffer) - 1)
    {
        printf("Buffer Overflow Averted!\n");
        buffer[0] = '\0';
        posting = false;
        return ERR_MEM;
    }

    // POST save settings reset
    if (strncmp(req, "GET ", 4) == 0)
    {
        posting = false;
        contentLength = 0;
        buffer_ptr = buffer;
    }
    // POST save settings reset
    if (strncmp(req, "POST ", 5) == 0)
    {
        posting = true;

        // Content-Length: 1818

        contentLength = atoi(strstr(req, "Content-Length: ") + 16);
        buffer[0] = '\0';
        buffer_ptr = buffer;
    }
    if (posting)
    {
        memcpy(buffer_ptr, p->payload, p->len);
        buffer_ptr[p->len] = '\0';
        buffer_ptr += p->len;

        if (strlen(strstr(buffer, "\r\n\r\n") + 4) < contentLength)
        {
            tcp_output(tpcb);
            pbuf_free(p);
            return ERR_OK;
        }
        else
        {
            posting = false; // Reset for next request
        }
        req = buffer; // Redirect to the full buffer for processing
    }

    //    printf("Request (%d bytes)\n", contentLength);

    // restart
    if (strncmp(req, "POST /restart", 13) == 0)
    {
        printf("Rebooting from request\n");
        const char *resp = "{\"status\":\"ok\",\"message\":\"Rebooted.\"}";
        snprintf(header, sizeof(header), HEADER_JSON, strlen(resp));
        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        pbuf_free(p);
        watchdog_enable(10, 1);
        return ERR_OK;
    }

    if (strncmp(req, "POST /save", 10) == 0)
    {
        buffer_ptr = strstr(req, "\r\n\r\n") + 4;
        const char *resp = "";

        int error = load_flash_buffer(buffer_ptr, contentLength, state->config);

        if (error)
        {
            printf("Parse error near position %d in \n#%s#\n", error, buffer_ptr);
            resp = "{\"status\":\"error\",\"message\":\"Failed to parse settings.\"}";
        }
        else
        {
            resp = "{\"status\":\"ok\",\"message\":\"Buffered.\"}";
            cJSON_SetBoolValue(cJSON_GetObjectItem(state->config, "is_dirty"), 1);
        }
        snprintf(header, sizeof(header), HEADER_JSON, strlen(resp));
        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        pbuf_free(p);
        return ERR_OK;
    }

    // Captive portal redirects
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
        if (state)
            state->config = load_configuration();

        char temp_wifi[64] = {0};
        char temp_mqtt[64] = {0};

        cJSON *wifi_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(state->config, "wifi"), "password");
        cJSON *mqtt_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(state->config, "mqtt"), "password");

        if (wifi_pw)
        {
            strncpy(temp_wifi, cJSON_GetStringValue(wifi_pw), 63);
            cJSON_SetValuestring(wifi_pw, "*****");
        }
        if (mqtt_pw)
        {
            strncpy(temp_mqtt, cJSON_GetStringValue(mqtt_pw), 63);
            cJSON_SetValuestring(mqtt_pw, "*****");
        }

        if (cJSON_PrintPreallocated(state->config, buffer, sizeof(buffer), 0))
        {
            printf("Serving settings(%d bytes)\n", strlen(buffer));
            snprintf(header, sizeof(header), HEADER_HTML, strlen(buffer));
            tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
            tcp_write(tpcb, buffer, strlen(buffer), TCP_WRITE_FLAG_COPY);
            tcp_output(tpcb);
        }
        else
        {
            printf("Failed Serving settings\n");
        }

        if (wifi_pw)
            cJSON_SetValuestring(wifi_pw, temp_wifi);
        if (mqtt_pw)
            cJSON_SetValuestring(mqtt_pw, temp_mqtt);

        pbuf_free(p);
        return ERR_OK;
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

    // Config page (Chunked)
    else if (strncmp(req, "GET /config", 11) == 0 || is_provisioning)
    {
        printf("Serving config page\n");
        snprintf(header, sizeof(header), HEADER_HTML_GZIP, config_embed_gz_len);

        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
        state->file_data = config_embed_gz;
        state->total_len = config_embed_gz_len;
        state->bytes_sent = 0;

        tcp_sent(tpcb, http_sent_cb);
        send_next_chunk(tpcb, state);

        pbuf_free(p);
        return ERR_OK;
    }

    // toggle pin
    else if (strncmp(req, "GET /pio=", 9) == 0)
    {
        int pin = atoi(req + 9);
        printf("Toggling %d\n", pin);
        bool bit = toggle_pin(pin);
        char resp[30];
        snprintf(resp, sizeof(resp), "{\"status\":%d,\"pio\":%d}", get_pin(pin), pin);
        snprintf(header, sizeof(header), HEADER_JSON, strlen(resp));
        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        pbuf_free(p);
        return ERR_OK;
    }

    // ignore favicon / addchannel
    else if (strncmp(req, "GET /favicon.ico", 16) == 0 || strncmp(req, "GET /addchannel", 15) == 0)
    {
        pbuf_free(p);
        return ERR_OK;
    }

    // Tailwind CSS (Chunked)
    else if (strncmp(req, "GET /tailwind.min.js.gz", 23) == 0)
    {
        printf("Serving tailwind.min.js\n");
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/javascript\r\n"
                 "Content-Encoding: gzip\r\n"
                 "Cache-Control: max-age=31536000\r\n" // Improved cache
                 "Connection: close\r\n"
                 "Content-Length: %d\r\n\r\n",
                 tailwind_min_embed_gz_len);

        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
        state->file_data = tailwind_min_embed_gz;
        state->total_len = tailwind_min_embed_gz_len;
        state->bytes_sent = 0;

        tcp_sent(tpcb, http_sent_cb);
        send_next_chunk(tpcb, state);

        pbuf_free(p);
        return ERR_OK;
    }

    // Alpine.js (Chunked)
    else if (strncmp(req, "GET /alpine.min.js.gz", 21) == 0)
    {
        printf("Serving alpine.min.js\n");
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/javascript\r\n"
                 "Content-Encoding: gzip\r\n"
                 "Cache-Control: max-age=31536000\r\n" // Improved cache
                 "Connection: close\r\n"
                 "Content-Length: %d\r\n\r\n",
                 alpine_min_embed_gz_len);

        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
        state->file_data = alpine_min_embed_gz;
        state->total_len = alpine_min_embed_gz_len;
        state->bytes_sent = 0;

        tcp_sent(tpcb, http_sent_cb);
        send_next_chunk(tpcb, state);

        pbuf_free(p);
        return ERR_OK;
    }

    // chart.js (Chunked)
    else if (strncmp(req, "GET /chart.js.gz", 16) == 0)
    {
        printf("Serving chart.js\n");
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/javascript\r\n"
                 "Content-Encoding: gzip\r\n"
                 "Cache-Control: max-age=31536000\r\n" // Improved cache
                 "Connection: close\r\n"
                 "Content-Length: %d\r\n\r\n",
                 chart_embed_gz_len);

        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
        state->file_data = chart_embed_gz;
        state->total_len = chart_embed_gz_len;
        state->bytes_sent = 0;

        tcp_sent(tpcb, http_sent_cb);
        send_next_chunk(tpcb, state);

        pbuf_free(p);
        return ERR_OK;
    }

    // Dashboard (Chunked - default)
    printf("Serving dashboard page\n");
    snprintf(header, sizeof(header), HEADER_HTML_GZIP, dashboard_embed_gz_len);

    tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    state->file_data = dashboard_embed_gz;
    state->total_len = dashboard_embed_gz_len;
    state->bytes_sent = 0;

    tcp_sent(tpcb, http_sent_cb);
    send_next_chunk(tpcb, state);

    pbuf_free(p);
    return ERR_OK;
}

static err_t http_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    // Allocate a state for this specific connection
    http_state_t *state = calloc(1, sizeof(http_state_t));
    if (!state)
    {
        return ERR_MEM; // Let lwIP gracefully refuse if out of heap
    }

    state->config = (cJSON *)arg; // Pass the global config down

    tcp_arg(newpcb, state);
    tcp_err(newpcb, http_err_cb);
    tcp_recv(newpcb, http_recv_cb);
    return ERR_OK;
}

void start_webserver(cJSON *config)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
        return;

    // Bind the global config argument to the listening PCB
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