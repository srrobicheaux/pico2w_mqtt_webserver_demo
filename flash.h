#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include <stddef.h>
#include "pico/time.h"
#include "IOs.h"
#include "mqtt_manager.h"
#include "wifi_provisioning.h"

typedef struct {
    uint16_t version;     
    mqtt_settings_t mqtt;
    wifi_t wifi;
    io_t IOs;

    uint32_t position;     // Next VoltageLog index (circular buffer)
} DeviceSettings;

bool load_settings(DeviceSettings *settings);
void save_settings(DeviceSettings *settings, size_t len);

// Voltage history functions
void save_voltage_log(const analog_log_t *log, DeviceSettings *settings);
const analog_log_t *read_voltage_log(uint32_t position);

/*
// Hardware / watchdog helpers
bool get_bootsel_button();
void send_status_event(void (*notifer)(char *json, size_t size));
void reset();

typedef enum {
    GPIO_ACTION_READ = 0,
    GPIO_ACTION_TOGGLE = 1,
    GPIO_ACTION_PRESS = 2
} gpio_action_t;

// Update the function prototype
bool pin_action(int pio, gpio_action_t action);

*/
#endif // FLASH_H
