#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "IOs.h"
#include "webserver.h"
#include "wifi_provisioning.h"
#include "mqtt_manager.h"
#include "system_info.h"
#include "flash.h" // Needed for load_settings()

//todos:
//fix flash saves.
//fix history saves
    //wireup gettime
//add io toggle button
        //set direction on toggle request
//rewire Min/Max off of feed data
//make dashboar honor IO counts


int main()
{
    DeviceSettings global;
    MQTT_CLIENT_DATA_T mqtt_client;

    stdio_init_all();
    sleep_ms(3000);

    load_settings(&global);
    watchdog_enable(300000, true);

    // ConnectNetwork handles AP vs STA mode internally
    bool connected = wifi_init(&global.wifi);
    IO_init(&global.IOs);
    mqtt_init(&global.wifi, &global.mqtt, &mqtt_client);
    webserver_init(!connected);

    uint32_t last_publish_time = 0;
    uint32_t last_reconnect_time = 0;
    uint32_t duration = 10; //seconds

    watchdog_update();

    uint32_t now;
    while (true)
    {
        cyw43_arch_poll(); // This is required in poll mode
        watchdog_update();

        now = to_ms_since_boot(get_absolute_time());

        // 1. NON-BLOCKING RECONNECT LOOP (Check every 10 seconds)
        if (connected && !mqtt_manager_is_connected() && (now - last_reconnect_time > 10000 * duration))
        {
            mqtt_manager_start();
            last_reconnect_time = now;
            duration++;
        }

        // 2. NON-BLOCKING PUBLISH LOOP
        if (connected && (now - last_publish_time > 0))
        {
            // --- All IOs ---
            IOs_JSON(mqtt_client.data, 256);
            webserver_push_update("IO", mqtt_client.data);
            if (mqtt_manager_is_connected())
                mqtt_manager_publish("/IO", mqtt_client.data);

            // --- System Status ---
            send_system_status_event(mqtt_client.data);
            webserver_push_update("system_status", mqtt_client.data);
            if (mqtt_manager_is_connected())
                mqtt_manager_publish("/system_status", mqtt_client.data);

            last_publish_time = now;
        }

        best_effort_wfe_or_timeout(make_timeout_time_ms(10));
    }
}