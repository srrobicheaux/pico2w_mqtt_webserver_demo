#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "IOs.h"
#include "webserver.h"
#include "wifi_provisioning.h"
#include "mqtt_manager.h"
#include "system_info.h"
#include "flash.h"

int main()
{
    DeviceSettings global_settings;
    MQTT_CLIENT_DATA_T mqtt_client;

    stdio_init_all();
    sleep_ms(2000);

    printf("BatMon starting...\n");

    load_settings(&global_settings);

    watchdog_enable(300000, true);   // 5 minute watchdog

    // Button setup (BOOTSEL button on Pico 2W is GPIO 25 when pressed)
    gpio_init(25);
    gpio_set_dir(25, GPIO_IN);
    gpio_pull_up(25);

    bool connected = wifi_init(&global_settings.wifi);
    IO_init(&global_settings.IOs);
    mqtt_init(&global_settings.wifi, &global_settings.mqtt, &mqtt_client);
    webserver_init(!connected);

    uint32_t last_publish = 0;
    uint32_t last_button_check = 0;
    uint32_t button_press_start = 0;
    bool button_was_pressed = false;

    while (true) {
        cyw43_arch_poll();
        watchdog_update();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // ============== Button Handling ==============
        if (now - last_button_check > 50) {          // 50ms debounce
            last_button_check = now;
            bool pressed = !gpio_get(25);             // BOOTSEL button is active-low

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

        // ============== Normal operation loops ==============
        if (connected && !mqtt_manager_is_connected() /*&&  your reconnect logic */) {
            mqtt_manager_start();
        }

        if (connected && (now - last_publish > 500)) {   // adjust interval
            IOs_JSON(mqtt_client.data, sizeof(mqtt_client.data));
            webserver_push_update("IO", mqtt_client.data);
            if (mqtt_manager_is_connected())
                mqtt_manager_publish("/IO", mqtt_client.data);

            send_system_status_event(mqtt_client.data);
            webserver_push_update("system_status", mqtt_client.data);
            if (mqtt_manager_is_connected())
                mqtt_manager_publish("/system_status", mqtt_client.data);

            last_publish = now;
        }

        best_effort_wfe_or_timeout(make_timeout_time_ms(10));
    }
}