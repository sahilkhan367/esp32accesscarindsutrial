#include <stdio.h>
#include <string.h>
#include <stddef.h>   // for size_t
#include <stdint.h> //for rfid conversion
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_system.h" //for esp32 reset
#include "mqtt_client.h"
#include "helper_func.h"
#include "esp_log.h"
#include "esp_system.h"
#include <time.h>

static const char *TAG = "HELPER";


extern esp_mqtt_client_handle_t mqtt_client;


void rfid_add(const char *id);
void rfid_remove(const char *id);
void rfid_display_all(void);
bool rfid_exists(uint32_t id);
void bulk_add_parse_and_store(const char *data, size_t len);
void bulk_rm_parse_and_remove(const char *data, size_t len);
void bulk_add_task(void *param);
void bulk_rm_task(void *param);
void erase_rfid_data_and_restart(esp_mqtt_client_handle_t client);
void save_offline_log(uint32_t uid, const char *reader, const char *direction);
void print_offline_logs(void);
void upload_offline_logs(void);
void send_offline_log_to_server(const offline_log_t *log);



#define RFID_NAMESPACE "rfid_db"
extern const char *DEVICE_ID;

#define KEY_MAX_LEN     16
#define VALUE_MAX_LEN   32


int contains_keyword(const char *data, size_t len, const char *key) {
    size_t klen = strlen(key);

    if (len < klen) return 0;

    for (size_t i = 0; i <= len - klen; i++) {
        if (memcmp(&data[i], key, klen) == 0) {
            return 1;
        }
    }
    return 0;
}


void data_parsing(const char *data, size_t data_len)
{
    char key[KEY_MAX_LEN + 1] = {0};
    char value[VALUE_MAX_LEN + 1] = {0};

    if (data == NULL || data_len == 0) {
        //printf("ERROR: Invalid input\n");
        return;
    
    printf("RAW: %.*s\n", data_len, data);

    }if (contains_keyword(data, data_len, "bulk_add") ||
        contains_keyword(data, data_len, "BULK_ADD")) {
        printf("BULK ADD FOUND\n");
        // bulk_add_parse_and_store(data, data_len);
        char *copy = malloc(data_len + 1);
        memcpy(copy, data, data_len);
        copy[data_len] = '\0';
        xTaskCreate(bulk_add_task, "bulk_add_task", 8192, copy, 5, NULL);
        printf("Completed BULK ADD\n");
        // call your bulk add function here
        return;
    }

    if (contains_keyword(data, data_len, "bulk_rm") ||
        contains_keyword(data, data_len, "BULK_RM")) {
        printf("BULK RM FOUND\n");
        //bulk_rm_parse_and_remove(data, data_len);
        char *copy = malloc(data_len + 1);
        memcpy(copy, data, data_len);
        copy[data_len] = '\0';
        xTaskCreate(bulk_rm_task, "bulk_rm_task", 8192, copy, 5, NULL);
        printf("Completed BULK RM\n");
        // call your bulk remove function here
        return;
    }

    /* ---------- Find ':' safely ---------- */
    const char *colon = NULL;
    for (size_t i = 0; i < data_len; i++) {
        if (data[i] == ':') {
            colon = &data[i];
            break;
        }
    }

    if (colon == NULL) {
        //printf("ERROR: Missing ':'\n");
        return;
    }

    /* ---------- KEY ---------- */
    size_t key_len = colon - data;
    if (key_len == 0 || key_len > KEY_MAX_LEN) {
        //printf("ERROR: Invalid key length\n");
        return;
    }

    memcpy(key, data, key_len);
    key[key_len] = '\0';

    /* ---------- VALUE ---------- */
    size_t value_len = data_len - (key_len + 1);
    if (value_len == 0 || value_len > VALUE_MAX_LEN) {
        //printf("ERROR: Invalid value length\n");
        return;
    }

    memcpy(value, colon + 1, value_len);
    value[value_len] = '\0';

    /* ---------- OUTPUT ---------- */
    printf("KEY   = %s\n", key);
    printf("VALUE = %s\n", value);

    if (strcmp(key, "ADD") == 0) {
    rfid_add(value);
    }
    else if (strcmp(key, "RM") == 0) {
    rfid_remove(value);
    }
    else if (strcmp(key, "DISPLAY") == 0 &&
    strcmp(value, "DATA") == 0) {
    rfid_display_all();
    }
    else if (strcmp(key, "RESET") == 0 &&
    strcmp(value, "RESET") == 0) {
    esp_restart();
    }
}



