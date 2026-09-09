#pragma once

void wifi_manager_init(void);
void wifi_connect_sta(const char *ssid, const char *pass);
bool wifi_is_sta_connected(void);
int wifi_get_rssi(void);
bool wifi_get_connected_ssid(char *ssid, size_t len);
void wifi_update_network_led(void);
void wifi_manager_start_sta(void);
extern const char *network_type;

void log_network_status(void);



