#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "lwip/apps/mqtt.h"
#include "wifi_provisioning.h"
#include "system_info.h"
#include "IOs.h"

#ifndef MQTT_TOPIC_LEN
#define MQTT_TOPIC_LEN 100
#endif

typedef struct {
    char model[12];        // e.g., "Pico 2W"
    char version[6];      // e.g., "1.2.0"

    char server[32];
    char user[32];
    char password[64];
} mqtt_settings_t;


typedef struct {
    mqtt_client_t* mqtt_client_inst;
    struct mqtt_connect_client_info_t mqtt_client_info;
    char data[MQTT_OUTPUT_RINGBUF_SIZE];
    char topic[MQTT_TOPIC_LEN];
    uint32_t len;
    ip_addr_t mqtt_server_address;
    bool connect_done;
    int subscribe_count;
    bool stop_client;
    mqtt_connection_status_t status;
} MQTT_CLIENT_DATA_T;

bool mqtt_init(wifi_t *_wifi_settings, mqtt_settings_t *_mqtt_settings, MQTT_CLIENT_DATA_T * _state);

// Change start to return void, we don't need to leak the state pointer anymore
void mqtt_manager_start(MQTT_CLIENT_DATA_T *state, mqtt_settings_t *mqtt_settings);

// Change publish to drop the state argument (we renamed it to standard naming too)
void mqtt_manager_publish(MQTT_CLIENT_DATA_T *state, const char *topic, const char *data);
#endif
