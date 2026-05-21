#ifndef MQTT_MANAGER
#define MQTT_MANAGER

// #include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "pico/cyw43_arch.h"
#include "mqtt_manager.h"
#include "system_info.h"
#include "secrets.h"
extern io_t *IOs;

typedef struct
{
    const char topic[128];   // e.g., Flash ID or "BATMON_01"
    const char payload[256]; // e.g., "Batmon Pro"
} MQTT_publish_t;

// mqtt_settings_t *mqtt_settings;
char *device_id;
char *network_name;
char dev_json[256];

static void pub_request_cb(void *arg, err_t err)
{
    if (err != ERR_OK)
    {
        printf("MQTT Publish Callback Error: %d\n", err);
        printf("free new attempt todo\n");
    }
}

// --- Hardware Abstraction ---
static void handle_gpio_command(MQTT_CLIENT_DATA_T *_state, int pin, char *data)
{
    printf("GPIO Action: Pin %d -> %s\n", pin, data);

    bool is_input = true;
    bool set_high = false;

    if (lwip_stricmp(data, "on") == 0 || strcmp(data, "1") == 0)
    {
        is_input = false;
        set_high = true;
    }
    if (lwip_stricmp(data, "off") == 0 || strcmp(data, "0") == 0)
    {
        is_input = false;
        set_high = false;
    }

    if (!is_input)
    {
        if (pin < 4)
        {
            cyw43_arch_gpio_put(pin, set_high);
        }
        else
        {
            gpio_set_dir(pin, set_high);
            ;
            gpio_put(pin, set_high);
        }
    }

    char topic_buf[128];
    snprintf(topic_buf, sizeof(topic_buf), "%s/pin/%d/status", device_id, pin);

    char msg[8];
    snprintf(msg, sizeof(msg), "%s", gpio_get(pin) ? "on" : "off");
    mqtt_manager_publish(_state, topic_buf, msg);
}

void _mqtt_manager_publish(MQTT_CLIENT_DATA_T *_state, const char *topic, const char *payload, bool retain, bool raw_topic)
{
    if (!_state->connect_done || !_state->mqtt_client_inst)
        return;

    char full_topic[128];

    if (!raw_topic)
    {
        snprintf(full_topic, sizeof(full_topic), "%s%s", device_id, topic);
    }
    else
    {
        strncpy(full_topic, topic, sizeof(full_topic) - 1);
        full_topic[sizeof(full_topic) - 1] = '\0';
    }

    cyw43_arch_lwip_begin();
    // Use QoS 0 or 1 based on your needs; 0 is usually fine for sensors
    err_t err = mqtt_publish(_state->mqtt_client_inst, full_topic, payload, strlen(payload), 0, retain, pub_request_cb, _state);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        printf("Failed to queue publish: %d\n", err);
    }
}

void mqtt_manager_publish(MQTT_CLIENT_DATA_T *_state, const char *topic, const char *payload)
{
    if (_state->connect_done)
        _mqtt_manager_publish(_state, topic, payload, false, false);
}

static void sub_unsub_topics(MQTT_CLIENT_DATA_T *_state, bool sub)
{
    char topic_buf[128];
    // Listen for Home Assistant's own status (so we can re-announce ourselves)
    mqtt_sub_unsub(_state->mqtt_client_inst, "homeassistant/status", 0, NULL, _state, sub);

    // Subscribe to GPIO commands
    snprintf(topic_buf, sizeof(topic_buf), "%s/pin/#", device_id);
    mqtt_sub_unsub(_state->mqtt_client_inst, topic_buf, 0, NULL, _state, sub);

    // Subscribe to GPIO commands
//    snprintf(topic_buf, sizeof(topic_buf), "%s/IO", device_id);
    //    mqtt_sub_unsub(_state->mqtt_client_inst, topic_buf, 0, NULL, _state, sub);

    // Subscribe to a restart command
    snprintf(topic_buf, sizeof(topic_buf), "%s/restart", device_id);
    mqtt_sub_unsub(_state->mqtt_client_inst, topic_buf, 0, NULL, _state, sub);

    // Subscribe to unline request
    snprintf(topic_buf, sizeof(topic_buf), "%s/online", device_id);
    mqtt_sub_unsub(_state->mqtt_client_inst, topic_buf, 0, NULL, _state, sub);
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;
    strncpy(_state->topic, topic, sizeof(_state->topic) - 1);
}

