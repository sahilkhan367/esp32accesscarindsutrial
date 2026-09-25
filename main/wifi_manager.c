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
#include "helper_func.h"
#include "esp_sntp.h"
#include <time.h>
#include "freertos/event_groups.h"
#include "web_server.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ethernet_module.h"
static bool ethernet_priority = false;


#define WIFI_CONNECTED_BIT BIT0
#define MAX_RETRY 5

#define WIFI_LED_GPIO GPIO_NUM_2

extern EventGroupHandle_t wifi_event_group;

static const char *TAG = "WIFI_MGR";
EventGroupHandle_t wifi_event_group;


static char saved_ssid[32];
static char saved_pass[64];

extern const char *DEVICE_ID;

static const char *RESET_TAG  = "RESET_TASK";
void sunday_reset_task(void *arg);
static void save_last_reset_day(int day);
static int load_last_reset_day();
static void time_sync_notification_cb(struct timeval *tv);
extern volatile bool ntp_time_synced;


static void time_sync_notification_cb(struct timeval *tv);
extern volatile bool ntp_time_synced;

void initialize_sntp(void);


static void ntp_sync_task(void *arg)
{
    ESP_LOGI("SNTP", "NTP sync task started");

    setenv("TZ", "IST-5:30", 1);
    tzset();

    // Wait for network
    while (!ethernet_is_connected() && !wifi_is_sta_connected())
    {
        ESP_LOGI("SNTP", "Waiting for network connection...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(
        "SNTP",
        "Network available. Ethernet=%d WiFi=%d",
        ethernet_is_connected(),
        wifi_is_sta_connected()
    );

    ntp_time_synced = false;

    initialize_sntp();

    // Wait maximum 15 seconds for first sync
    int retry = 0;

    while (!ntp_time_synced && retry < 30)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;

        ESP_LOGI(
            "SNTP",
            "Waiting for NTP synchronization... %d/30",
            retry
        );
    }

    if (ntp_time_synced)
    {
        ESP_LOGI("SNTP", "NTP synchronization successful");
    }
    else
    {
        ESP_LOGW("SNTP", "NTP sync failed. Retrying...");

        esp_sntp_stop();

        vTaskDelay(pdMS_TO_TICKS(1000));

        ntp_time_synced = false;

        initialize_sntp();

        retry = 0;

        while (!ntp_time_synced && retry < 30)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            retry++;
        }

        if (ntp_time_synced)
        {
            ESP_LOGI("SNTP", "NTP synchronization successful after retry");
        }
        else
        {
            ESP_LOGE("SNTP", "NTP synchronization failed");
        }
    }

    if (ntp_time_synced)
    {
        xTaskCreate(
            sunday_reset_task,
            "sunday_reset",
            4096,
            NULL,
            5,
            NULL
        );
    }

    vTaskDelete(NULL);
}

void wifi_update_network_led(void)
{
    bool ethernet_connected = ethernet_is_connected();
    bool wifi_connected = wifi_is_sta_connected();

    if (ethernet_connected || wifi_connected)
    {
        // At least one network is available
        gpio_set_level(WIFI_LED_GPIO, 0);   // BLUE LED OFF
    }
    else
    {
        // No Ethernet and no WiFi
        gpio_set_level(WIFI_LED_GPIO, 1);   // BLUE LED ON
    }

    ESP_LOGI("NET_LED",
             "Ethernet=%d WiFi=%d LED=%s",
             ethernet_connected,
             wifi_connected,
             (ethernet_connected || wifi_connected) ? "OFF" : "ON");
}







//-------------wifi strenght--------------

int wifi_get_rssi(void)
{
    wifi_ap_record_t ap_info;

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;   // RSSI in dBm
    }

    return -100; // Not connected
}

static bool ethernet_available(void)
{
    return ethernet_is_connected();
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
void initialize_sntp(void)
{
    ESP_LOGI("SNTP", "Initializing SNTP...");

    // Stop previous SNTP instance if already running
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // NTP servers
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");

    // Sync immediately when NTP response is received
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    // Register callback
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    ESP_LOGI("SNTP", "Starting SNTP...");

    // DO NOT call esp_sntp_getservername() here.
    // It can return NULL for an unused/uninitialized server slot.

    ESP_LOGI("SNTP", "Server 0 configured: pool.ntp.org");
    ESP_LOGI("SNTP", "Server 1 configured: time.google.com");
    ESP_LOGI("SNTP", "Server 2 configured: time.cloudflare.com");

    esp_sntp_init();

    ESP_LOGI(
        "SNTP",
        "SNTP sync status: %d",
        esp_sntp_get_sync_status()
    );
}


static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

        wifi_update_network_led();

        ESP_LOGW(TAG, "STA disconnected");

        /*
         * Ethernet has priority.
         * If Ethernet is available, do NOT reconnect WiFi.
         */
        if (ethernet_is_connected())
        {
            ESP_LOGI(TAG, "Ethernet active -> WiFi reconnect skipped");
        }
        else
        {
            /*
             * Ethernet is unavailable, so WiFi can reconnect.
             */
            ESP_LOGI(TAG, "Ethernet unavailable -> reconnecting WiFi");

            esp_err_t err = esp_wifi_connect();

            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "WiFi reconnect failed: %s",
                         esp_err_to_name(err));
            }
        }
    }

    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

        /*
         * Update network LED.
         *
         * Ethernet connected OR WiFi connected
         *              -> LED OFF
         *
         * Both disconnected
         *              -> LED ON
         */
        wifi_update_network_led();

        ESP_LOGI(TAG, "STA connected");



        /*
         * Check if synchronization actually happened
         */

        /*
         * MQTT starts from your existing code.
         */
    }
}







