#ifndef MQTT_MANAGER
#define MQTT_MANAGER

//#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "pico/cyw43_arch.h"
#include "mqtt_manager.h"
#include "system_info.h"
#include "secrets.h"

MQTT_CLIENT_DATA_T *state;

typedef struct {
    const char topic[128];    // e.g., Flash ID or "BATMON_01"
    const char payload[256];  // e.g., "Batmon Pro"
} MQTT_publish_t;

mqtt_settings_t *mqtt_settings;
char * device_id;
char * network_name;

// --- Helper: Safe Topic Builder ---
static char *build_topic(const char *suffix, char *out_buf, size_t max_len)
{
    snprintf(out_buf, max_len, "%s%s", device_id, suffix);
    return out_buf;
}

static void pub_request_cb(void *arg, err_t err)
{
    if (err != ERR_OK)
    {
        printf("MQTT Publish Callback Error: %d\n", err);
    }
}

// --- Hardware Abstraction ---
static void handle_gpio_command(int pin, char *data)
{
    printf("GPIO Action: Pin %d -> %s\n", pin, data);

    bool read =true;
    bool set_high =false;

    if (lwip_stricmp(data, "on") == 0 || strcmp(data, "1") == 0) {
        read = false;
        set_high = true;
    }
    if (lwip_stricmp(data, "off") == 0 || strcmp(data, "0") == 0) {
        read = false;
        set_high = false;
    }

    if(!read){
        if (pin == CYW43_WL_GPIO_LED_PIN)
        {
            cyw43_arch_gpio_put(pin, set_high);
        }
        else
        {
            gpio_set_dir(pin, GPIO_OUT);
            gpio_put(pin, set_high);
        }
    }


    char topic_buf[128];
    snprintf(topic_buf, sizeof(topic_buf), "/pin/%d/status", pin);

    char msg[8];
    snprintf(msg, sizeof(msg), "%s", gpio_get(pin) ? "on" : "off");

    build_topic(topic_buf, topic_buf, sizeof(topic_buf));
    // Note: We don't use begin/end here if we are already inside a callback (LwIP is already locked)
    mqtt_manager_publish(topic_buf, msg);
}

void _mqtt_manager_publish(const char *topic, const char *payload, bool retain, bool raw_topic)
{
    if (!state->connect_done || !state->mqtt_client_inst)
        return;

    char full_topic[128];
    // BUG WAS HERE: You only built the topic if retain was true.
    // It must be built every time.
    if (!raw_topic)
    {
        build_topic(topic, full_topic, sizeof(full_topic));
    }
    else
    {
        strncpy(full_topic, topic, sizeof(full_topic) - 1);
        full_topic[sizeof(full_topic) - 1] = '\0';
    }

    cyw43_arch_lwip_begin();
    // Use QoS 0 or 1 based on your needs; 0 is usually fine for sensors
    err_t err = mqtt_publish(state->mqtt_client_inst, full_topic, payload, strlen(payload), 0, retain, pub_request_cb, state);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("Failed to queue publish: %d\n", err);
    }
}

void mqtt_manager_publish(const char *topic, const char *payload){
    _mqtt_manager_publish(topic, payload, false, false);
}

static void sub_unsub_topics(MQTT_CLIENT_DATA_T *_state, bool sub)
{
    char topic_buf[128];
    
    // Subscribe to GPIO commands
    build_topic("/pin/#", topic_buf, sizeof(topic_buf));
    mqtt_sub_unsub(_state->mqtt_client_inst, topic_buf, 0, NULL, _state, sub);

    // Subscribe to a restart command
    build_topic("/restart", topic_buf, sizeof(topic_buf));
    mqtt_sub_unsub(_state->mqtt_client_inst, topic_buf, 0, NULL, _state, sub);

    // Subscribe to unline request
    build_topic("/online", topic_buf, sizeof(topic_buf));
    mqtt_sub_unsub(_state->mqtt_client_inst, topic_buf, 0, NULL, _state, sub);

    // Listen for Home Assistant's own status (so we can re-announce ourselves)
    mqtt_sub_unsub(_state->mqtt_client_inst, "homeassistant/status", 0, NULL, _state, sub);
}


static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    strncpy(state->topic, topic, sizeof(state->topic) - 1);
}
      
