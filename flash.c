#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/unique_id.h"
#include "pico/cyw43_arch.h"
#include "flash.h"

// Bump this to 2 to force the Pico to overwrite the old flash memory!
#define SETTINGS_VERSION     0x0005  
#define FLASH_TARGET_OFFSET  (1536 * 1024)
#define FLASH_HISTORY_OFFSET (FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE * 3)
#define MAX_POSITIONS        10000     // ~28 hours at 5s logging

//const DeviceSettings *flash_data = (const DeviceSettings *)(XIP_BASE + FLASH_TARGET_OFFSET);
const DeviceSettings *flash_data = (DeviceSettings *)(XIP_BASE + FLASH_TARGET_OFFSET);

// ================== SETTINGS ==================
bool load_settings(DeviceSettings *local)
{
    char id_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES *2 +1];
    pico_get_unique_board_id_string(id_str, sizeof(id_str));
    // FIX: Now we also check if the version matches!
    if (strcmp(flash_data->wifi.device_id, id_str) != 0 || flash_data->version != SETTINGS_VERSION)
    {
        printf("No valid settings for %s.\n", id_str);
        memset(local,0, sizeof(DeviceSettings));
        strcpy(local->wifi.device_id, id_str);
        local->version = SETTINGS_VERSION;
        local->position = 0;
        local->IOs.analog_count=3;
        local->IOs.pio_count=12;
        local->wifi.ssid[0]='\0';
    }
    else
    {
        memcpy(local, flash_data, sizeof(DeviceSettings));
        printf("Found settings for this Board ID:\t%18s\n", id_str, flash_data->wifi.device_id);
    }

    return true;
}

void save_settings(DeviceSettings *local, size_t len)
{
    const int flash_size = ((len  % FLASH_PAGE_SIZE) +1) * FLASH_PAGE_SIZE;
    uint8_t buffer[flash_size];
    memcpy(buffer, local, len);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(FLASH_TARGET_OFFSET, buffer, sizeof(buffer));
    restore_interrupts(ints);
	
    printf("Settings saved\n");
}

void save_settings_to_flash(const char* json_buffer) {
    // 1. Prepare a buffer aligned to 4096 bytes (1 sector)
    uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    memset(sector_buffer, 0, FLASH_SECTOR_SIZE);
    
    // 2. Copy the JSON string into the buffer 
    // (Ensure it doesn't exceed 4096 bytes)
    strncpy((char*)sector_buffer, json_buffer, FLASH_SECTOR_SIZE - 1);

    // 3. Flash operations MUST be done with interrupts disabled
    uint32_t ints = save_and_disable_interrupts();
    
    // Erase the sector first
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    
    // Write the data (must be a multiple of FLASH_PAGE_SIZE, which is 256)
    flash_range_program(FLASH_TARGET_OFFSET, sector_buffer, FLASH_SECTOR_SIZE);
    
    restore_interrupts(ints);
}


// ================== VOLTAGE LOGGING (circular) ==================
void save_voltage_log(const analog_log_t *log, DeviceSettings *settings)
{
    uint8_t buffer[FLASH_PAGE_SIZE];
    memset(buffer, 0xFF, sizeof(buffer));
    memcpy(buffer, log, sizeof(analog_log_t));

    uint32_t ints = save_and_disable_interrupts();

    // Erase sector every 16 entries
    if ((flash_data->position % 16) == 0) {
        uint32_t sector = flash_data->position / 16;
        flash_range_erase(FLASH_HISTORY_OFFSET + sector * FLASH_SECTOR_SIZE+ FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
    }

    uint32_t addr = FLASH_HISTORY_OFFSET + flash_data->position * FLASH_PAGE_SIZE;
    flash_range_program(addr, buffer, FLASH_PAGE_SIZE);

    restore_interrupts(ints);

    settings->position = (settings->position + 1) % MAX_POSITIONS;
    if (settings->position == 0) {
        printf("Voltage history wrapped around (circular buffer)\n");
    }
}

const analog_log_t *read_voltage_log(uint32_t position)
{
    if (position >= MAX_POSITIONS) position = 0;
    return (const analog_log_t *)(XIP_BASE + FLASH_HISTORY_OFFSET + position * FLASH_PAGE_SIZE);
}

// ================== HARDWARE HELPERS (fixes your linker errors) ==================
#define RESET_TIME 15000000
