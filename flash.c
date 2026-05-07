#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/unique_id.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "flash.h"
#include <string.h>
#include <stdio.h>
#include "secrets.h"

static const DeviceSettings *flash_settings = (const DeviceSettings *)(XIP_BASE + FLASH_TARGET_OFFSET);

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

bool load_settings(DeviceSettings *local) {
    char id_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(id_str, sizeof(id_str));

    if (flash_settings->version != SETTINGS_VERSION ||
        strcmp(flash_settings->wifi.device_id, id_str) != 0 ||
        crc32((const uint8_t*)flash_settings, offsetof(DeviceSettings, crc32)) != flash_settings->crc32) {

        printf("No valid settings found - loading defaults\n");
        memset(local, 0, sizeof(DeviceSettings));
        strcpy(local->wifi.device_id, id_str);
        local->version = SETTINGS_VERSION;
        local->IOs.analog_count = 3;
        local->IOs.pio_count = 12;

        for (int i = 0; i < 3; ++i) {
            snprintf(local->IOs.analogs[i].name, sizeof(local->IOs.analogs[i].name), "Analog %d", i+1);
            local->IOs.analogs[i].enabled = true;
            local->IOs.analogs[i].ratio = 1.0f;
            local->IOs.analogs[i].offset = 0.0f;
        }
        for (int i = 0; i < 12; ++i) {
            snprintf(local->IOs.pios[i].name, sizeof(local->IOs.pios[i].name), "PIO %d", i);
            local->IOs.pios[i].enabled = true;
            local->IOs.pios[i].is_out = false;
        }
        return false;
    }

    memcpy(local, flash_settings, sizeof(DeviceSettings));
    strncpy(local->wifi.password, SSID_PW, sizeof(local->wifi.password) - 1);
    strncpy(local->mqtt.password, MQTT_PW, sizeof(local->mqtt.password) - 1);
    printf("Settings loaded successfully for board %s\n", id_str);
    return true;
}

bool save_settings(const DeviceSettings *settings) {
    DeviceSettings copy = *settings;
    copy.crc32 = crc32((const uint8_t*)&copy, offsetof(DeviceSettings, crc32));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t*)&copy, sizeof(DeviceSettings));
    restore_interrupts(ints);

    printf("Settings saved (version 0x%04X)\n", copy.version);
    return true;
}

// ====================== FULL JSON SERIALIZATION ======================
int settings_to_json(const DeviceSettings *s, char *buf, size_t bufsize) {
    char *p = buf;
    size_t rem = bufsize;
    int len;

    len = snprintf(p, rem, "{\"version\":%d,\"position\":%u,", s->version, s->position);
    p += len; rem -= len;

    // WiFi
    len = snprintf(p, rem, "\"wifi\":{\"ssid\":\"%s\",\"password\":\"***\",\"net_name\":\"%s\",\"dev_id\":\"%s\"},",
                   s->wifi.ssid, s->wifi.network_name, s->wifi.device_id);
    p += len; rem -= len;

    // MQTT
    len = snprintf(p, rem, "\"mqtt\":{\"server\":\"%s\",\"user\":\"%s\",\"password\":\"***\",\"model\":\"%s\",\"version\":\"%s\"},",
                   s->mqtt.server, s->mqtt.user, s->mqtt.model ? s->mqtt.model : "", s->mqtt.version ? s->mqtt.version : "");
    p += len; rem -= len;

    // IOs
    len = snprintf(p, rem, "\"IOs\":{\"analog_count\":%u,\"pio_count\":%u,\"analog\":[",
                   s->IOs.analog_count, s->IOs.pio_count);
    p += len; rem -= len;

    for (uint32_t i = 0; i < s->IOs.analog_count; ++i) {
        const analog_t *a = &s->IOs.analogs[i];
        len = snprintf(p, rem, "{\"enabled\":%s,\"name\":\"%s\",\"ratio\":%.3f,\"offset\":%.3f}%s",
                       a->enabled ? "true" : "false", a->name, a->ratio, a->offset,
                       (i < s->IOs.analog_count - 1) ? "," : "");
        p += len; rem -= len;
    }

    len = snprintf(p, rem, "],\"pio\":[");
    p += len; rem -= len;

    for (uint32_t i = 0; i < s->IOs.pio_count; ++i) {
        const pio_t *pio = &s->IOs.pios[i];
        len = snprintf(p, rem, "{\"enabled\":%s,\"name\":\"%s\",\"out\":%s}%s",
                       pio->enabled ? "true" : "false", pio->name,
                       pio->is_out ? "true" : "false",
                       (i < s->IOs.pio_count - 1) ? "," : "");
        p += len; rem -= len;
    }

    len = snprintf(p, rem, "]}}");
    p += len;
    return (int)(p - buf);
}

