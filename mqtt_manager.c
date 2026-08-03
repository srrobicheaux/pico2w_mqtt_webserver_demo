#include "mqtt_manager.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"
#include "io_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void pub_request_cb(void *arg, err_t err)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;

    if (err != ERR_OK)
    {
        printf("callback topic: %s -> data: %s\n", _state->topic, _state->data);

        printf("MQTT Pub Error: %d\n", err);
    }
}

void mqtt_manager_publish_raw(MQTT_CLIENT_DATA_T *_state, const char *topic, const char *payload, bool retain)
{
    if (!_state || _state->status != MQTT_CONNECT_ACCEPTED || !_state->mqtt_client_inst || !topic || !payload)
        return;

    cyw43_arch_lwip_begin();
    err_t err = mqtt_publish(_state->mqtt_client_inst, topic, payload, strlen(payload), 0, retain, pub_request_cb, _state);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        printf("topic: %s -> payload: %s\n", topic, payload);
        printf("MQTT Queue Fail: %d\n", err);
    }
    else
    {
        //        printf("%s -> %s\n", topic, payload);
    }
}

// Looks inside the nested "ha" block for state topics
static const char *lookup_state_topic(cJSON *channels, int pin, char *fallback_buf, size_t fallback_sz, const char *dev_id)
{
    cJSON *ch = NULL;
    cJSON_ArrayForEach(ch, channels)
    {
        cJSON *pin_obj = cJSON_GetObjectItem(ch, "pin");
        if (pin_obj && pin_obj->valueint == pin)
        {
            cJSON *ha_node = cJSON_GetObjectItem(ch, "ha");
            if (ha_node)
            {
                cJSON *top_obj = cJSON_GetObjectItem(ha_node, "state_topic");
                if (top_obj && top_obj->valuestring)
                {
                    if (strchr(top_obj->valuestring, '%'))
                    {
                        snprintf(fallback_buf, fallback_sz, top_obj->valuestring, dev_id, pin);
                        return fallback_buf;
                    }
                    return top_obj->valuestring;
                }
            }
        }
    }
    snprintf(fallback_buf, fallback_sz, "%s/pin/%d/status", dev_id, pin);
    return fallback_buf;
}

void mqtt_manager_publish_state(MQTT_CLIENT_DATA_T *_state)
{
    if (!_state || _state->status != MQTT_CONNECT_ACCEPTED || !_state->config_root)
        return;

    cJSON *channels = cJSON_GetObjectItem(_state->config_root, "channels");

    cJSON *item = NULL;
    cJSON *dev_id_obj = cJSON_GetObjectItem(_state->config_root, "device_id");
    const char *dev_id = (dev_id_obj && dev_id_obj->valuestring) ? dev_id_obj->valuestring : "batmon";

    cJSON_ArrayForEach(item, channels)
    {
        char topic[128];
        char payload[32];

        cJSON *enabled = cJSON_GetObjectItem(item, "enabled");
        cJSON *pin_obj = cJSON_GetObjectItem(item, "pin");
        cJSON *ha_node = cJSON_GetObjectItem(item, "ha");

        if (enabled && cJSON_IsTrue(enabled) && ha_node && pin_obj)
        {
            char fallback[128];
            cJSON *pin_obj = cJSON_GetObjectItem(item, "pin");
            int pin = cJSON_GetNumberValue(pin_obj);
            const char *topic = cJSON_GetStringValue(cJSON_GetObjectItem(ha_node, "state_topic"));
            float value = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "value"));

            if (cJSON_GetNumberValue(cJSON_GetObjectItem(item, "type")) != DIGITAL)
            {
                // Format the float value for the MQTT payload
                snprintf(payload, sizeof(payload), "%.2f", value);
                mqtt_manager_publish_raw(_state, topic, payload, false);
            }
            else
            {
                mqtt_manager_publish_raw(_state, topic, value ? "ON" : "OFF", false);
            }
            // Publish the individual pin update
        }
    }
}

static void handle_inbound_gpio(MQTT_CLIENT_DATA_T *_state, int pin, const char *command)
{
    char *resp;
    //    printf("Inbound MQTT Control -> Pin %d: %s\n", pin, command);
    bool set_high = (lwip_stricmp(command, "on") == 0 || strcmp(command, "1") == 0 || lwip_stricmp(command, "ON") == 0);

    //    printf("Todo: set settings value and then mark channel dirty.\n");
    //    toggle_pin(pin);

    // char fallback[128];
    // cJSON *dev_id_obj = cJSON_GetObjectItem(_state->config_root, "device_id");
    // const char *dev_id = (dev_id_obj && dev_id_obj->valuestring) ? dev_id_obj->valuestring : "batmon";
    // cJSON *channels = cJSON_GetObjectItem(_state->config_root, "channels");

    // const char *topic = lookup_state_topic(channels, pin, fallback, sizeof(fallback), dev_id);
    // mqtt_manager_publish_raw(_state, topic, get_pin(pin) ? "ON" : "OFF", false);
}