static void _get_dev_block(char *buf, size_t len) {
snprintf(buf, len, 
    "\"dev\": {\"ids\":[\"%s\"],\"mdl\":\"%s\",\"sw\":\"%s\",\"name\": \"%s\","
    "\"configuration_url\": \"http://%s/config\"}",
    device_id,mqtt_settings->model, mqtt_settings->version,network_name,mqtt_settings->server,device_id);
}

void ha_add_info(char *value, char *class, char *Units, char *dev_json) {
    char topic[128];
    char payload[1024];
    char name[32];    

    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s_sensor_%s/config", device_id,device_id, value);
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
      "\"expire_after\": 120,"
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
    //printf("Publishing HA Discovery for %s:\nTopic: %s\nPayload: %s\n", value, topic, payload);
    // Use the wrapper we discussed earlier to send to the raw topic
    _mqtt_manager_publish(topic, payload, true, true);
}


void ha_add_sensor(int pin,char *dev_json) {
    char topic[128];
    char payload[1024];
    char name[32];    

    // Needs to be: homeassistant/switch/<device_id>/<object_id>/config
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s_sensor_%d/config", device_id,device_id, pin);
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
      "\"expire_after\": 120,"
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

    //printf("Publishing HA Discovery for Pin %d:\nTopic: %s\nPayload: %s\n", pin, topic, payload);
    // Use the wrapper we discussed earlier to send to the raw topic

    _mqtt_manager_publish(topic, payload, true, true);
}

// Updated to use 3 parameters, consistent with your ha_publish_discovery call
void ha_add_switch(int pin,char *dev_json) {
    char topic[128];
    char payload[1024];
    // Needs to be: homeassistant/switch/<device_id>/<object_id>/config
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s/%s_switch_%d/config", device_id,device_id, pin);
    snprintf(payload, sizeof(payload),
    "{"
      "\"platform\": \"switch\","
      "\"name\": \"Pin %d\","
      "\"device_class\": \"switch\","
      "\"command_topic\": \"%s/pin/%d\","
      "\"payload_off\": \"OFF\","
      "\"payload_on\": \"ON\","
      "\"state_topic\": \"%s/pin/%d\","
      "\"state_off\": \"OFF\","
      "\"state_on\": \"ON\","
      "\"unique_id\": \"%s_switch_%d\","
      "\"availability_topic\": \"%s/status\","
      "\"payload_available\": \"online\","
      "\"payload_not_available\": \"offline\","
      "\"qos\": 0.0,"
    "%s }",
    pin, 
    device_id, pin, 
    device_id, pin,
    device_id, pin, 
    device_id, 
    dev_json);

    if (pin == -99) {
        char * pos = strstr(payload, "Pin -99");
        memcpy(pos,"Restart",7);
        pos = strstr( payload,"/pin/-99");
        memcpy(pos,"/restart",8);
        //printf ("Publishing HA Discovery for Restart Command:\nTopic: %s\nPayload: %s\n", topic, payload);
    }
    //printf("Publishing HA Discovery for Pin %d:\nTopic: %s\nPayload: %s\n", pin, topic, payload);
    // Use the wrapper we discussed earlier to send to the raw topic

    _mqtt_manager_publish(topic, payload, true, true);
}

