// --- io_manager.h ---
#ifndef IO_MANAGER_H
#define IO_MANAGER_H
#include <stdbool.h>
#include "cJSON.h"

bool toggle_pin(int pin);
bool get_pin(int pin);
void io_init_all(cJSON *channels);
cJSON * system_channel_update();
cJSON *channel_updates(cJSON *channels);
bool poll_bootsel_button() ;
#endif /* IO_MANAGER_H */