// Safely format specific strings based on whether they contain %s, %d, or both
static void safe_hydrate_key(cJSON *ha_node, const char *key, const char *dev_id, int pin)
{
    cJSON *item = cJSON_GetObjectItem(ha_node, key);
    if (!item || !item->valuestring)
        return;

    char buffer[256];
    bool has_s = (strstr(item->valuestring, "%s") != NULL);
    bool has_d = (strstr(item->valuestring, "%d") != NULL);

    if (has_s && has_d)
    {
        snprintf(buffer, sizeof(buffer), item->valuestring, dev_id, pin);
    }
    else if (has_s)
    {
        snprintf(buffer, sizeof(buffer), item->valuestring, dev_id);
    }
    else if (has_d)
    {
        snprintf(buffer, sizeof(buffer), item->valuestring, pin);
    }
    else
    {
        return; // No format tokens found, leave string as-is
    }

    cJSON_SetValuestring(item, buffer);
}

static void sub_unsub_topics(MQTT_CLIENT_DATA_T *_state, bool sub)
{
    if (!_state || !_state->config_root)
        return;

    char buf[128];
    cJSON *dev_id_obj = cJSON_GetObjectItem(_state->config_root, "device_id");
    const char *dev_id = (dev_id_obj && dev_id_obj->valuestring) ? dev_id_obj->valuestring : "batmon";

    mqtt_sub_unsub(_state->mqtt_client_inst, "homeassistant/status", 0, NULL, _state, sub);

    snprintf(buf, sizeof(buf), "%s/restart", dev_id);
    mqtt_sub_unsub(_state->mqtt_client_inst, buf, 0, NULL, _state, sub);

    snprintf(buf, sizeof(buf), "%s/online", dev_id);
    mqtt_sub_unsub(_state->mqtt_client_inst, buf, 0, NULL, _state, sub);

    cJSON *channels = cJSON_GetObjectItem(_state->config_root, "channels");
    if (channels)
    {
        cJSON *ch = NULL;
        cJSON_ArrayForEach(ch, channels)
        {
            cJSON *enabled = cJSON_GetObjectItem(ch, "enabled");
            cJSON *pin_obj = cJSON_GetObjectItem(ch, "pin");
            cJSON *ha_node = cJSON_GetObjectItem(ch, "ha");

            if (enabled && cJSON_IsTrue(enabled) && ha_node && pin_obj)
            {
                cJSON *command_top = cJSON_GetObjectItem(ha_node, "command_topic");
                if (command_top && command_top->valuestring)
                {
                    if (strchr(command_top->valuestring, '%'))
                    {
                        snprintf(buf, sizeof(buf), command_top->valuestring, dev_id, pin_obj->valueint);
                        mqtt_sub_unsub(_state->mqtt_client_inst, buf, 0, NULL, _state, sub);
                    }
                    else
                    {
                        mqtt_sub_unsub(_state->mqtt_client_inst, command_top->valuestring, 0, NULL, _state, sub);
                    }
                }
            }
        }
    }
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;
    if (topic)
    {
        strncpy(_state->topic, topic, sizeof(_state->topic) - 1);
        _state->topic[sizeof(_state->topic) - 1] = '\0';
    }
}

