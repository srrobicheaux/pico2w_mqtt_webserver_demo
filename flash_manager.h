// --- flash_manager.h ---
#ifndef FLASH_MANAGER_H
#define FLASH_MANAGER_H
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>

#define VERSION 30

//FYI - 640 sectors remain after FLASH_SETTINGS_OFFSET
#define FLASH_SETTINGS_OFFSET (1536 * 1024UL)
#define FLASH_SECTOR_SIZE 4096
#define FLASH_SETTING_SIZE (FLASH_SECTOR_SIZE * 4) // 4 sectors for settings (16KB)

extern cJSON *g_config;
extern cJSON *g_pending_config;
extern volatile bool g_config_dirty;

void load_configuration(void);
int load_flash_buffer(char *json_str, size_t length);
bool flash_save_settings(cJSON *config_to_save);

#endif
/* FLASH_MANAGER_H */
