#pragma once

// --- flash_manager.h ---
#ifndef FLASH_MANAGER_H
#define FLASH_MANAGER_H
#include "cJSON.h"
#include <stdbool.h>

#define VERSION 24

//FYI - 640 sectors remain after FLASH_SETTINGS_OFFSET
#define FLASH_SETTINGS_OFFSET (1536 * 1024UL)
#define FLASH_SECTOR_SIZE 4096
#define FLASH_SETTING_SIZE (FLASH_SECTOR_SIZE * 2) // 2 sectors for settings (8KB)

cJSON *load_configuration(void);
void flash_save_settings();
int load_flash_buffer(char *json_str, size_t length,cJSON *config);

#endif /* FLASH_MANAGER_H */