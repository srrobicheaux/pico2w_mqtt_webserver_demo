// --- webserver.h ---
#ifndef WEBSERVER_H
#define WEBSERVER_H

typedef struct
{
    cJSON *config;
    bool sse;
    struct tcp_pcb *tpcb;

    const uint8_t *file_data;
    size_t total_len;
    size_t bytes_sent;
     void *next;
     void *previous;
} http_state_t;


void start_webserver(cJSON *config);
void webserver_send_sse_update(char *payload);
void webserver_update_config(cJSON *new_config);

#endif /* WEBSERVER_H */