// ====================== FULL JSON DESERIALIZATION ======================
bool json_to_settings(const char *json, DeviceSettings *s) {
    if (!json || !s) return false;

    const char *p;

    // WiFi
    if ((p = strstr(json, "\"ssid\":\"")) != NULL)
        sscanf(p + 8, "%31[^\"]", s->wifi.ssid);
    if ((p = strstr(json, "\"password\":\"")) != NULL && strstr(p, "***") == NULL)
        sscanf(p + 12, "%63[^\"]", s->wifi.password);
    if ((p = strstr(json, "\"net_name\":\"")) != NULL)
        sscanf(p + 12, "%37[^\"]", s->wifi.network_name);

    // MQTT
    if ((p = strstr(json, "\"server\":\"")) != NULL)
        sscanf(p + 10, "%31[^\"]", s->mqtt.server);
    if ((p = strstr(json, "\"user\":\"")) != NULL)
        sscanf(p + 8, "%31[^\"]", s->mqtt.user);
    if ((p = strstr(json, "\"password\":\"")) != NULL && strstr(p, "***") == NULL)
        sscanf(p + 12, "%63[^\"]", s->mqtt.password);

    // Analogs
    const char *analog_start = strstr(json, "\"analog\":[");
    if (analog_start) {
        const char *a = analog_start + 10;
        for (uint32_t i = 0; i < s->IOs.analog_count && i < 8; ++i) {
            if ((p = strstr(a, "\"enabled\":")) != NULL)
                s->IOs.analogs[i].enabled = (strstr(p, "true") != NULL);
            if ((p = strstr(a, "\"name\":\"")) != NULL)
                sscanf(p + 8, "%31[^\"]", s->IOs.analogs[i].name);
            if ((p = strstr(a, "\"ratio\":")) != NULL)
                sscanf(p + 8, "%f", &s->IOs.analogs[i].ratio);
            if ((p = strstr(a, "\"offset\":")) != NULL)
                sscanf(p + 9, "%f", &s->IOs.analogs[i].offset);

            a = strstr(a, "}");
            if (a) a++;
        }
    }

    // PIOs
    const char *pio_start = strstr(json, "\"pio\":[");
    if (pio_start) {
        const char *a = pio_start + 7;
        for (uint32_t i = 0; i < s->IOs.pio_count && i < 20; ++i) {
            if ((p = strstr(a, "\"enabled\":")) != NULL)
                s->IOs.pios[i].enabled = (strstr(p, "true") != NULL);
            if ((p = strstr(a, "\"name\":\"")) != NULL)
                sscanf(p + 8, "%31[^\"]", s->IOs.pios[i].name);
            if ((p = strstr(a, "\"out\":")) != NULL)
                s->IOs.pios[i].is_out = (strstr(p, "true") != NULL);

            a = strstr(a, "}");
            if (a) a++;
        }
    }

    s->version = SETTINGS_VERSION;
    return true;
}

// History stubs (restore your original logic here if needed)
void save_voltage_log(const analog_log_t *log, DeviceSettings *settings) {
    // TODO: implement circular log in second flash sector if desired
    (void)log; (void)settings;
}

const analog_log_t *read_voltage_log(uint32_t position) {
    // TODO: implement
    return NULL;
}

bool get_bootsel_button_pressed(void) {
    return !gpio_get(25);
}

void factory_reset(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE * 4);
    restore_interrupts(ints);
    reboot_device();
}

void reboot_device(void) {
    watchdog_reboot(0, 0, 0);
}