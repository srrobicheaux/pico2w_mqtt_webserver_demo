#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include <stddef.h>

void publish_temperature(char * payload); 
void send_system_status_event(char *payload);

typedef enum {
    GPIO_ACTION_READ = 0,
    GPIO_ACTION_TOGGLE = 1,
    GPIO_ACTION_PRESS = 2
} gpio_action_t;

bool pin_action(int pio, gpio_action_t action);
void reset();


void time_manager_init();

// Get the current formatted time
void get_current_time_str(char *buffer, size_t max_len);
// Get full ISO timestamp for MQTT (e.g., 2024-05-24T12:00:00)
void get_iso_timestamp(char *buffer, size_t max_len) ;

#endif