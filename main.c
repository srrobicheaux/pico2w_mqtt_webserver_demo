#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "networking.h"
#include "io_manager.h"
#include "flash_manager.h"
#include "cJSON.h"
#include "webserver.h"
#include "mqtt_manager.h"

void check_button(cJSON *root)
{
    if (!poll_bootsel_button())
        return;

    watchdog_disable();
    // if more than ~5s held, reset settings
    absolute_time_t LongPress = get_absolute_time() + 5000000; //~5seconds

    while (poll_bootsel_button())
        ;
    if (time_reached(LongPress))
    {
        printf("Button held long enough, resetting settings.\n");
        // create a version mismatch to factory reset
        cJSON_SetNumberValue(cJSON_GetObjectItem(root, "version"), 0);

        flash_save_settings(root);
    }
    else
    {
        printf("Short Button press, rebooting only.\n");
    }
    watchdog_reboot(0, 0, 1500);
}

bool timer_callback_webupdate_channels(repeating_timer_t *mst)
{
    MQTT_CLIENT_DATA_T *mqtt_state = (MQTT_CLIENT_DATA_T *)mst->user_data;
    cJSON *channels = cJSON_GetObjectItem(mqtt_state->config_root, "channels");

    cJSON *updates = channel_updates(channels);
    if (updates != NULL)
    {
        webserver_send_sse_update(updates);
        cJSON_Delete(updates); // Free the updates object allocation
    }

    return true;
}

bool timer_callback_mqttupdate_channels(repeating_timer_t *mst)
{
    MQTT_CLIENT_DATA_T *mqtt_state = (MQTT_CLIENT_DATA_T *)mst->user_data;
    cJSON *channels = cJSON_GetObjectItem(mqtt_state->config_root, "channels");

    cJSON *updates = channel_updates(channels);
    if (updates != NULL)
    {
        mqtt_manager_publish_state(mst->user_data, updates);
        cJSON_Delete(updates); // Free the updates object allocation
    }

    return true;
}

bool timer_callback_update_status(repeating_timer_t *mst)
{
    MQTT_CLIENT_DATA_T *mqtt_state = (MQTT_CLIENT_DATA_T *)mst->user_data;
    cJSON *system_update = system_channel_update();
    if (system_update != NULL)
    {
        webserver_send_sse_update(system_update);
        mqtt_manager_publish_state(mqtt_state, system_update);
        cJSON_Delete(system_update); // Free the updates object allocation
    }

    return true;
}

bool timer_callback_check_config(repeating_timer_t *mst)
{
    MQTT_CLIENT_DATA_T *mqtt_state = (MQTT_CLIENT_DATA_T *)mst->user_data;

    // Handle Runtime Configuration Changes (The Webserver Dirty Bit)
    // If user changes broker profiles via UI, gracefully recycle the connection
    cJSON *dirty_node = cJSON_GetObjectItem(mqtt_state->config_root, "is_dirty");
    if (dirty_node && cJSON_IsTrue(dirty_node))
    {
        printf("Config change detected. Cycling MQTT Engine...\n");

        // Disconnect existing client if allocated
        if (mqtt_state->mqtt_client_inst)
        {
            cyw43_arch_lwip_begin();
            mqtt_disconnect(mqtt_state->mqtt_client_inst);
            cyw43_arch_lwip_end();
        }

        // Re-initialize state machine with updated runtime settings object
        mqtt_manager_init(mqtt_state, mqtt_state->config_root);

        // Clear the dirty bit flag
        cJSON_SetBoolValue(dirty_node, false);
    }

    // Run the background state engine continuously to handle async DNS and keep-alives
    mqtt_manager_start(mqtt_state);
}

int main()
{
    stdio_init_all();
    sleep_ms(3000); // Allow time for USB serial to connect

    cJSON *settings = load_configuration();
    cJSON *channels = cJSON_GetObjectItem(settings, "channels");
    cJSON *wifi = cJSON_GetObjectItem(settings, "wifi");
    cJSON *mqtt = cJSON_GetObjectItem(settings, "mqtt");

    bool on_wifi = wifi_init(
        cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "ssid")),
        cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "password")),
        cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "network_name")));

    watchdog_enable(180000, 0);
    start_webserver(settings);

    int wifi_ms = cJSON_GetNumberValue(cJSON_GetObjectItem(wifi, "update_interval"));
    int mqtt_ms = cJSON_GetNumberValue(cJSON_GetObjectItem(mqtt, "update_interval"));

    // Safely zero memory and bind config pointers
    MQTT_CLIENT_DATA_T mqtt_state;
    mqtt_manager_init(&mqtt_state, settings);

    io_init_all(channels);

    static repeating_timer_t mst_mqttupdate_channels;
    add_repeating_timer_ms(-mqtt_ms, timer_callback_mqttupdate_channels, &mqtt_state, &mst_mqttupdate_channels);

    static repeating_timer_t mst_webupdate_channels;
    add_repeating_timer_ms(-wifi_ms, timer_callback_webupdate_channels, &mqtt_state, &mst_webupdate_channels);

    static repeating_timer_t mst_status;
    add_repeating_timer_ms(-5000, timer_callback_update_status, &mqtt_state, &mst_status);

    static repeating_timer_t mst_config;
    add_repeating_timer_ms(-1000, timer_callback_check_config, &mqtt_state, &mst_config);

    cJSON *dirty_bit = cJSON_GetObjectItem(settings, "is_dirty");
    cJSON_SetBoolValue(dirty_bit, 0);

    while (true)
    {
        if(cJSON_IsTrue(dirty_bit)){
            printf("Saving...\n");
            flash_save_settings();
            cJSON_SetBoolValue(dirty_bit, 0);
        }

        wifi_poll();
        check_button(settings);
        watchdog_update();
    }
    return 0;
}