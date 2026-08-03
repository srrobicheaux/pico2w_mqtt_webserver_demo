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
#include "malloc.h"

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
    cJSON *channels = (cJSON *)mst->user_data;
    if (!channels)
        return true;

    char payload[512] = "data: {";
    char *pos = payload + strlen(payload);

    int index = 0;
    cJSON *item = NULL;

    cJSON_ArrayForEach(item, channels)
    {
        cJSON *ptr_value = cJSON_GetObjectItem(item, "value");

        if (ptr_value)
        {
            float val = (float)cJSON_GetNumberValue(ptr_value);
            size_t rem = sizeof(payload) - (pos - payload);

            int written = snprintf(pos, rem, "\"%d\":%.2f,", index, val);
            if (written > 0 && (size_t)written < rem)
            {
                pos += written;
            }
        }
        index++;
    }

    // Replace trailing comma with closing brace and SSE newlines
    if (*(pos - 1) == ',')
    {
        pos--;
    }
    snprintf(pos, sizeof(payload) - (pos - payload), "}\n\n");
    cyw43_arch_lwip_begin();
    webserver_send_sse_update(payload);
    cyw43_arch_lwip_end();

    return true;
}

bool timer_callback_mqttupdate_channels(repeating_timer_t *mst)
{
    MQTT_CLIENT_DATA_T *system_state = (MQTT_CLIENT_DATA_T *)mst->user_data;
    //    cJSON *channels = cJSON_GetObjectItem(system_state->config_root, "channels");

    cyw43_arch_lwip_begin();
    mqtt_manager_publish_state(system_state);
    cyw43_arch_lwip_end();
    return true;
}

extern char __StackLimit, __bss_end__;


//Todo List (Backlog):
//Allow multiple WiFI networks to be stored and cycled through on connection failure
//Add a "reset to factory defaults" button on the webserver page
//Factory reset if button held for 5 seconds on boot
//Monitor network disconnects and attempt to reconnect automatically
//If nework connection isnt successful after 30 seconds, Enter AP mode.
//If AP mode without configuration for 5 minutes, reboot and try connections again.
//Log analog values to flash and allow download of CSV file from webserver
//seperate MQTT and Webserver into their own threads to avoid blocking each other
//seperate Analog and Diagnostic channels into their own website areas
//fix network naming to be more user friendly (currently uses Picow for SSID and network name)

int main()
{
    stdio_init_all();
    load_configuration(); // Populates g_config

    cJSON *channels = cJSON_GetObjectItem(g_config, "channels");
    cJSON *wifi = cJSON_GetObjectItem(g_config, "wifi");
    cJSON *mqtt = cJSON_GetObjectItem(g_config, "mqtt");

    bool on_wifi = wifi_init(
        cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "ssid")),
        cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "password")),
        cJSON_GetStringValue(cJSON_GetObjectItem(wifi, "network_name")));

    watchdog_enable(180000, 0);
    start_webserver(g_config);

    MQTT_CLIENT_DATA_T system_state;
    mqtt_manager_init(&system_state, g_config);
    io_init_all(channels);
    mqtt_manager_start(&system_state);

    static repeating_timer_t mst_mqttupdate_channels;
    add_repeating_timer_ms(2000, timer_callback_mqttupdate_channels, &system_state, &mst_mqttupdate_channels);

    static repeating_timer_t mst_webupdate_channels;
    add_repeating_timer_ms(500, timer_callback_webupdate_channels, channels, &mst_webupdate_channels);

    while (true)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

        best_effort_wfe_or_timeout(make_timeout_time_ms(100));

        if (g_config_dirty)
        {
            // 1. Pause repeating timers so ISR callbacks do not access freed JSON
            cancel_repeating_timer(&mst_webupdate_channels);
            cancel_repeating_timer(&mst_mqttupdate_channels);

            if (g_pending_config)
            {
                // 2. Commit new configuration to physical flash
                flash_save_settings(g_pending_config);

                // 3. Swap root pointers safely
                if (g_config)
                    cJSON_Delete(g_config);
                g_config = g_pending_config;
                g_pending_config = NULL;

                // 4. Update local sub-node pointers
                channels = cJSON_GetObjectItem(g_config, "channels");
                wifi = cJSON_GetObjectItem(g_config, "wifi");
                mqtt = cJSON_GetObjectItem(g_config, "mqtt");

                // 4b. Pass the new pointer to the webserver!
                webserver_update_config(g_config);
            }

            g_config_dirty = false;

            webserver_send_sse_update("data: {\"MESSAGE\":\"Settings have changed. Refresh to load them.\"}\n\n");
            printf("Config change detected. Re-initializing subsystems...\n");

            // 5. Re-initialize IO and MQTT with fresh pointers
            io_init_all(channels);

            if (system_state.mqtt_client_inst)
            {
                cyw43_arch_lwip_begin();
                mqtt_disconnect(system_state.mqtt_client_inst);
                cyw43_arch_lwip_end();
            }

            mqtt_manager_init(&system_state, g_config);
            mqtt_manager_start(&system_state);

            // 6. Restart hardware timers with updated target pointers
            add_repeating_timer_ms(2000, timer_callback_mqttupdate_channels, &system_state, &mst_mqttupdate_channels);
            add_repeating_timer_ms(500, timer_callback_webupdate_channels, channels, &mst_webupdate_channels);
        }
        wifi_poll();
        check_button(g_config);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

        channel_updates(channels);
        watchdog_update();
    }
    return 0;
}