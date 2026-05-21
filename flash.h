#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include <stddef.h>
#include "pico/time.h"
#include "wifi_provisioning.h"
#include "IOs.h"
#include "mqtt_manager.h"

#define SETTINGS_VERSION      0x0009
#define FLASH_TARGET_OFFSET   (1536 * 1024UL)
//#define FLASH_SECTOR_SIZE     4096

typedef struct {
    uint16_t version;
    wifi_t wifi;
    io_t IOs;
    mqtt_settings_t mqtt;
    uint32_t position;
    uint32_t crc32;
} DeviceSettings;

bool load_settings(DeviceSettings *settings);
bool save_settings(const DeviceSettings *settings);

int settings_to_json_compact(char *buf, size_t bufsize);
int settings_to_json_pretty(char *buf, size_t bufsize);

bool json_to_settings(const char *json, DeviceSettings *s);

void save_voltage_log(const analog_log_t *log, DeviceSettings *settings);
const analog_log_t *read_voltage_log(uint32_t position);

bool get_bootsel_button_pressed(void);
void factory_reset(void);
void reboot_device(void);

#endif