void ha_add_info(MQTT_CLIENT_DATA_T *_state, char *value, char *class, char *Units)
{
    char topic[128];
    char payload[1024];
    char name[32];

    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s_sensor_%s/config", device_id, device_id, value);
    snprintf(payload, sizeof(payload),
             "{"
             "\"platform\": \"sensor\","
             "\"name\": \"System %s\","
             "\"device_class\": \"%s\","
             "\"state_class\": \"measurement\","
             "\"unit_of_measurement\": \"%s\","
             "\"state_topic\": \"%s/system_status\","
             "\"suggested_display_precision\": 2,"
             "\"value_template\": \"{{value_json.%s}}\","
             "\"expire_after\": 10,"
             "\"unique_id\": \"%s_analog_%s\","
             "\"availability_topic\": \"%s/status\","
             "\"payload_available\": \"online\","
             "\"payload_not_available\": \"offline\","
             "\"qos\": 0.0,"
             "%s }",
             value,
             class,
             Units,
             device_id,
             value,
             device_id, value,
             device_id,
             dev_json);
    // printf("Publishing HA Discovery for %s:\nTopic: %s\nPayload: %s\n", value, topic, payload);
    //  Use the wrapper we discussed earlier to send to the raw topic
    _mqtt_manager_publish(_state, topic, payload, true, true);
}

void ha_add_sensor(MQTT_CLIENT_DATA_T *_state, int pin)
{
    char topic[128];
    char payload[1024];
    char name[32];

    // Needs to be: homeassistant/switch/<device_id>/<object_id>/config
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s_sensor_%d/config", device_id, device_id, pin);
    snprintf(payload, sizeof(payload),
             "{"
             "\"platform\": \"sensor\","
             "\"name\": \"Analog %d\","
             "\"device_class\": \"voltage\","
             "\"state_class\": \"measurement\","
             "\"unit_of_measurement\": \"V\","
             "\"state_topic\": \"%s/IO\","
             "\"suggested_display_precision\": 2,"
             "\"value_template\": \"{{value_json.analog[%d]}}\","
             "\"expire_after\": 10,"
             "\"unique_id\": \"%s_analog_%d\","
             "\"availability_topic\": \"%s/status\","
             "\"payload_available\": \"online\","
             "\"payload_not_available\": \"offline\","
             "\"qos\": 0.0,"
             "%s }",
             pin,
             device_id,
             pin,
             device_id, pin,
             device_id,
             dev_json);

    // printf("Publishing HA Discovery for Pin %d:\nTopic: %s\nPayload: %s\n", pin, topic, payload);
    //  Use the wrapper we discussed earlier to send to the raw topic

    _mqtt_manager_publish(_state, topic, payload, true, true);
}

// Updated to use 3 parameters, consistent with your ha_publish_discovery call
void ha_add_switch(MQTT_CLIENT_DATA_T *_state, int pin)
{
    char topic[128];
    char payload[1024];
    char name[32] = "Restart";
    // Needs to be: homeassistant/switch/<device_id>/<object_id>/config
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s/%s_switch_%d/config", device_id, device_id, pin);

    if (pin != -99)
    {
        strncpy(name, IOs->pios[pin].name, sizeof(name) - 1);
    }

    // E3C3D7106A914008/pin/15

    snprintf(payload, sizeof(payload),
             "{"
             "\"platform\": \"switch\","
             "\"name\": \"%s\","
             "\"device_class\": \"switch\","

             "\"command_topic\": \"%s/pin/%d\","
             "\"payload_off\": \"OFF\","
             "\"payload_on\": \"ON\","

//             "\"state_topic\": \"%s/IO\","
//             "\"value_template\": \"{{value_json.pio[%d]}}\","

            "\"state_topic\": \"%s/pin/%d\","
             "\"state_off\": \"OFF\","
             "\"state_on\": \"ON\","
             "\"expire_after\": 10,"

             "\"unique_id\": \"%s_switch_%d\","
             "\"availability_topic\": \"%s/status\","
             "\"payload_available\": \"online\","
             "\"payload_not_available\": \"offline\","
             "\"qos\": 0.0,"
             "%s }",
             name,
             device_id, pin,
             device_id, pin,
             device_id, pin,
             device_id,
             dev_json);

    if (pin == -99)
    {
        char *pos = strstr(payload, "/pin/-99");
        memcpy(pos, "/restart", 8);
    }

    // printf("Publishing HA Discovery for Pin %d:\nTopic: %s\nPayload: %s\n", pin, topic, payload);
    // printf("Publishing HA Discovery for Pin %d:\nTopic: %s\nPayload: %s\n", pin, topic, payload);
    //  Use the wrapper we discussed earlier to send to the raw topic

    _mqtt_manager_publish(_state, topic, payload, true, true);
}

