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
http_state_t initial_state;

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

static void http_close_clean(http_state_t *current, bool from_err_cb)
{
    if (!current)
        return;
    http_state_t *previous = current->previous;
    http_state_t *next = current->next;

    // 1. Unlink node from doubly-linked list
    if (previous)
    {
        previous->next = next;
    }
    if (next)
    {
        next->previous = previous;
    }

    // 2. Clear lwIP callbacks and close PCB (only if NOT called from err_cb)
    struct tcp_pcb *tpcb = current->tpcb;
    if (tpcb && !from_err_cb)
    {
        tcp_arg(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_err(tpcb, NULL);
        tcp_sent(tpcb, NULL);
        tcp_close(tpcb);
    }

    // 3. Free node (never attempt to free static initial_state head)
    if (current != &initial_state)
    {
        free(current);
    }
}

// Cleans up state memory if connection is aborted (e.g. RST from client)
static void http_err_cb(void *arg, err_t err)
{
    if (arg)
    {
        http_state_t *state = (http_state_t *)arg;
        // Pass true so tcp_close is NOT called on an already-freed PCB
        http_close_clean(state, true);
    }
}

// ==================== HTTP SERVER ====================

static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    static char buffer[FLASH_SETTING_SIZE]; // testing to see if I can reuse the receive buffer to flash disk
    char header[256];
    static bool posting = false;
    static int contentLength = 0;
    static char *buffer_ptr;

    http_state_t *state = (http_state_t *)arg;
    bool is_provisioning = false;

    // Client closed connection
    if (!p)
    {
        http_close_clean(state, false);
        return ERR_OK;
    }

    // Processing error
    if (err != ERR_OK || !state)
    {
        http_close_clean(state, false);
        if (p)
            pbuf_free(p);
        return ERR_OK;
    }

    // maybe this should be p->len
    // tcp_recved(tpcb, p->len);
    tcp_recved(tpcb, p->tot_len);

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
    if (strncmp(req, "POST ", 5) == 0 || posting)
    {
        if (!posting)
        {
            posting = true;
            buffer_ptr = buffer;
            contentLength = atoi(strstr(req, "Content-Length: ") + 16);
            if (contentLength + strstr(req, "\r\n\r\n") + 4 - req > sizeof(buffer))
            {
                printf("Buffer Overflow detected %d > %d\n%20s\n", contentLength + strstr(req, "\r\n\r\n") + 4 - req, sizeof(buffer), req);
                posting = false;
                pbuf_free(p);
                return ERR_MEM;
            }
        }
        memcpy(buffer_ptr, p->payload, p->tot_len);
        buffer_ptr[p->tot_len] = '\0';
        buffer_ptr += p->tot_len;

        if (strlen(strstr(buffer, "\r\n\r\n") + 4) < contentLength)
        {
            pbuf_free(p);
            return ERR_OK;
        }
        posting = false; // Reset for next request
        buffer_ptr = strstr(buffer, "\r\n\r\n") + 4;

        req = buffer; // Redirect to the full buffer for processing
                      //        printf("Request: (%d bytes)\n#%s#\n", strlen(req), req);
        printf("Payload: (%d bytes of %d)\n#", strlen(buffer_ptr), contentLength);
    }

    // restart
    if (strncmp(req, "POST /restart", 13) == 0)
    {
        webserver_send_sse_update("data: {\"MESSAGE\":\"Reboot requested!}\n\n");

        printf("Rebooting from request\n");
        const char *resp = "{\"MESSAGE\":\"Reboot requested!}";
        snprintf(header, sizeof(header), HEADER_JSON, strlen(resp));
        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        watchdog_enable(30, 1);
    }

    // save config
    else if (strncmp(req, "POST /save", 10) == 0)
    {
        const char *resp = "";
        int error = load_flash_buffer(buffer_ptr, contentLength);

        if (error)
        {
            printf("Parse error near position %d\n", error);
            resp = "data:  {\"MESSAGE\":\"Failed to parse settings.\"}\n\n";
        }
        else
        {
            resp = "data:  {\"MESSAGE\":\"Save Requested.\"}\n\n";
        }
        snprintf(header, sizeof(header), HEADER_JSON, strlen(resp));
        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
    }

    // Captive portal redirects
    else if (strstr(req, "captive.apple.com") || strstr(req, "hotspot-detect.html") || strstr(req, "connectivity-check") || strstr(req, "connectivitycheck") || strstr(req, "generate_204"))
    {
        printf("connectivity check\n");
        tcp_write(tpcb, HEADER_REDIRECT, strlen(HEADER_REDIRECT), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
    }

    // Settings
    else if (strncmp(req, "GET /settings", 11) == 0)
    {
        // if (state && !state->config)
        //     state->config = load_configuration();

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
            tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);

            state->file_data = buffer;
            state->total_len = strlen(buffer);
            state->bytes_sent = 0;

            tcp_sent(tpcb, http_sent_cb);
            send_next_chunk(tpcb, state);

            //tcp_write(tpcb, buffer, strlen(buffer), TCP_WRITE_FLAG_COPY);
            //tcp_output(tpcb);
        }
        else
        {
            printf("Failed Serving settings\n");
        }

        if (wifi_pw)
            cJSON_SetValuestring(wifi_pw, temp_wifi);
        if (mqtt_pw)
            cJSON_SetValuestring(mqtt_pw, temp_mqtt);
    }

    // start SSE Events
    else if (strncmp(req, "GET /events", 11) == 0)
    {
        printf("Starting SSE Events\n");
        state->sse = true;
        state->tpcb = tpcb;
        tcp_write(tpcb, HEADER_SSE, strlen(HEADER_SSE), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
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
    }

    // You will now parse the literal array index in the JSON:
    else if (strncmp((char *)p->payload, "GET /channel/", 13) == 0)
    {
        char digits[4];
        snprintf(digits,4,"%s",p->payload+13);
        char *resp = NULL;

        int index = atoi(digits);
        if (index >= 0 && index < cJSON_GetArraySize(cJSON_GetObjectItem(state->config, "channels")))
        {
            cJSON *channel = cJSON_GetArrayItem(cJSON_GetObjectItem(state->config, "channels"), index);
            if (channel)
            {
                cJSON *tg = cJSON_GetObjectItem(channel, "toggle");
                if (!tg)
                {
                    tg = cJSON_AddBoolToObject(channel, "toggle", 1);
                }
                else
                {
                    cJSON_SetBoolValue(tg, 1);
                }

                printf("Requesting channel %d toggle\n", index);
                resp = "{\"success\": 1, \"MESSAGE\": \"Channel toggle requested.\"}\n\n";
            }
            else
            {
                resp = "{\"success\": 0, \"MESSAGE\": \"Invalid channel index.\"}\n\n";
            }
        }
        else {
            resp = "{\"success\": 0, \"MESSAGE\": \"Invalid channel index.\"}\n\n";
        }
        snprintf(header, sizeof(header), HEADER_JSON, strlen(resp));
        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        tcp_write(tpcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
    }

    // ignore favicon
    else if (strncmp(req, "GET /favicon.ico", 16) == 0 || strncmp(req, "GET /addchannel", 15) == 0)
    {
    }

    // Tailwind CSS (Chunked)
    else if (strncmp(req, "GET /tailwind.min.js", 20) == 0)
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
    }

    // Alpine.js (Chunked)
    else if (strncmp(req, "GET /alpine.min.js", 18) == 0)
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
    }

    // chart.js (Chunked)
    else if (strncmp(req, "GET /chart.js", 13) == 0)
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
    }

    // dashboard (Chunked)
    else if (strncmp(req, "GET / ", 6) == 0)
    {
        // Dashboard (Chunked - default)
        printf("Serving dashboard page\n");
        snprintf(header, sizeof(header), HEADER_HTML_GZIP, dashboard_embed_gz_len);

        tcp_write(tpcb, header, strlen(header), TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
        state->file_data = dashboard_embed_gz;
        state->total_len = dashboard_embed_gz_len;
        state->bytes_sent = 0;

        tcp_sent(tpcb, http_sent_cb);
        send_next_chunk(tpcb, state);
    }

    pbuf_free(p);
    return ERR_OK;
}

