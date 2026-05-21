#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "IOs.h"
#include "mqtt_manager.h"
#include "system_info.h"

#include "webserver.h"
#include "wifi_provisioning.h"
#include "flash.h"

int main()
{
    bool connected = false;
    DeviceSettings global_settings;
    MQTT_CLIENT_DATA_T mqtt_client;

    stdio_init_all();
    sleep_ms(2000);

    watchdog_enable(60000, true);   // 1 minute watchdog
    IO_init(&global_settings.IOs);

    printf("Figure out mqtt update of pio.\n");
    while( !connected){
        load_settings(&global_settings);
        connected = wifi_init(&global_settings.wifi);
        if (!connected) {
            printf("WiFi connection failed. Retrying with defaults.\n");
        }
        watchdog_update();
    }


    webserver_init(!connected, global_settings.wifi.network_name);
    mqtt_init(&global_settings.wifi, &global_settings.mqtt, &mqtt_client);

    uint32_t last_publish = 0;
    uint32_t last_button_check = 0;
    uint32_t button_press_start = 0;
    bool button_was_pressed = false;

    while (true) {
        watchdog_update();
        cyw43_arch_poll();

        uint32_t now = to_ms_since_boot(get_absolute_time());
/*
        // ============== Button Handling ==============
        if (now - last_button_check > 50) {          // 50ms debounce
            last_button_check = now;
            bool pressed = get_bootsel_button();

            if (pressed && !button_was_pressed) {
                button_press_start = now;
                button_was_pressed = true;
            }
            else if (!pressed && button_was_pressed) {
                // Short press = reboot
                if (now - button_press_start < 3000) {
                    printf("Short BOOTSEL press - rebooting\n");
                    reboot_device();
                }
                button_was_pressed = false;
            }
            else if (pressed && (now - button_press_start > 5000)) {
                // Long press (>5s) = factory reset
                printf("Long BOOTSEL press - factory reset!\n");
                factory_reset();
            }
        }
*/
        // ============== Normal operation loops ==============

        if (connected && (now - last_publish > 500)) {   // adjust interval
            mqtt_manager_start(&mqtt_client, &global_settings.mqtt);

            static char buffer[256];
            IOs_JSON(buffer, sizeof(buffer));
            webserver_push_update("IO", buffer);
            mqtt_manager_publish(&mqtt_client, "/IO", buffer);

            load_status_JSON(buffer);
            webserver_push_update("system_status", buffer);
            mqtt_manager_publish(&mqtt_client, "/system_status", buffer);

            last_publish = now;
        }

        best_effort_wfe_or_timeout(make_timeout_time_ms(10));
    }
 //   wifi_provisioning_start();
}