void rfid_add(const char *id)
{
    nvs_handle_t nvs;
    if (nvs_open(RFID_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        //printf("NVS open failed\n");
        return;
    }

    uint8_t value = 1;
    esp_err_t err = nvs_set_u8(nvs, id, value);

    if (err == ESP_OK) {
        nvs_commit(nvs);
        //printf("RFID ADDED: %s\n", id);
    } else {
        //printf("ADD FAILED\n");
    }

    nvs_close(nvs);
}




void rfid_remove(const char *id)
{
    nvs_handle_t nvs;
    if (nvs_open(RFID_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        //printf("NVS open failed\n");
        return;
    }

    esp_err_t err = nvs_erase_key(nvs, id);

    if (err == ESP_OK) {
        nvs_commit(nvs);
        //printf("RFID REMOVED: %s\n", id);
    } else {
        //printf("RFID NOT FOUND\n");
    }

    nvs_close(nvs);
}



void rfid_display_all(void)
{
    nvs_iterator_t it = NULL;
    esp_err_t err;

    printf("---- STORED RFID CARDS ----\n");

    err = nvs_entry_find("nvs", RFID_NAMESPACE, NVS_TYPE_U8, &it);
    while (err == ESP_OK && it != NULL) {

        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        printf("%s\n", info.key);

        err = nvs_entry_next(&it);
    }
}




bool rfid_exists(uint32_t id)
{
    nvs_handle_t nvs;
    uint8_t value;

    if (nvs_open(RFID_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    char key[16];
    snprintf(key, sizeof(key), "%lu", (unsigned long)id);

    esp_err_t err = nvs_get_u8(nvs, key, &value);
    nvs_close(nvs);

    return (err == ESP_OK);
}




//============= CARD CONVERSION =============

static uint8_t hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

uint32_t uid_to_decimal(const char *uid)
{
    size_t len = strlen(uid);
    uint32_t value = 0;

    // Use first 3 bytes of the last 4 bytes (6 hex chars)
    for (int i = (int)len - 8; i < (int)len - 2; i++) {
        value = (value << 4) | hex_char_to_val(uid[i]);
    }

    return value;   // return decimal value directly
}

void bulk_add_task(void *param)
{
    char *data = (char *)param;
    size_t len = strlen(data);

    bulk_add_parse_and_store(data, len);

    free(data);   // IMPORTANT
    vTaskDelete(NULL);
}

void bulk_add_parse_and_store(const char *data, size_t len)
{
    const char *start = NULL;
    const char *end = NULL;

    /* ---------- Find { and } ---------- */
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '{') start = &data[i + 1];
        if (data[i] == '}') {
            end = &data[i];
            break;
        }
    }

    if (!start || !end || start >= end) {
        printf("Invalid BULK format\n");
        return;
    }

    printf("Starting BULK ADD...\n");

    char id[32];
    int idx = 0;

    for (const char *p = start; p <= end; p++) {

        if (*p == ',' || p == end) {

            id[idx] = '\0';

            /* ---------- Trim ---------- */
            char *clean = id;

            // remove leading spaces
            while (*clean == ' ') clean++;

            // remove trailing spaces + braces
            int len = strlen(clean);
            while (len > 0 && 
                  (clean[len-1] == ' ' || 
                   clean[len-1] == '}' || 
                   clean[len-1] == '{')) {
                clean[len-1] = '\0';
                len--;
            }

            /* ---------- Store ---------- */
            if (len > 0) {
                printf("Adding ID: %s\n", clean);
                rfid_add(clean);
            }

            idx = 0;
        }
        else {
            if (idx < sizeof(id) - 1) {
                id[idx++] = *p;
            }
        }
    }
    char topic[64];

    snprintf(topic, sizeof(topic), "esp32/ack_bulk/%s", DEVICE_ID);


    esp_mqtt_client_publish(
    mqtt_client,
    topic,
    "{\"status\":\"completed\",\"cmd\":\"bulk_add\"}",
    0,
    1,
    0
    );
    printf("Completed BULK ADD\n");
    }



void bulk_rm_task(void *param)
{
    char *data = (char *)param;
    size_t len = strlen(data);

    bulk_rm_parse_and_remove(data, len);

    free(data);
    vTaskDelete(NULL);
}

void bulk_rm_parse_and_remove(const char *data, size_t len)
{
    const char *start = NULL;
    const char *end = NULL;

    /* ---------- Find { and } ---------- */
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '{') start = &data[i + 1];
        if (data[i] == '}') {
            end = &data[i];
            break;
        }
    }

    if (!start || !end || start >= end) {
        printf("Invalid BULK RM format\n");
        return;
    }

    printf("Starting BULK RM...\n");

    char id[32];
    int idx = 0;

    for (const char *p = start; p <= end; p++) {

        if (*p == ',' || p == end) {

            /* ❌ DO NOT add '}' */
            id[idx] = '\0';

            /* ---------- Trim ---------- */
            char *clean = id;

            // remove leading spaces
            while (*clean == ' ') clean++;

            // remove trailing spaces + braces
            int len = strlen(clean);
            while (len > 0 &&
                  (clean[len-1] == ' ' ||
                   clean[len-1] == '}' ||
                   clean[len-1] == '{')) {
                clean[len-1] = '\0';
                len--;
            }

            /* ---------- Remove ---------- */
            if (len > 0) {
                printf("Removing ID: %s\n", clean);
                rfid_remove(clean);
            }

            idx = 0;
        }
        else {
            if (idx < sizeof(id) - 1) {
                id[idx++] = *p;
            }
        }
    }
    //-----------
    char topic[64];

    snprintf(topic, sizeof(topic), "esp32/ack_bulk/%s", DEVICE_ID);
    esp_mqtt_client_publish(
    mqtt_client,
    topic,
    "{\"status\":\"completed\",\"cmd\":\"bulk_rm\"}",
    0,
    1,
    0
    );
    //-------------

    printf("Completed BULK RM\n");
}


