#ifndef NETWORKING_H
#define NETWORKING_H
#include "cJSON.h"

typedef enum 
{
    WIFI_NOT_INITIALIZE = -4,
    WIFI_SCANNING = 0,
    WIFI_SCANED = 1,
    WIFI_CONNECTING = 2,
    WIFI_CONNECTED = 3,
    WIFI_FOUND = 4,
    WIFI_DISCONNECTED =5,
    WIFI_AP_STARTING = -1,
    WIFI_AP = -2,
    WIFI_ERROR = -3
} wifi_mode;

//void wifi_Connect(cJSON *wifi);
wifi_mode wifi_poll(wifi_mode mode, cJSON *networks);
int32_t get_wifi_rssi(void);

#endif // NETWORKING_H