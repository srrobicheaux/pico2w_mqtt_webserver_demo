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

// single repeating timer callback for MQTT and web updates
bool timer_due_callback(struct repeating_timer *t)
{
    // Safely cast the void pointer back to a volatile boolean pointer
    volatile bool *flag = (volatile bool *)t->user_data;
    *flag = true;

    return true;
}

void check_button(cJSON *root)
{
    if (!poll_bootsel_button())
        return;

    watchdog_disable();
    // Safely calculate the 5-second future timestamp
    absolute_time_t LongPress = delayed_by_us(get_absolute_time(), 5000000);

    while (poll_bootsel_button())
    {
        tight_loop_contents(); // Prevents the compiler from aggressively optimizing the empty loop
    }

    if (time_reached(LongPress))
    {
        printf("Button held long enough, resetting settings.\n");
        // create a version mismatch to factory reset
        cJSON_SetNumberValue(cJSON_GetObjectItem(root, "version"), 0);
        printf("loading the default hasnt been implemented correctly.\n");
        flash_save_settings(root);
    }
    else
    {
        printf("Short Button press, rebooting only.\n");
    }
    watchdog_enable(5, 0);
    sleep_ms(10);
}

bool webupdate_channels(cJSON *channels)
{
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

bool mqtt_update(MQTT_CLIENT_DATA_T *system_state)
{
    cyw43_arch_lwip_begin();
    mqtt_manager_publish_state(system_state);
    cyw43_arch_lwip_end();
    return true;
}

extern char __StackLimit, __bss_end__;

// Todo List (Backlog):

// Log analog values to flash and allow download of CSV file from webserver

// 1. Define the struct holding your data and a function pointer for the callback
typedef struct
{
    struct repeating_timer timer;
    volatile bool due;
    int32_t interval_ms;

    // Function pointer matching the Pico SDK repeating timer signature
    bool (*callback)(struct repeating_timer *t);
} timed_boolean_t;

// 2. Write a single shared callback function that handles any instance
static bool generic_timer_callback(struct repeating_timer *t)
{
    // The user_data pointer points directly to the 'due' boolean inside our struct
    volatile bool *flag = (volatile bool *)t->user_data;
    *flag = true;
    return true; // Keep repeating
}

// 3. Create an initializer function to wire and start it cleanly
void timed_boolean_init(timed_boolean_t *tb, int32_t interval_ms)
{
    tb->interval_ms = interval_ms;
    tb->due = false;
    tb->callback = generic_timer_callback;

    // Register the timer, passing the address of 'due' as user_data
    add_repeating_timer_ms(
        tb->interval_ms,
        tb->callback,
        (void *)&tb->due,
        &tb->timer);
}

// Instantiate your grouped timers
static timed_boolean_t mqtt_timer;
static timed_boolean_t web_timer;
static timed_boolean_t wifi_timer;

int main()
{
    MQTT_CLIENT_DATA_T system_state = {0}; // Ensure this is initialized with zeroes
    volatile bool initization_due = true;
    wifi_timer.due = true;

    stdio_init_all();
    //    sleep_ms(3000);
    watchdog_enable(180000, 1);
    wifi_mode mode = WIFI_NOT_INITIALIZE;

    load_configuration(); // Populates g_config

    cJSON *networks = cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "wifi"), "networks");

    while (true)
    {
        mode = wifi_poll(mode, networks);
        if (mode == WIFI_AP || mode == WIFI_CONNECTED)
        {

            if (initization_due)
            {
                printf("(Re)initializing subsystems...\n");
                io_init_all(cJSON_GetObjectItem(g_config, "channels"));
                mqtt_manager_init(&system_state, g_config);

                start_webserver(g_config);
                webserver_send_sse_update("data: {\"MESSAGE\":\"System initialized. Refresh to load settings.\"}\n\n");

                timed_boolean_init(&wifi_timer, cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "wifi"), "interval_ms") ? cJSON_GetNumberValue(cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "wifi"), "interval_ms")) : 120000);
                timed_boolean_init(&web_timer, cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "web"), "interval_ms") ? cJSON_GetNumberValue(cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "web"), "interval_ms")) : 100);
                timed_boolean_init(&mqtt_timer, cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "mqtt"), "interval_ms") ? cJSON_GetNumberValue(cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "mqtt"), "interval_ms")) : 2000);

                initization_due = false;
            }
            channel_updates(cJSON_GetObjectItem(g_config, "channels"));
            if (mqtt_timer.due && !initization_due)
            {
                // start if not already started exit if connected
                if (mqtt_manager_start(&system_state))
                {
                    mqtt_update(&system_state);
                };
                mqtt_timer.due = false;
            }

            if (web_timer.due)
            {
                webupdate_channels(cJSON_GetObjectItem(g_config, "channels"));

                web_timer.due = false;
            }

            if (reconfig_due)
            {
                if (g_pending_config)
                {
                    flash_save_settings(g_pending_config);

                    if (g_config)
                        cJSON_Delete(g_config);
                    g_config = g_pending_config;
                    networks = cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "wifi"), "networks");

                    g_pending_config = NULL;
                }

                reconfig_due = false;
                initization_due = true;
            }
        }
 
        // 1. Feed the Watchdog continuously as long as the loop isn't locked up
        watchdog_update();
        check_button(g_config);
    }
    return 0;
}