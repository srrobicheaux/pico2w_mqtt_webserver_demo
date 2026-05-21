#include <string.h>
#include <stdio.h>
// https://github.com/DaveGamble/cJSON/blob/master/cJSON.h
#include "cJSON.h"
#include "hardware/flash.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#include "pico/unique_id.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "flash.h"
#include "secrets.h"

// 3. Calculate required flash page alignment (multiple of 256 bytes)
const size_t write_size = ((sizeof(DeviceSettings) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;

static const DeviceSettings *flash_settings = (const DeviceSettings *)(XIP_BASE + FLASH_TARGET_OFFSET);

static uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

bool load_settings(DeviceSettings *local)
{
    char id_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(id_str, sizeof(id_str));
    uint32_t crc = crc32((const uint8_t *)flash_settings, offsetof(DeviceSettings, crc32));

    if (flash_settings->version != SETTINGS_VERSION ||
        strcmp(flash_settings->wifi.device_id, id_str) != 0 ||
        flash_settings->crc32 != crc)
    {
        printf("Invalid settings in flash for board %s: Version: 0x%04X, CRC: %08X\n", flash_settings->wifi.device_id, flash_settings->version, flash_settings->crc32);
        printf("                          Expected: %s: Version: 0x%04X, CRC: %08X\n", id_str, SETTINGS_VERSION, crc);
        printf("Using Defaults!\n");

        memset(local, 0, sizeof(DeviceSettings));
        strcpy(local->wifi.device_id, id_str);
        local->version = SETTINGS_VERSION;
        local->position = 0;
        local->IOs.analog_count = 3;
        local->IOs.pio_count = 20;
        snprintf(local->wifi.network_name, sizeof(local->wifi.network_name) - 1, "pico%5s", id_str);
        strncpy(local->mqtt.model, "Pico 2W", 32);
        strncpy(local->mqtt.version, "1.0.4", 32);
        strncpy(local->mqtt.server, "rpi4b.lan", 32);
        strncpy(local->mqtt.user, MQTT_USER, 32);
        strncpy(local->mqtt.password, MQTT_PW, 64);

        strncpy(local->wifi.ssid, SSID, 32);
        strncpy(local->wifi.password, SSID_PW, 64);

        for (int i = 0; i < local->IOs.analog_count; ++i)
        {
            snprintf(local->IOs.analogs[i].name, sizeof(local->IOs.analogs[i].name) - 1, "Analog %d", i + 1);
            local->IOs.analogs[i].enabled = false;
            local->IOs.analogs[i].ratio = 1.0f;
            local->IOs.analogs[i].offset = 0.0f;
        }
        for (int i = 0; i < local->IOs.pio_count; ++i)
        {
            snprintf(local->IOs.pios[i].name, sizeof(local->IOs.pios[i].name), "PIO %d", i);
            local->IOs.pios[i].enabled = false;
            local->IOs.pios[i].is_out = false;
        }
        return false;
    }

    memcpy(local, flash_settings, sizeof(DeviceSettings));
    printf("Settings loaded successfully for board %s\n", id_str);

    return true;
}

bool save_settings(const DeviceSettings *settings)
{
    // 1. Create a local copy to manipulate
    DeviceSettings copy = *settings;
    char id_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(id_str, sizeof(id_str));
    strncpy(copy.wifi.device_id, id_str, sizeof(copy.wifi.device_id) - 1);

    // 2. Calculate the CRC
    copy.crc32 = crc32((const uint8_t *)&copy, offsetof(DeviceSettings, crc32));

    // 4. Allocate the aligned buffer and fill with 0xFF (erased flash state)
    uint8_t write_buffer[write_size];
    memset(write_buffer, 0xFF, write_size);

    // 5. Copy our struct into the beginning of the padded buffer
    memcpy(write_buffer, &copy, sizeof(DeviceSettings));

    // 6. Disable interrupts and perform the flash operations
    uint32_t ints = save_and_disable_interrupts();

    //    cyw43_arch_lwip_begin(); // Ensure lwIP is not accessing flash during our operation
    // Note: FLASH_SECTOR_SIZE is 4096 bytes. Erase 1 sector unless your struct is > 4KB.
    flash_range_erase(FLASH_TARGET_OFFSET, write_size);

    // Program the perfectly aligned buffer
    flash_range_program(FLASH_TARGET_OFFSET, write_buffer, write_size);
    //    cyw43_arch_lwip_end(); // Release the lwIP lock
    restore_interrupts(ints);

    // 7. Verify the flash write was successful (fixed syntax here)
    uint32_t read_back_crc = crc32((const uint8_t *)flash_settings, offsetof(DeviceSettings, crc32));
    if (read_back_crc != copy.crc32)
    {
        printf("Error: CRC mismatch after saving settings! Expected: %08X, Got: %08X\n", copy.crc32, read_back_crc);
        return false;
    }

    printf("Settings saved (version 0x%04X)\n", copy.version);
    return true;
}

// ====================== SAFE STRING COPY ======================
static void safe_strcpy(char *dest, size_t dest_size, const char *src)
{
    if (src && dest && dest_size > 0)
    {
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
    else if (dest && dest_size > 0)
    {
        dest[0] = '\0';
    }
}

// ====================== JSON → SETTINGS ======================
bool json_to_settings(const char *json_str, DeviceSettings *s)
{
    if (!json_str || !s)
        return false;

    cJSON *root = cJSON_Parse(json_str);
    if (!root)
        return false;

    cJSON *item;

    // Top-level
    item = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsNumber(item))
        s->version = (uint16_t)cJSON_GetNumberValue(item);

    item = cJSON_GetObjectItem(root, "position");
    if (cJSON_IsNumber(item))
        s->position = (uint32_t)cJSON_GetNumberValue(item);

    // WiFi
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (cJSON_IsObject(wifi))
    {
        item = cJSON_GetObjectItem(wifi, "ssid");
        if (cJSON_IsString(item))
            safe_strcpy(s->wifi.ssid, sizeof(s->wifi.ssid), item->valuestring);

        item = cJSON_GetObjectItem(wifi, "password");
        if (cJSON_IsString(item) && strcmp(item->valuestring, "***") != 0)
            safe_strcpy(s->wifi.password, sizeof(s->wifi.password), item->valuestring);

        item = cJSON_GetObjectItem(wifi, "net_name");
        if (cJSON_IsString(item))
            safe_strcpy(s->wifi.network_name, sizeof(s->wifi.network_name), item->valuestring);

        item = cJSON_GetObjectItem(wifi, "dev_id");
        if (cJSON_IsString(item))
            safe_strcpy(s->wifi.device_id, sizeof(s->wifi.device_id), item->valuestring);
    }

    // MQTT
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt))
    {
        item = cJSON_GetObjectItem(mqtt, "server");
        if (cJSON_IsString(item))
            safe_strcpy(s->mqtt.server, sizeof(s->mqtt.server), item->valuestring);

        item = cJSON_GetObjectItem(mqtt, "user");
        if (cJSON_IsString(item))
            safe_strcpy(s->mqtt.user, sizeof(s->mqtt.user), item->valuestring);

        item = cJSON_GetObjectItem(mqtt, "password");
        if (cJSON_IsString(item) && strcmp(item->valuestring, "***") != 0)
            safe_strcpy(s->mqtt.password, sizeof(s->mqtt.password), item->valuestring);

        item = cJSON_GetObjectItem(mqtt, "model");
        if (cJSON_IsString(item))
            safe_strcpy(s->mqtt.model, sizeof(s->mqtt.model), item->valuestring);

        item = cJSON_GetObjectItem(mqtt, "version");
        if (cJSON_IsString(item))
            safe_strcpy(s->mqtt.version, sizeof(s->mqtt.version), item->valuestring);
    }

    // IOs
    cJSON *ios = cJSON_GetObjectItem(root, "IOs");
    if (cJSON_IsObject(ios))
    {
        item = cJSON_GetObjectItem(ios, "analog_count");
        if (cJSON_IsNumber(item))
            s->IOs.analog_count = (size_t)cJSON_GetNumberValue(item);

        item = cJSON_GetObjectItem(ios, "pio_count");
        if (cJSON_IsNumber(item))
            s->IOs.pio_count = (size_t)cJSON_GetNumberValue(item);

        // Analogs
        cJSON *analogs = cJSON_GetObjectItem(ios, "analog");
        if (cJSON_IsArray(analogs))
        {
            size_t count = cJSON_GetArraySize(analogs);
            for (size_t i = 0; i < count && i < 8; ++i)
            {
                cJSON *obj = cJSON_GetArrayItem(analogs, i);
                if (!cJSON_IsObject(obj))
                    continue;

                item = cJSON_GetObjectItem(obj, "enabled");
                if (cJSON_IsBool(item))
                    s->IOs.analogs[i].enabled = cJSON_IsTrue(item);

                item = cJSON_GetObjectItem(obj, "name");
                if (cJSON_IsString(item))
                    safe_strcpy(s->IOs.analogs[i].name, sizeof(s->IOs.analogs[i].name), item->valuestring);

                item = cJSON_GetObjectItem(obj, "ratio");
                if (cJSON_IsNumber(item))
                    s->IOs.analogs[i].ratio = (float)cJSON_GetNumberValue(item);

                item = cJSON_GetObjectItem(obj, "offset");
                if (cJSON_IsNumber(item))
                    s->IOs.analogs[i].offset = (float)cJSON_GetNumberValue(item);
            }
        }

        // PIOs
        cJSON *pios = cJSON_GetObjectItem(ios, "pio");
        if (cJSON_IsArray(pios))
        {
            size_t count = cJSON_GetArraySize(pios);
            for (size_t i = 0; i < count && i < 20; ++i)
            {
                cJSON *obj = cJSON_GetArrayItem(pios, i);
                if (!cJSON_IsObject(obj))
                    continue;

                item = cJSON_GetObjectItem(obj, "enabled");
                if (cJSON_IsBool(item))
                    s->IOs.pios[i].enabled = cJSON_IsTrue(item);

                item = cJSON_GetObjectItem(obj, "name");
                if (cJSON_IsString(item))
                    safe_strcpy(s->IOs.pios[i].name, sizeof(s->IOs.pios[i].name), item->valuestring);

                item = cJSON_GetObjectItem(obj, "out");
                if (cJSON_IsBool(item))
                    s->IOs.pios[i].is_out = cJSON_IsTrue(item);
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

// ====================== SETTINGS → JSON (with Compact Option) ======================
int settings_to_json(const DeviceSettings *s, char *buf, size_t bufsize, bool compact)
{
    if (!s || !buf || bufsize == 0)
        return -1;

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON_AddNumberToObject(root, "version", s->version);
    cJSON_AddNumberToObject(root, "position", s->position);

    // WiFi
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ssid", s->wifi.ssid);
    cJSON_AddStringToObject(wifi, "password", "***");
    cJSON_AddStringToObject(wifi, "net_name", s->wifi.network_name);
    cJSON_AddStringToObject(wifi, "dev_id", s->wifi.device_id);
    cJSON_AddItemToObject(root, "wifi", wifi);

    // MQTT
    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddStringToObject(mqtt, "server", s->mqtt.server);
    cJSON_AddStringToObject(mqtt, "user", s->mqtt.user);
    cJSON_AddStringToObject(mqtt, "password", "***");
    cJSON_AddStringToObject(mqtt, "model", s->mqtt.model);
    cJSON_AddStringToObject(mqtt, "version", s->mqtt.version);
    cJSON_AddItemToObject(root, "mqtt", mqtt);

    // IOs
    cJSON *ios = cJSON_CreateObject();
    cJSON_AddNumberToObject(ios, "analog_count", (double)s->IOs.analog_count);
    cJSON_AddNumberToObject(ios, "pio_count", (double)s->IOs.pio_count);

    // Analogs array
    cJSON *analogs = cJSON_CreateArray();
    for (size_t i = 0; i < s->IOs.analog_count && i < 8; ++i)
    {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddBoolToObject(a, "enabled", s->IOs.analogs[i].enabled);
        cJSON_AddStringToObject(a, "name", s->IOs.analogs[i].name);
        cJSON_AddNumberToObject(a, "ratio", s->IOs.analogs[i].ratio);
        cJSON_AddNumberToObject(a, "offset", s->IOs.analogs[i].offset);
        cJSON_AddItemToArray(analogs, a);
    }
    cJSON_AddItemToObject(ios, "analog", analogs);

    // PIOs array
    cJSON *pios = cJSON_CreateArray();
    for (size_t i = 0; i < s->IOs.pio_count && i < 20; ++i)
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddBoolToObject(p, "enabled", s->IOs.pios[i].enabled);
        cJSON_AddStringToObject(p, "name", s->IOs.pios[i].name);
        cJSON_AddBoolToObject(p, "out", s->IOs.pios[i].is_out);
        cJSON_AddItemToArray(pios, p);
    }
    cJSON_AddItemToObject(ios, "pio", pios);

    cJSON_AddItemToObject(root, "IOs", ios);

    // Generate JSON string
    char *json_str = compact ? cJSON_PrintUnformatted(root) : cJSON_Print(root);

    if (!json_str)
    {
        cJSON_Delete(root);
        return -1;
    }

    strncpy(buf, json_str, bufsize - 1);
    buf[bufsize - 1] = '\0';

    cJSON_free(json_str);
    cJSON_Delete(root);

    return (int)strlen(buf);
}

int settings_to_json_compact(char *buf, size_t bufsize)
{
    return settings_to_json(flash_settings, buf, bufsize, true);
}

int settings_to_json_pretty(char *buf, size_t bufsize)
{
    return settings_to_json(flash_settings, buf, bufsize, false);
}

// History stubs (restore your original logic here if needed)
void save_voltage_log(const analog_log_t *log, DeviceSettings *settings)
{
    // TODO: implement circular log in second flash sector if desired
    (void)log;
    (void)settings;
}

const analog_log_t *read_voltage_log(uint32_t position)
{
    // TODO: implement
    return NULL;
}

bool get_bootsel_button_pressed(void)
{
    //    return !gpio_get(25);

    printf("not implemeted yet!");

    return false; // Stub for now, implement actual button reading logic
}

void factory_reset(void)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE * 2);
    restore_interrupts(ints);
    reboot_device();
}

void reboot_device(void)
{
    watchdog_reboot(0, 0, 0);
}