static void ha_publish_discovery(MQTT_CLIENT_DATA_T *_state)
{
    if (!_state || !_state->config_root)
        return;

    cJSON *channels = cJSON_GetObjectItem(_state->config_root, "channels");
    if (!channels)
        return;

    cJSON *dev_id_obj = cJSON_GetObjectItem(_state->config_root, "device_id");
    cJSON *model_obj = cJSON_GetObjectItem(_state->config_root, "model");
    cJSON *version_obj = cJSON_GetObjectItem(_state->config_root, "version");
    cJSON *mqtt = cJSON_GetObjectItem(_state->config_root, "mqtt");
    cJSON *name_obj = cJSON_GetObjectItem(mqtt, "device_name");

    const char *dev_id = (dev_id_obj && dev_id_obj->valuestring) ? dev_id_obj->valuestring : "pico";
    const char *model = (model_obj && model_obj->valuestring && strlen(model_obj->valuestring) > 0) ? model_obj->valuestring : "Pico 2W";
    const char *dev_name = (name_obj && name_obj->valuestring && strlen(name_obj->valuestring) > 0) ? name_obj->valuestring : "Pico Gateway";

    // Handle integer or string version safely
    char version_str[16];
    if (version_obj && cJSON_IsNumber(version_obj))
    {
        snprintf(version_str, sizeof(version_str), "%d", version_obj->valueint);
    }
    else if (version_obj && version_obj->valuestring)
    {
        snprintf(version_str, sizeof(version_str), "%s", version_obj->valuestring);
    }
    else
    {
        strncpy(version_str, "1.0.0", sizeof(version_str));
    }

    printf("Generating Clean Auto-Discovery records from nested HA configurations...\n");

    cJSON *ch = NULL;
    char raw_json[1024];
    cJSON_ArrayForEach(ch, channels)
    {
        cJSON *enabled_obj = cJSON_GetObjectItem(ch, "enabled");
        if (!enabled_obj || !cJSON_IsTrue(enabled_obj))
            continue;

        cJSON *pin_obj = cJSON_GetObjectItem(ch, "pin");
        cJSON *ha_node = cJSON_GetObjectItem(ch, "ha");
        if (!pin_obj || !ha_node)
            continue;

        cJSON *platform_obj = cJSON_GetObjectItem(ha_node, "platform");

        int pin = pin_obj->valueint;
        const char *platform = platform_obj->valuestring;
        if (!platform_obj || !platform_obj->valuestring)
            continue;

        // Duplicate the block to avoid damaging runtime memory settings
        cJSON *disc_payload = cJSON_Duplicate(ha_node, true);

        // Run hydration across all necessary fields cleanly via an array loop
        const char *keys_to_hydrate[] = {
            "name", "state_topic", "command_topic",
            "unique_id", "availability_topic", "value_template"};
        for (size_t i = 0; i < sizeof(keys_to_hydrate) / sizeof(keys_to_hydrate[0]); i++)
        {
            safe_hydrate_key(disc_payload, keys_to_hydrate[i], dev_id, pin);
        }

        // Strip the platform identifier out of the payload body
        cJSON_DeleteItemFromObject(disc_payload, "platform");

        // Inject the core, shared device registration schema block
        cJSON *device = cJSON_CreateObject();
        cJSON *ids = cJSON_CreateArray();
        cJSON_AddItemToArray(ids, cJSON_CreateString(dev_id));
        cJSON_AddItemToObject(device, "identifiers", ids);
        cJSON_AddStringToObject(device, "name", dev_name);
        cJSON_AddStringToObject(device, "model", model);
        cJSON_AddStringToObject(device, "sw_version", version_str);
        cJSON_AddItemToObject(disc_payload, "device", device);

        // Build target registration endpoint topic path
        char discovery_topic[128];
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/%s/%s/%s_ch_%d/config",
                 platform, dev_id, dev_id, pin);

        if (cJSON_PrintPreallocated(disc_payload, raw_json, sizeof(raw_json), false))
        {
            mqtt_manager_publish_raw(_state, discovery_topic, raw_json, true);
        }

        cJSON_Delete(disc_payload);
        cyw43_arch_poll();
    }
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;
    size_t safe_len = (len < sizeof(_state->data) - 1) ? len : sizeof(_state->data) - 1;
    memcpy(_state->data, data, safe_len);
    _state->data[safe_len] = '\0';

    if (strstr(_state->topic, "/restart"))
    {
        watchdog_reboot(0, 0, 100);
        return;
    }

    if (strstr(_state->topic, "homeassistant/status"))
    {
        if (lwip_stricmp(_state->data, "online") == 0)
        {
            ha_publish_discovery(_state);
        }
        return;
    }

    cJSON *channels = cJSON_GetObjectItem(_state->config_root, "channels");
    cJSON *dev_id_obj = cJSON_GetObjectItem(_state->config_root, "device_id");
    const char *dev_id = (dev_id_obj && dev_id_obj->valuestring) ? dev_id_obj->valuestring : "batmon";

    if (channels)
    {
        int Index = 0;
        cJSON *ch = NULL;
        cJSON_ArrayForEach(ch, channels)
        {
            cJSON *ha_node = cJSON_GetObjectItem(ch, "ha");
            if (ha_node)
            {
                cJSON *cmd_obj = cJSON_GetObjectItem(ha_node, "command_topic");
                if (cmd_obj && cmd_obj->valuestring)
                {
                    char check_buf[128];
                    const char *expected_topic = cmd_obj->valuestring;

                    if (strchr(expected_topic, '%'))
                    {
                        cJSON *pin_obj = cJSON_GetObjectItem(ch, "pin");
                        if (pin_obj)
                        {
                            snprintf(check_buf, sizeof(check_buf), expected_topic, dev_id, pin_obj->valueint);
                            expected_topic = check_buf;
                        }
                    }

                    if (strcmp(_state->topic, expected_topic) == 0)
                    {
                        cJSON *tg = cJSON_GetObjectItem(ch, "toggle");
                        if (!tg)
                        {
                            tg = cJSON_AddBoolToObject(ch, "toggle", 1);
                        }
                        else
                        {
                            cJSON_SetBoolValue(tg, 1);
                        }
                        break;
                    }
                }
            }
            Index++;
        }
    }
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    MQTT_CLIENT_DATA_T *_state = (MQTT_CLIENT_DATA_T *)arg;
    _state->status = status;
    _state->is_connecting = false; // The cycle is complete, win or lose

    if (status == MQTT_CONNECT_ACCEPTED)
    {
        printf("MQTT Session Bound.\n");
        sub_unsub_topics(_state, true);

        char buf[128];
        cJSON *dev_id_obj = cJSON_GetObjectItem(_state->config_root, "device_id");
        snprintf(buf, sizeof(buf), "%s/status", (dev_id_obj && dev_id_obj->valuestring) ? dev_id_obj->valuestring : "batmon");
        ha_publish_discovery(_state);
        mqtt_manager_publish_raw(_state, buf, "online", true);
    }
    else
    {
        printf("MQTT Connection Dropped/Refused: %d\n", status);
    }
}