static void ha_publish_discovery(MQTT_CLIENT_DATA_T *_state)
{
    printf("Processing HA Discovery. Status: %s\n", _state->data);

    // reused switch for pico resetting becuase almost identical payload and it simplifies the logic a lot.
    ha_add_switch(_state, -99);

    ha_add_info(_state, "temperature", "temperature", "°F");
    ha_add_info(_state, "uptime", "duration", "s");

    for (int i = 0; i < 3; i++)
    {
        //     IO_settings.analogs[i].name
        //        ha_add_sensor(i,dev_json, "voltage", "V");
        ha_add_sensor(_state, i);
        cyw43_arch_poll(); // This is required in poll mode
    }
    for (int i = 10; i < 16; i++)
    {
        ha_add_switch(_state, i);
        cyw43_arch_poll(); // This is required in poll mode
    }
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;
    size_t safe_len = (len < sizeof(_state->data) - 1) ? len : sizeof(_state->data) - 1;
    memcpy(_state->data, data, safe_len);
    _state->data[safe_len] = '\0';
    char topic_buf[128];

    printf("Inbound -> Topic: %s | Data: %s\n", _state->topic, _state->data);

    // Topic parsing: find "/pin/"
    const char *pin_ptr = strstr(_state->topic, "/pin/");
    if (pin_ptr)
    {
        int pin_num;
        if (sscanf(pin_ptr, "/pin/%d", &pin_num) == 1)
        {
            if (sscanf(pin_ptr, "/status", &pin_num) == 1)
            {
                strcpy(_state->data, "is_input");
            }
            handle_gpio_command(_state, pin_num, _state->data);
        }
    }
    else if (strstr(_state->topic, "/restart"))
    {
        reset();
    }
    else if (strstr(_state->topic, "homeassistant/status") != 0)
    {
        printf("Homeassistant is %s. ", _state->data);
        ha_publish_discovery(_state);

        snprintf(topic_buf, sizeof(topic_buf), "%s/status", device_id);
        mqtt_manager_publish(_state, topic_buf, "online");
    }
    else if (strstr(_state->topic, "/online") || strstr(_state->topic, "/status"))
    {
        snprintf(topic_buf, sizeof(topic_buf), "%s/status", device_id);
        mqtt_manager_publish(_state, topic_buf, "online");
    }
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;
    _state->status = status;
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        printf("MQTT Connected and Accepted!\n");
        sub_unsub_topics(_state, true);
        mqtt_manager_publish(_state, "/status", "online");
    }
    else
    {
        printf("MQTT Connection Refused/Disconnected: %d\n", status);
        sub_unsub_topics(_state, false);
        //        panic_unsupported();
    }
    _state->connect_done = true;
}

static void start_client(MQTT_CLIENT_DATA_T *_state)
{
    if (!_state->mqtt_client_inst)
        _state->mqtt_client_inst = mqtt_client_new();

    mqtt_set_inpub_callback(_state->mqtt_client_inst, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, _state);

    cyw43_arch_lwip_begin();
    err_t err = mqtt_client_connect(_state->mqtt_client_inst, &_state->mqtt_server_address, MQTT_PORT,
                                    mqtt_connection_cb, _state, &_state->mqtt_client_info);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        _state->status = err;
        printf("\nMQTT connect launch failed: %d\n", err);
        _state->connect_done = false;
    }
}

static void dns_found_cb(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;
    if (ipaddr)
    {
        _state->mqtt_server_address = *ipaddr;
        start_client(_state);
    }
    else
    {
        printf("DNS Lookup failed for broker\n");
    }
    if (ipaddr)
    {
        _state->mqtt_server_address = *ipaddr;
        start_client(_state);
    }
    else
    {
        printf("DNS Lookup failed for broker\n");
    }
}

void mqtt_manager_start(MQTT_CLIENT_DATA_T *state, mqtt_settings_t *mqtt_settings)
{
    static bool started = false;
    if (MQTT_CONNECT_ACCEPTED == state->status)
        return;

    if (started && !state->connect_done)
        return;

    if (state->status != 4096)
    { // state->mqtt_client_inst) {
        printf("MQTT Client already running. Restarting connection...%d\n", state->status);
        mqtt_unsubscribe(state->mqtt_client_inst, "#", NULL, state);
        // memset(state->mqtt_client_inst, 0, sizeof(state->mqtt_client_inst));

        mqtt_client_free(state->mqtt_client_inst);
        // state->mqtt_client_inst; // Force re-creation of client
        // state->mqtt_client_inst = NULL; // Force re-creation of client
        // reset();
    }
    started = true;

    printf("Starting MQTT Manager Broker: %s, Node ID: %s, Object ID: %s\t", mqtt_settings->server, network_name, device_id);
    state->mqtt_client_info.client_id = device_id;
    state->mqtt_client_info.keep_alive = 60;
    state->mqtt_client_info.client_user = mqtt_settings->user;
    state->mqtt_client_info.client_pass = mqtt_settings->password;

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(mqtt_settings->server, &state->mqtt_server_address, dns_found_cb, state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK)
        start_client(state);
}

bool mqtt_init(wifi_t *_wifi_settings, mqtt_settings_t *_mqtt_settings, MQTT_CLIENT_DATA_T *_state)
{
    _state->connect_done = false;
    device_id = _wifi_settings->device_id;
    network_name = _wifi_settings->network_name;

    snprintf(dev_json, sizeof(dev_json),
             "\"dev\": {\"ids\":[\"%s\"],\"mdl\":\"%s\",\"sw\":\"%s\",\"name\": \"%s\","
             "\"configuration_url\": \"http://%s/config\"}",
             device_id, _mqtt_settings->model, _mqtt_settings->version, network_name, _mqtt_settings->server, device_id);

    return true;
}

#endif