// ==================== HTTP ACCEPT & SSE DISPATCH ====================

static err_t http_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    http_state_t *head = (http_state_t *)arg;
    if (!head || !newpcb)
        return ERR_VAL;

    // Allocate state for the new connection
    http_state_t *node = calloc(1, sizeof(http_state_t));
    if (!node)
    {
        return ERR_MEM; // Refuse connection gracefully if out of heap
    }

    // Initialize node properties
    node->config = head->config;
    node->tpcb = newpcb;
    node->sse = false;

    // Traverse to the end of the doubly-linked list
    http_state_t *tail = head;
    while (tail->next != NULL)
    {
        tail = tail->next;
    }

    // Append new node to the list
    tail->next = node;
    node->previous = tail;

    // Register lwIP handlers for this client
    tcp_arg(newpcb, node);
    tcp_err(newpcb, http_err_cb);
    tcp_recv(newpcb, http_recv_cb);

    return ERR_OK;
}

static cJSON *s_webserver_config = NULL;

void webserver_update_config(cJSON *new_config)
{
    s_webserver_config = new_config;
}

void start_webserver(cJSON *config)
{
    s_webserver_config = config;

    struct tcp_pcb *pcb = tcp_new();

    if (!pcb)
        return;
    initial_state.config = config;
    initial_state.next = NULL;
    initial_state.previous = NULL;
    initial_state.tpcb = NULL;

    // Bind the global config argument to the listening PCB
    tcp_arg(pcb, &initial_state);
    tcp_bind(pcb, IP_ADDR_ANY, PORT);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, http_accept_cb);

    printf("http://%s:80\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
}

// Iterates through active connections and pushes SSE payload
void _webserver_send_sse_updates(char *payload, http_state_t *state)
{
    http_state_t *curr = state;
    while (curr != NULL)
    {
        // Ensure connection is actively registered for SSE and has a valid PCB
        if (curr->sse && curr->tpcb != NULL)
        {
            tcp_write(curr->tpcb, payload, strlen(payload), TCP_WRITE_FLAG_COPY);
            tcp_output(curr->tpcb);
        }
        curr = curr->next;
    }
}

void webserver_send_sse_update(char *payload)
{
    _webserver_send_sse_updates(payload, &initial_state);
}
