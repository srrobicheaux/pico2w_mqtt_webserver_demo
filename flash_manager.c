// --- flash_manager.c ---
#include "pico/stdlib.h"
#include <stdio.h>
#include "flash_manager.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "cJSON.h"
#include <string.h>
#include "pico/stdlib.h"
#include "build/default_config_json_embed.h"
#include "pico/unique_id.h"

void substitution(cJSON *root, int pin, const char *device_id)
{
    cJSON *item = NULL;
    char buffer[128];
    char pin_str[2]; // Buffer to hold the string representation of the pin

    // Pre-convert the int to a string for easier copying
    snprintf(pin_str, sizeof(pin_str), "%d", pin);

    cJSON_ArrayForEach(item, root)
    {
        if (cJSON_IsString(item) && item->valuestring)
        {
            const char *src = item->valuestring;
            char *dst = buffer;
            int replaced = 0;

            while (*src && (dst < buffer + sizeof(buffer) - 1))
            {
                // Check for %s
                if (device_id != NULL && strncmp(src, "%s", 2) == 0)
                {
                    if (dst + strlen(device_id) < buffer + sizeof(buffer) - 1)
                    {
                        strcpy(dst, device_id);
                        dst += strlen(device_id);
                        src += 2;
                        replaced = 1;
                    }
                    else
                        break;
                }
                // Check for %d
                else if (pin > -1 && strncmp(src, "%d", 2) == 0)
                {
                    if (dst + strlen(pin_str) < buffer + sizeof(buffer) - 1)
                    {
                        strcpy(dst, pin_str);
                        dst += strlen(pin_str);
                        src += 2;
                        replaced = 1;
                    }
                    else
                        break;
                }
                // Regular character
                else
                {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';

            if (replaced)
            {
                cJSON_SetValuestring(item, buffer);
            }
        }
    }
}

void flash_save_settings(cJSON *settings)
{
    static uint8_t big_flash_buffer[FLASH_SETTING_SIZE];
    memset(big_flash_buffer, 0, FLASH_SETTING_SIZE);
    if (cJSON_PrintPreallocated(settings, big_flash_buffer, FLASH_SETTING_SIZE - 1, false))
    {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(FLASH_SETTINGS_OFFSET, FLASH_SETTING_SIZE);
        flash_range_program(FLASH_SETTINGS_OFFSET, big_flash_buffer, FLASH_SETTING_SIZE);
        restore_interrupts(ints);
        printf("Saved: %s \t(length: %d)\n", settings->string, (int)strlen(big_flash_buffer));
    }
    else
    {
        printf("Failed to Save Settings: %s \t(length: %d)\n", settings->string, (int)strlen(big_flash_buffer));
    }
}

cJSON *load_configuration(void)
{
    const char *json = (const char *)(XIP_BASE + FLASH_SETTINGS_OFFSET);
    //    printf("Config:\n%.4000s\n",json);

    cJSON *root = cJSON_Parse(json);

    if (root)
    {
        int version = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "version"));
        if (version == VERSION || version == -1) // allow initialization versoin
        {
            char device_id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
            pico_get_unique_board_id_string(device_id, sizeof(device_id));
            if (version < 0)
            {
                cJSON_SetValuestring(cJSON_GetObjectItem(root, "device_id"), device_id);
                cJSON_SetNumberValue(cJSON_GetObjectItem(root, "version"), VERSION);
                version = VERSION;
            }
            printf("PICO:%s w/ Config Version:%d (length: %d)\n", device_id, version, (int)strlen(json));
            return root;
        }
        else
        {
            printf("Version mismatch: current-%d !=\t target-%d\n", version, VERSION);
        }
    }
    else
    {
        printf("Current Config is corrupted.\n");
    }

    printf("Performing factory reset.\n");
    root = cJSON_Parse(default_config_json);
    if (root)
    {
        flash_save_settings(root);
        int version = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "version"));
        printf("Config Version:%d (length: %d)\n", version, (int)strlen(json));
        return root;
    }
    printf("Failed to parse default JSON settings!\n");
    root = cJSON_Parse("{}");
    return root;
}

