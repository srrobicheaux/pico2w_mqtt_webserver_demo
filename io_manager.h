// --- io_manager.h ---
#ifndef IO_MANAGER_H
#define IO_MANAGER_H
#include <stdbool.h>
#include "cJSON.h"

typedef enum channel_type
{
    DIGITAL = 0,
    ANALOG = 1,
    RAM = 2,
    FLASH = 3,
    TEMP = 4,
    UPTIME = 5
} channel_type_t;

//bool toggle_pin(int pin);
//bool get_pin(int pin);
void io_init_all(cJSON *channels);

bool channel_updates(cJSON *channels);
bool poll_bootsel_button() ;
#endif /* IO_MANAGER_H */