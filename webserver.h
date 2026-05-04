// webserver.h
#ifndef WEBSERVER_H
#define WEBSERVER_H
#include "flash.h"

/**
 * Initializes the web server (binds/listens on port 80).
 * Call once after WiFi connection is established.
 * 
 * @return true on success, false on failure.
 */
bool webserver_init(bool _provisioning);
void webserver_push_update(const char *topic, const char *json_payload) ;
#endif // WEBSERVER_H