static void ha_publish_discovery(char *data){
    printf("Processing HA Discovery. Status: %s\n", data);
    char dev_json[256];
    _get_dev_block(dev_json, sizeof(dev_json));
    //reused switch for pico resetting becuase almost identical payload and it simplifies the logic a lot.
    ha_add_switch(-99,dev_json); 


    ha_add_info("temperature", "temperature", "°F", dev_json) ;
    ha_add_info("uptime", "duration", "s", dev_json) ;

    for (int i = 0; i < 3; i++) {
   //     IO_settings.analogs[i].name
//        ha_add_sensor(i,dev_json, "voltage", "V"); 
        ha_add_sensor(i,dev_json); 
                cyw43_arch_poll(); // This is required in poll mode

    }
    for (int i = 5; i < 15; i++) {
        ha_add_switch(i,dev_json); 
                cyw43_arch_poll(); // This is required in poll mode
    }

    mqtt_manager_publish("/status","online");
    cyw43_arch_poll(); // This is required in poll mode
    cyw43_arch_poll(); // This is required in poll mode
    cyw43_arch_poll(); // This is required in poll mode
    cyw43_arch_poll(); // This is required in poll mode
    mqtt_manager_publish("/status","online");

}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    size_t safe_len = (len < sizeof(state->data) - 1) ? len : sizeof(state->data) - 1;
    memcpy(state->data, data, safe_len);
    state->data[safe_len] = '\0';

    printf("Inbound -> Topic: %s | Data: %s\n", state->topic, state->data);

    // Topic parsing: find "/pin/"
    const char *pin_ptr = strstr(state->topic, "/pin/");
    if (pin_ptr)
    {
        int pin_num;
        if (sscanf(pin_ptr, "/pin/%d", &pin_num) == 1)
        {
            if (sscanf(pin_ptr, "/status", &pin_num) == 1){
                strcpy(state->data,"read");                
            }
            handle_gpio_command(pin_num, state->data);
        }
    }
    else if (strstr(state->topic, "/restart"))
    {
        reset();
    }
    else if (strstr(state->topic, "homeassistant/status") != 0)
    {
        printf("Homeassistant is %s. ", state->data);
        ha_publish_discovery(state->data);
    }
    else if (strstr(state->topic, "/online") || strstr(state->topic, "/status"))
    {
        mqtt_manager_publish("/status","online");
    }
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{

    if (status == MQTT_CONNECT_ACCEPTED)
    {
        printf("MQTT Connected and Accepted!\n");
        state->connect_done = true;
        sub_unsub_topics(state, true);
        mqtt_manager_publish("/status","online");
    }
    else
    {
        printf("MQTT Connection Refused/Disconnected: %d\n", status);
        state->connect_done = false;
        sub_unsub_topics(state, false);
        panic_unsupported();
    }
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
        printf("MQTT connect launch failed: %d\n", err);
}

static void dns_found_cb(const char *hostname, const ip_addr_t *ipaddr, void *arg)
{
    if (ipaddr)
    {
        state->mqtt_server_address = *ipaddr;
        start_client(state);
    }
    else
    {
        printf("DNS Lookup failed for broker\n");
    }
}

void mqtt_manager_start()
{
    static bool started = false;
    if (started && !state->connect_done)        return;
    started = true;
    if (state->mqtt_client_inst)
    {
        printf("MQTT Client already running. Restarting with new settings (but disconnect is wankie!)...\n");
        sub_unsub_topics(state, false); // Unsubscribe from old topics
        //        mqtt_client_disconnect(state->mqtt_client_inst);
        mqtt_client_free(state->mqtt_client_inst);
//        memset(state->mqtt_client_info, 0, sizeof(state->mqtt_client_info));
    }

    printf("Starting MQTT Manager Broker: %s, Node ID: %s, Object ID: %s\t", mqtt_settings->server, network_name,device_id);
    state->mqtt_client_info.client_id = device_id;
    state->mqtt_client_info.keep_alive = 60;
    state->mqtt_client_info.client_user = mqtt_settings->user;
    state->mqtt_client_info.client_pass = mqtt_settings->password;

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(mqtt_settings->server, &state->mqtt_server_address, dns_found_cb, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK)
        start_client(state);
}

bool mqtt_manager_is_connected(void)
{
    return state->connect_done;
}

int mqtt_settings_JSON(char *payload, size_t len)
{
    return snprintf(payload, len, 
                   "\"mqtt\":{\"model\":\"%s\",\"version\":\"%s\",\"server\":\"%s\",\"user\":\"%s\"},",
                   mqtt_settings->model, mqtt_settings->version, mqtt_settings->server,mqtt_settings->user);
}

bool mqtt_init(wifi_t *_wifi_settings, mqtt_settings_t *_mqtt_settings, MQTT_CLIENT_DATA_T *_state)
{
    state = _state;
     state->connect_done = false;
     device_id = _wifi_settings->device_id;
     network_name = _wifi_settings->network_name;
     mqtt_settings = _mqtt_settings;

    strncpy(_mqtt_settings->model, "Pico 2W", 32);
    strncpy(_mqtt_settings->version, "1.0.4", 32);
    strncpy(_mqtt_settings->server, "rpi4b.lan", 32);
    strncpy(_mqtt_settings->user, MQTT_USER, 32);
    strncpy(_mqtt_settings->password, MQTT_PW, 64);

     return true;
}

#endif