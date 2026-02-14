#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "driver/gpio.h"

#include "wifi_manager.h"
#include "esp_sntp.h"
#include <time.h>
#include "freertos/event_groups.h"
#include "web_server.h"


#define WIFI_CONNECTED_BIT BIT0
#define MAX_RETRY 5

#define WIFI_LED_GPIO GPIO_NUM_2

extern EventGroupHandle_t wifi_event_group;

static const char *TAG = "WIFI_MGR";
EventGroupHandle_t wifi_event_group;


static char saved_ssid[32];
static char saved_pass[64];



//-------------wifi strenght--------------

int wifi_get_rssi(void)
{
    wifi_ap_record_t ap_info;

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;   // RSSI in dBm
    }

    return -100; // Not connected
}


static void wifi_signal_task(void *arg)
{
    while (1) {
        int rssi = wifi_get_rssi();
        const char *quality = "Unknown";

        if (rssi >= -50) {
            quality = "Excellent";
        } else if (rssi >= -60) {
            quality = "Good";
        } else if (rssi >= -70) {
            quality = "Fair";
        } else if (rssi >= -80) {
            quality = "Weak";
        } else {
            quality = "Very Weak";
        }

        ESP_LOGI("WIFI", "Signal: %d dBm (%s)", rssi, quality);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}


bool wifi_get_connected_ssid(char *ssid, size_t len)
{
    wifi_ap_record_t ap_info;

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strncpy(ssid, (char *)ap_info.ssid, len - 1);
        ssid[len - 1] = '\0';
        return true;
    }
    return false;
}



//-------------------------------

static void initialize_sntp(void)
{
    ESP_LOGI("SNTP", "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}



static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        gpio_set_level(WIFI_LED_GPIO, 1); // LED OFF
        ESP_LOGW(TAG, "STA disconnected");
    }

    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        gpio_set_level(WIFI_LED_GPIO, 0); // LED ON
        ESP_LOGI(TAG, "STA connected");
        initialize_sntp();

        time_t now = 0;
        struct tm timeinfo = { 0 };
        
        int retry = 0;
        const int retry_count = 10;

        while (timeinfo.tm_year < (2016 - 1900) && retry < retry_count) {
            ESP_LOGI("SNTP", "Waiting for time sync... (%d/%d)", retry + 1, retry_count);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            time(&now);
            localtime_r(&now, &timeinfo);
            retry++;
        }
        
        ESP_LOGI("SNTP", "Time synchronized");
        
        // Now start MQTT
    }
}







static void wifi_watchdog_task(void *arg)
{
    wifi_ap_record_t ap_info;

    while (1) {
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
            ESP_LOGW("WIFI_WD", "STA truly disconnected, reconnecting");
            esp_wifi_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); // check every 60s
    } 
}

static void wifi_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << WIFI_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_set_level(WIFI_LED_GPIO, 1); // OFF initially
}

static bool load_wifi_nvs(void)
{
    nvs_handle_t nvs;
    size_t ssid_len = sizeof(saved_ssid);
    size_t pass_len = sizeof(saved_pass);

    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK)
        return false;

    if (nvs_get_str(nvs, "ssid", saved_ssid, &ssid_len) != ESP_OK ||
        nvs_get_str(nvs, "pass", saved_pass, &pass_len) != ESP_OK) {
        nvs_close(nvs);
        return false;
    }

    nvs_close(nvs);
    return true;
}

static void save_wifi_nvs(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    nvs_open("wifi", NVS_READWRITE, &nvs);
    nvs_set_str(nvs, "ssid", ssid);
    nvs_set_str(nvs, "pass", pass);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void wifi_connect_sta(const char *ssid, const char *pass)
{
    //save_wifi_nvs(ssid, pass);
    if (strcmp(saved_ssid, ssid) != 0 ||
    strcmp(saved_pass, pass) != 0) {
    save_wifi_nvs(ssid, pass);
    }
}



void wifi_manager_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_led_init();
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    xTaskCreate(
    wifi_watchdog_task,
    "wifi_wd",
    4096,
    NULL,
    5,
    NULL
    );


    //---------------call this task when sending data to erp-----------
    // xTaskCreate(
    // wifi_signal_task,
    // "wifi_signal",
    // 2048,
    // NULL,
    // 4,
    // NULL
    // );

    ESP_ERROR_CHECK(esp_event_handler_register(
    WIFI_EVENT,
    ESP_EVENT_ANY_ID,
    wifi_event_handler,
    NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
    IP_EVENT,
    IP_EVENT_STA_GOT_IP,
    wifi_event_handler,
    NULL));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = "ESP32_SETUP",
            .password = "12345678",
            .channel = 0,   // AUTO
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    web_server_start();

    // 🔑 LOAD SAVED CREDENTIALS AFTER BOOT
    if (load_wifi_nvs()) {
        wifi_config_t sta_cfg = {0};
        strcpy((char *)sta_cfg.sta.ssid, saved_ssid);
        strcpy((char *)sta_cfg.sta.password, saved_pass);

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

bool wifi_is_sta_connected(void)
{
    return xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT;
}