//=============== CARD conversion END ================




void erase_rfid_data_and_restart(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "STEP 1: Function entered");

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(RFID_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "STEP 2: Failed to open NVS");
        return;
    }

    ESP_LOGI(TAG, "STEP 3: NVS opened");

    nvs_erase_all(nvs);
    ESP_LOGI(TAG, "STEP 4: NVS erased");

    nvs_commit(nvs);
    ESP_LOGI(TAG, "STEP 5: NVS committed");

    nvs_close(nvs);

    ESP_LOGI(TAG, "STEP 6: Sending ACK");

    char ack_topic[64];
    char ack_msg[128];

    snprintf(ack_topic, sizeof(ack_topic), "esp32/ack_bulk_RM_ALL/%s", DEVICE_ID);

    snprintf(ack_msg, sizeof(ack_msg),
             "{\"device_id\":\"%s\",\"status\":\"success\"}",
             DEVICE_ID);

    esp_mqtt_client_publish(client, ack_topic, ack_msg, strlen(ack_msg), 1, 0);

    ESP_LOGI(TAG, "STEP 7: ACK sent");

    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "STEP 8: Restarting");

    // esp_restart();
}

//=============================offline logs=========================================

void save_offline_log(uint32_t uid,
                      const char *reader,
                      const char *direction)
{
    nvs_handle_t handle;

    if (nvs_open("offline_logs", NVS_READWRITE, &handle) != ESP_OK)
        return;

    uint32_t tail = 0;

    nvs_get_u32(handle, "tail", &tail);

    char key[20];
    snprintf(key,
             sizeof(key),
             "log_%lu",
             (unsigned long)tail);

    offline_log_t log;

    memset(&log, 0, sizeof(log));

    log.uid = uid;

    strcpy(log.reader, reader);
    strcpy(log.direction, direction);
    strcpy(log.device_id, DEVICE_ID);

    log.timestamp = (uint32_t)time(NULL);

    nvs_set_blob(handle,
                 key,
                 &log,
                 sizeof(log));

    tail++;

    nvs_set_u32(handle, "tail", tail);

    nvs_commit(handle);

    nvs_close(handle);
}







