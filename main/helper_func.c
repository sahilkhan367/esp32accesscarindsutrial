#include <stdio.h>
#include <string.h>
#include <stddef.h>   // for size_t

#include "nvs.h"
#include "nvs_flash.h"

void rfid_add(const char *id);
void rfid_remove(const char *id);
void rfid_display_all(void);
bool rfid_exists(const char *id);



#define RFID_NAMESPACE "rfid_db"


#define KEY_MAX_LEN     16
#define VALUE_MAX_LEN   32

void data_parsing(const char *data, size_t data_len)
{
    char key[KEY_MAX_LEN + 1] = {0};
    char value[VALUE_MAX_LEN + 1] = {0};

    if (data == NULL || data_len == 0) {
        //printf("ERROR: Invalid input\n");
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

}


void rfid_add(const char *id)
{
    nvs_handle_t nvs;
    if (nvs_open(RFID_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        printf("NVS open failed\n");
        return;
    }

    uint8_t value = 1;
    esp_err_t err = nvs_set_u8(nvs, id, value);

    if (err == ESP_OK) {
        nvs_commit(nvs);
        printf("RFID ADDED: %s\n", id);
    } else {
        printf("ADD FAILED\n");
    }

    nvs_close(nvs);
}




void rfid_remove(const char *id)
{
    nvs_handle_t nvs;
    if (nvs_open(RFID_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        printf("NVS open failed\n");
        return;
    }

    esp_err_t err = nvs_erase_key(nvs, id);

    if (err == ESP_OK) {
        nvs_commit(nvs);
        printf("RFID REMOVED: %s\n", id);
    } else {
        printf("RFID NOT FOUND\n");
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




bool rfid_exists(const char *id)
{
    nvs_handle_t nvs;
    uint8_t value;

    if (nvs_open(RFID_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_get_u8(nvs, id, &value);
    nvs_close(nvs);

    return (err == ESP_OK);
}