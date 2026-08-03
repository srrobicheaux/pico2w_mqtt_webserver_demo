#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "lwip/apps/mqtt.h"
#include "cJSON.h"

#ifndef MQTT_TOPIC_LEN
#define MQTT_TOPIC_LEN 128
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

typedef struct {
    mqtt_client_t *mqtt_client_inst;
    struct mqtt_connect_client_info_t mqtt_client_info;
    ip_addr_t mqtt_server_address;
    cJSON *config_root;
    mqtt_connection_status_t status;
    bool is_connecting;         // Tracks active lookups/handshakes
    char topic[128];
    char data[256];
} MQTT_CLIENT_DATA_T;

bool mqtt_manager_init(MQTT_CLIENT_DATA_T *_state, cJSON *config);
bool mqtt_manager_start(MQTT_CLIENT_DATA_T *state);
void mqtt_manager_publish_raw(MQTT_CLIENT_DATA_T *_state, const char *topic, const char *payload, bool retain);
void mqtt_manager_publish_state(MQTT_CLIENT_DATA_T *_state);

#endif // MQTT_MANAGER_H