static void wifi_watchdog_task(void *arg)
{
    wifi_ap_record_t ap_info;

    while (1) {

        /*
         * Ethernet has priority.
         * Never reconnect WiFi while Ethernet is available.
         */
        // if (ethernet_is_connected()) {

        //     wifi_update_network_led();

        //     ESP_LOGI("WIFI_WD",
        //              "Ethernet active -> WiFi reconnect skipped");

        // } else {

        //     if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {

        //         ESP_LOGW(
        //             "WIFI_WD",
        //             "Ethernet unavailable and WiFi disconnected -> reconnecting"
        //         );

        //         esp_wifi_connect();
        //     }
        // }

        vTaskDelay(pdMS_TO_TICKS(30000));
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
    

    esp_err_t ret;
    
    ret = esp_netif_init();
    
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    
    ret = esp_event_loop_create_default();
    
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

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


    wifi_config_t ap_cfg = {0};   // initialize empty
    // Build SSID dynamically
    snprintf((char *)ap_cfg.ap.ssid,
             sizeof(ap_cfg.ap.ssid),
             "accesshub_%s",
             DEVICE_ID);
    
    // Set password
    strcpy((char *)ap_cfg.ap.password, "12345678");
    
    // Other configs
    ap_cfg.ap.channel = 0;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    web_server_start();
    wifi_event_group = xEventGroupCreate();
    xTaskCreate(
    ntp_sync_task,
    "ntp_sync",
    4096,
    NULL,
    5,
    NULL
);

    // 🔑 LOAD SAVED CREDENTIALS AFTER BOOT
    if (load_wifi_nvs()) {

        wifi_config_t sta_cfg = {0};

        strcpy((char *)sta_cfg.sta.ssid, saved_ssid);
        strcpy((char *)sta_cfg.sta.password, saved_pass);

        ESP_ERROR_CHECK(
            esp_wifi_set_config(WIFI_IF_STA, &sta_cfg)
        );

        /*
         * Ethernet has priority.
         * Do not connect Wi-Fi STA if Ethernet already has IP.
         */
        if (!ethernet_is_connected()) {

            ESP_LOGI(TAG,
                     "Ethernet not available -> connecting WiFi STA");

            ESP_ERROR_CHECK(esp_wifi_connect());

        } else {

            ESP_LOGI(TAG,
                     "Ethernet available -> WiFi STA connection skipped");

            gpio_set_level(WIFI_LED_GPIO, 1);
        }
    }
}

bool wifi_is_sta_connected(void)
{
    return xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT;
}



///-----------------------------------------------weekly reset on sunday-------------------



// ---------- NVS Helpers ----------
static int load_last_reset_day()
{
    nvs_handle_t nvs;
    int32_t last_day = -1;

    if (nvs_open("sys", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_i32(nvs, "last_rst", &last_day);
        nvs_close(nvs);
    }

    return last_day;
}

static void save_last_reset_day(int day)
{
    nvs_handle_t nvs;

    if (nvs_open("sys", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_i32(nvs, "last_rst", day);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

// ---------- Reset Task ----------
void sunday_reset_task(void *arg)
{
    struct tm timeinfo;
    int last_reset_day = load_last_reset_day();

    while (1)
    {
        time_t now;
        time(&now);
        localtime_r(&now, &timeinfo);

        int today = timeinfo.tm_yday;

        // ✅ Condition: Sunday (0) + 5 AM + within 5 min window
        if (timeinfo.tm_wday == 0 &&
            timeinfo.tm_hour == 5 &&
            timeinfo.tm_min == 30 &&
            last_reset_day != today)  // 2-minute window
        //    last_reset_day != today
        
        {
            ESP_LOGW(RESET_TAG , "Sunday 5AM reset triggered!");

            // Save reset day to avoid duplicate resets
            save_last_reset_day(today);

            vTaskDelay(2000 / portTICK_PERIOD_MS); // small delay

            esp_restart();
        }

        vTaskDelay(30000 / portTICK_PERIOD_MS); // check every 30 sec
    }
}





void wifi_manager_start_sta(void)
{
    if (ethernet_is_connected()) {

        ESP_LOGI(
            TAG,
            "Ethernet is available -> WiFi STA will not start"
        );

        return;
    }

    if (strlen(saved_ssid) == 0) {

        ESP_LOGW(
            TAG,
            "No saved WiFi credentials"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Starting WiFi STA: %s",
        saved_ssid
    );

    wifi_config_t sta_cfg = {0};

    strcpy(
        (char *)sta_cfg.sta.ssid,
        saved_ssid
    );

    strcpy(
        (char *)sta_cfg.sta.password,
        saved_pass
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &sta_cfg
        )
    );

    esp_wifi_connect();
}






//===Check wether it is wifi or ethernet=============

const char *network_type = "None";


void log_network_status(void)
{
    bool ethernet = ethernet_is_connected();
    bool wifi = wifi_is_sta_connected();

    if (ethernet)
    {
        ESP_LOGI("NETWORK", "Active Network: ETHERNET");
        network_type = "Ethernet";
    }
    else if (wifi)
    {
        ESP_LOGI("NETWORK", "Active Network: WIFI");
        network_type = "WiFi";
    }
    else
    {
        ESP_LOGW("NETWORK", "No network connected");
        network_type = "None";
    }
}


static void time_sync_notification_cb(struct timeval *tv)
{
    ntp_time_synced = true;

    ESP_LOGI(TAG, "NTP time synchronized");

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[32];

    strftime(
        time_str,
        sizeof(time_str),
        "%d-%m-%Y %H:%M:%S",
        &timeinfo
    );

    ESP_LOGI(TAG, "NTP Time: %s", time_str);
}
