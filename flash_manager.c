#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "flash_manager.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "cJSON.h"
#include "build/default_config_embed.h"
#include "pico/unique_id.h"

cJSON *g_config = NULL;
cJSON *g_pending_config = NULL;
volatile bool g_config_dirty = false;

static uint8_t flash_ram_buffer[FLASH_SETTING_SIZE] __attribute__((aligned(4)));

bool flash_save_settings(cJSON *config_to_save)
{
    if (!config_to_save) return false;

    memset(flash_ram_buffer, 0, FLASH_SETTING_SIZE);

    if (!cJSON_PrintPreallocated(config_to_save, (char *)flash_ram_buffer, FLASH_SETTING_SIZE - 1, false))
    {
        printf("Error: Config JSON exceeds FLASH_SETTING_SIZE!\n");
        return false;
    }

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_SETTINGS_OFFSET, FLASH_SETTING_SIZE);
    flash_range_program(FLASH_SETTINGS_OFFSET, flash_ram_buffer, FLASH_SETTING_SIZE);
    restore_interrupts(ints);

    printf("Saved Settings to Flash (%d bytes used)\n", strlen((char *)flash_ram_buffer));
    return true;
}

int load_flash_buffer(char *json_str, size_t length)
{
    cJSON *new_values = cJSON_ParseWithLength(json_str, length);
    if (new_values)
    {
        // Preserve passwords if placeholder "*****" was submitted
        if (g_config) {
            cJSON *new_wifi_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(new_values, "wifi"), "password");
            cJSON *old_wifi_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "wifi"), "password");
            if (new_wifi_pw && old_wifi_pw && strncmp(cJSON_GetStringValue(new_wifi_pw), "*****", 5) == 0)
            {
                cJSON_SetValuestring(new_wifi_pw, cJSON_GetStringValue(old_wifi_pw));
            }

            cJSON *new_mqtt_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(new_values, "mqtt"), "password");
            cJSON *old_mqtt_pw = cJSON_GetObjectItem(cJSON_GetObjectItem(g_config, "mqtt"), "password");
            if (new_mqtt_pw && old_mqtt_pw && strncmp(cJSON_GetStringValue(new_mqtt_pw), "*****", 5) == 0)
            {
                cJSON_SetValuestring(new_mqtt_pw, cJSON_GetStringValue(old_mqtt_pw));
            }
        }

        // Clean up any unapplied pending tree and stage new tree
        if (g_pending_config) {
            cJSON_Delete(g_pending_config);
        }
        g_pending_config = new_values;
        g_config_dirty = true;
        return 0;
    }
    else
    {
        const char *err_ptr = cJSON_GetErrorPtr();
        return err_ptr ? (err_ptr - json_str + 1) : 1;
    }
}

void load_configuration(void)
{
    if (g_config) { cJSON_Delete(g_config); g_config = NULL; }
    if (g_pending_config) { cJSON_Delete(g_pending_config); g_pending_config = NULL; }

    const char *json = (const char *)(XIP_BASE + FLASH_SETTINGS_OFFSET);
    g_config = cJSON_ParseWithLength(json, FLASH_SETTING_SIZE); 
    
    if (g_config)
    {
        cJSON *version_item = cJSON_GetObjectItem(g_config, "version");
        int version = version_item ? (int)cJSON_GetNumberValue(version_item) : -99;
        
        if (version == VERSION || version == -1) 
        {
            char device_id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
            pico_get_unique_board_id_string(device_id, sizeof(device_id));
            if (version < 0)
            {
                cJSON_SetValuestring(cJSON_GetObjectItem(g_config, "device_id"), device_id);
                cJSON_SetNumberValue(version_item, VERSION);
            }
            printf("PICO:%s w/ Config Version:%d\n", device_id, version);
            return;
        }
    }

    // Factory Reset Fallback
    printf("Performing factory reset.\n");
    if (g_config) cJSON_Delete(g_config);

    g_config = cJSON_ParseWithLength((const char *)default_config_embed, default_config_embed_len);
    if (g_config)
    {
        flash_save_settings(g_config);
    }
}