#ifndef NETWORKING_H
#define NETWORKING_H

bool wifi_init(const char *ssid, const char *password, const char *network_name);
void wifi_poll();

#endif // NETWORKING_H