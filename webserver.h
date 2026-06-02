// --- webserver.h ---
#ifndef WEBSERVER_H
#define WEBSERVER_H
void start_webserver(cJSON *config);
void webserver_send_sse_update(cJSON *channels);

#endif /* WEBSERVER_H */