static void start_client(MQTT_CLIENT_DATA_T *_state)
{
    if (!_state->mqtt_client_inst)
        _state->mqtt_client_inst = mqtt_client_new();

    mqtt_set_inpub_callback(_state->mqtt_client_inst, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, _state);

    cJSON *mqtt_node = cJSON_GetObjectItem(_state->config_root, "mqtt");
    if (!mqtt_node)
    {
        _state->is_connecting = false;
        return;
    }

    cJSON *user_obj = cJSON_GetObjectItem(mqtt_node, "user");
    cJSON *pass_obj = cJSON_GetObjectItem(mqtt_node, "password");
    cJSON *dev_id_obj = cJSON_GetObjectItem(_state->config_root, "device_id");

    _state->mqtt_client_info.client_id = (dev_id_obj && dev_id_obj->valuestring) ? dev_id_obj->valuestring : "batmon_pico";
    _state->mqtt_client_info.keep_alive = 60;
    _state->mqtt_client_info.client_user = (user_obj && user_obj->valuestring) ? user_obj->valuestring : NULL;
    _state->mqtt_client_info.client_pass = (pass_obj && pass_obj->valuestring) ? pass_obj->valuestring : NULL;

    cyw43_arch_lwip_begin();
    err_t err = mqtt_client_connect(_state->mqtt_client_inst, &_state->mqtt_server_address, MQTT_PORT, mqtt_connection_cb, _state, &_state->mqtt_client_info);
    cyw43_arch_lwip_end();

    if (err != ERR_OK)
    {
        printf("Immediate connect failed: %d\n", err);
        _state->status = MQTT_CONNECT_DISCONNECTED;
        _state->is_connecting = false;
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
        printf("DNS Resolution Failed for broker.\n");
        _state->status = MQTT_CONNECT_DISCONNECTED;
        _state->is_connecting = false;
    }
}

bool mqtt_manager_start(MQTT_CLIENT_DATA_T *state)
{
    if (!state || !state->config_root)
        return false;

    // Already connected? Nothing to do.
    if (state->status == MQTT_CONNECT_ACCEPTED)
        return true;

    // Connection attempt in progress? Let it ride.
    if (state->is_connecting)
    {
        cyw43_arch_poll();
        return false;
    }

    cJSON *mqtt_node = cJSON_GetObjectItem(state->config_root, "mqtt");
    if (!mqtt_node)
        return false;

    cJSON *broker_obj = cJSON_GetObjectItem(mqtt_node, "broker");
    if (!broker_obj || !broker_obj->valuestring || !strlen(broker_obj->valuestring))
        return false;
    cJSON *user_obj = cJSON_GetObjectItem(mqtt_node, "user");
    if (!user_obj || !user_obj->valuestring || !strlen(user_obj->valuestring))
        return false;
    cJSON *password_obj = cJSON_GetObjectItem(mqtt_node, "password");
    if (!password_obj || !password_obj->valuestring || !strlen(password_obj->valuestring))
        return false;

    // Set guard flag to handle async execution safely
    state->is_connecting = true;
    printf("Resolving MQTT Broker: %s\n", broker_obj->valuestring);

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(broker_obj->valuestring, &state->mqtt_server_address, dns_found_cb, state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK)
    {
        start_client(state);
    }
    else if (err != ERR_INPROGRESS)
    {
        // Immediate hard failure (e.g. invalid arguments or down interface)
        state->is_connecting = false;
    }

    return false;
}

bool mqtt_manager_init(MQTT_CLIENT_DATA_T *_state, cJSON *config)
{
    if (!_state || !config)
        return false;
    memset(_state, 0, sizeof(MQTT_CLIENT_DATA_T));
    _state->config_root = config;
    _state->status = MQTT_CONNECT_DISCONNECTED; // Force evaluation out of the gate
    return true;
}