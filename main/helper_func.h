#ifndef HELPER_FUNC_H
#define HELPER_FUNC_H

#include <stddef.h>   // for size_t
#include <time.h>
#include <stdbool.h>

extern volatile bool offline_upload_running;


typedef struct {
    uint32_t uid;
    uint32_t timestamp;
    char reader[10];
    char direction[5];
    char device_id[20];
} offline_log_t;

void data_parsing(const char *data, size_t data_len);
extern void send_uart_scan_to_server(
    const char *reader,
    uint32_t uid,
    const char *direction);
void upload_offline_logs(void);

uint32_t uid_to_decimal(const char *uid);


//void print_number(int num);   // if want to writ eone more function
#include "mqtt_client.h"   // required for esp_mqtt_client_handle_t
void erase_rfid_data_and_restart(esp_mqtt_client_handle_t client);


void rfid_add(const char *id);
void rfid_remove(const char *id);
void rfid_display_all(void);
bool rfid_exists(uint32_t id);
void save_offline_log(uint32_t uid, const char *reader, const char *direction);
void print_offline_logs(void);
void send_offline_log_to_server(const offline_log_t *log);
bool offline_logs_available(void);

void upload_offline_logs_task(void *pvParameters);


#endif
