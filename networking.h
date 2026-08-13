#ifndef NETWORKING_H
#define NETWORKING_H
#include "cJSON.h"

typedef enum 
{
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


void wifi_Connect(cJSON *wifi);
wifi_mode wifi_poll();
bool start_wifi_scan(cJSON *networks) ;
void AP_Start();

#endif // NETWORKING_H