void print_offline_logs(void)
{
    nvs_handle_t handle;

    if (nvs_open("offline_logs",
                 NVS_READONLY,
                 &handle) != ESP_OK)
        return;

    uint32_t head = 0;
    uint32_t tail = 0;

    nvs_get_u32(handle, "head", &head);
    nvs_get_u32(handle, "tail", &tail);

    printf("\n===== OFFLINE LOGS =====\n");

    for (uint32_t i = head; i < tail; i++)
    {
        char key[20];

        snprintf(key,
                 sizeof(key),
                 "log_%lu",
                 (unsigned long)i);

        offline_log_t log;
        size_t size = sizeof(log);

        if (nvs_get_blob(handle,
                         key,
                         &log,
                         &size) == ESP_OK)
        {
            time_t ts = log.timestamp;

            struct tm timeinfo;

            localtime_r(&ts,
                        &timeinfo);

            char time_str[32];

            strftime(time_str,
                     sizeof(time_str),
                     "%Y-%m-%d %H:%M:%S",
                     &timeinfo);

            printf(
                "d_id:%s UID:%lu DIR:%s Time:%s\n",
                log.device_id,
                (unsigned long)log.uid,
                log.direction,
                time_str);
        }
    }

    printf("========================\n");

    nvs_close(handle);
}



void upload_offline_logs(void)
{
    nvs_handle_t handle;

    if (nvs_open("offline_logs",
                 NVS_READWRITE,
                 &handle) != ESP_OK)
        return;

    uint32_t head = 0;
    uint32_t tail = 0;

    nvs_get_u32(handle, "head", &head);
    nvs_get_u32(handle, "tail", &tail);

    ESP_LOGI(
        "OFFLINE",
        "head=%lu tail=%lu",
        (unsigned long)head,
        (unsigned long)tail);

    while (head < tail)
    {
        char key[20];

        snprintf(key,
                 sizeof(key),
                 "log_%lu",
                 (unsigned long)head);

        offline_log_t log;
        size_t size = sizeof(log);

        if (nvs_get_blob(handle,
                         key,
                         &log,
                         &size) != ESP_OK)
        {
            head++;

            nvs_set_u32(handle,
                        "head",
                        head);

            continue;
        }

        send_offline_log_to_server(&log);

        ESP_LOGI(
            "OFFLINE",
            "Uploaded %s",
            key);

        nvs_erase_key(handle, key);

        head++;

        nvs_set_u32(handle,
                    "head",
                    head);

        nvs_commit(handle);

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    nvs_close(handle);
}


void send_offline_log_to_server(const offline_log_t *log)
{
    char json[256];

    snprintf(
        json,
        sizeof(json),
        "{\"device_id\":\"%s\","
        "\"uid\":%lu,"
        "\"reader\":\"%s\","
        "\"direction\":\"%s\","
        "\"timestamp\":%lu}",
        log->device_id,
        (unsigned long)log->uid,
        log->reader,
        log->direction,
        (unsigned long)log->timestamp);

    esp_mqtt_client_publish(
        mqtt_client,
        "esp32/offline_logs",
        json,
        0,
        2,
        0);
}