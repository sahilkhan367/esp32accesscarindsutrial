#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "nvs_flash.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "mqtt_client.h"
#include "nvs.h"


#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_log.h"
#include "helper_func.h"


#include "nvs_flash.h"
#include "nvs.h"

#include "gpio_pin.h"  //for gpio pins file



/* ===================== GPIO & UART DEFINES ===================== */

#define LED_PIN_TEST   23

#define UART2_TX       17
#define UART2_RX       16

#define UART1_TX       26
#define UART1_RX       27



#define DEVICE_ID "esp32_001"
#define OTA_BASE_URL "http://esp32accesshub.novelinfra.com/firmware"



static const char *TAG = "ESP32_MQTT";
static esp_mqtt_client_handle_t mqtt_client;


//===============OTA update =======================

#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_log.h"

void start_ota_update(void)
{
    char ota_url[128];

    snprintf(
        ota_url,
        sizeof(ota_url),
        "%s/%s",
        OTA_BASE_URL,
        DEVICE_ID
    );

    ESP_LOGI("OTA", "OTA URL: %s", ota_url);

    esp_http_client_config_t http_cfg = {
        .url = ota_url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    ESP_LOGI("OTA", "Starting OTA update...");

    esp_err_t ret = esp_https_ota(&ota_cfg);

    if (ret == ESP_OK) {
        ESP_LOGI("OTA", "OTA successful, rebooting...");
        esp_restart();
    } else {
        ESP_LOGE("OTA", "OTA failed: %s", esp_err_to_name(ret));
    }
}




/* ===================== FORWARD DECLARATIONS ===================== */

void mqtt_publish(const char *data, const char *type)
{
    if (!mqtt_client) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return;
    }

    char json[128];

    snprintf(
        json,
        sizeof(json),
        "{ \"device_id\":\"%s\", \"type\":\"%s\", \"data\":\"%s\" }",
        DEVICE_ID,
        type,
        data
    );

    esp_mqtt_client_publish(
        mqtt_client,
        "esp32/request",
        json,
        0,
        1,
        0
    );

    ESP_LOGI(TAG, "MQTT published: %s", json);
}

/* ===================== GLOBAL VARIABLES ===================== */

QueueHandle_t uart1_queue;
QueueHandle_t uart2_queue;






uint32_t result;

/* ===================== UART TASKS ===================== */

//REQ_TOPIC = "esp32/request"


void send_uart_scan_to_server(const char *reader,
                              uint32_t uid,
                              const char *direction)
{
    char data[96];

    snprintf(
        data,
        sizeof(data),
        "%s:%lu:%s",
        reader,
        (unsigned long)uid,
        direction
    );

    mqtt_publish(data, "rfid");
}




void uart2_task(void *arg)
{
    uart_event_t event;
    char rx_data[129];

    while (1) {
        if (xQueueReceive(uart2_queue, &event, portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                int len = uart_read_bytes(
                    UART_NUM_2,
                    rx_data,
                    event.size,
                    portMAX_DELAY
                );

                rx_data[len] = '\0';
                printf("Reader 1: %s\n", rx_data);
                uint32_t result;
                result = uid_to_decimal(rx_data);
                printf("%lu\n", (unsigned long)result);
                if (rfid_exists(result)) {
                    printf("ACCESS GRANTED\n");
                    relay_task(NULL); 
                    green_led_1_on_500ms();
                    buzzer_1_beep();
                    send_uart_scan_to_server("reader1", result, "IN");
                } else {
                    red_led_1_on_500ms();   // ✅ safe, non-blocking
                    printf("Denaid\n");
                }
            }
        }
    }
}

void uart1_task(void *arg)
{
    uart_event_t event;
    char rx_data[129];

    while (1) {
        if (xQueueReceive(uart1_queue, &event, portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                int len = uart_read_bytes(
                    UART_NUM_1,
                    rx_data,
                    event.size,
                    portMAX_DELAY
                );

                rx_data[len] = '\0';
                printf("Reader 2: %s\n", rx_data);
                result = uid_to_decimal(rx_data);
                printf("%lu\n", (unsigned long)result);
                if (rfid_exists(result)) {
                    printf("ACCESS GRANTED\n");
                    relay_task(NULL);
                    green_led_2_on_500ms();
                    buzzer_2_beep();
                    send_uart_scan_to_server("reader2", result, "OUT");
                } else {
                    red_led_2_on_500ms();   // ✅ safe, non-blocking
                    printf("Denaid\n");
                }
            }
        }
    }
}

/* ===================== Get message as data ===================== */

static void mqtt_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");

        // ✅ SUBSCRIBE for commands
        esp_mqtt_client_subscribe(
            event->client,
            "esp32/cmd/esp32_001",   /// change here for different esp32
            1
        );

        // Publish online status
        esp_mqtt_client_publish(
            event->client,
            "esp32/status/esp32_001",
            "{\"device_id\":\"esp32_001\",\"status\":\"online\"}",
            0,
            1,
            1
        );
        break;

    case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "CMD RECEIVED: %.*s", event->data_len, event->data);
    printf("DATA before if: %.*s\n", event->data_len, event->data);
    data_parsing(event->data, event->data_len);
    static char cached_gmail[128] = "";
    if (strncmp(event->data, "gmail:", 6) == 0) {
    snprintf(
        cached_gmail,
        sizeof(cached_gmail),
        "%.*s",
        event->data_len - 6,
        event->data + 6
    );
    ESP_LOGI(TAG, "Cached gmail: %s", cached_gmail);
    }

    if (strncmp(event->data, "ADD", 3) == 0) {
        ESP_LOGI(TAG, "Data recived");
        printf("DATA: %.*s\n", event->data_len, event->data);

        // 🔹 MINIMAL ADDITION START
        char ack_msg[256];

        snprintf(
            ack_msg,
            sizeof(ack_msg),
            "{\"device_id\":\"esp32_001\",\"ADD\":\"%.*s\",\"status\":\"received\",\"gmail\":\"%s\"}",
            event->data_len,
            event->data,
            cached_gmail
        );
        // 🔹 MINIMAL ADDITION END

        esp_mqtt_client_publish(
            event->client,
            "esp32/ack/esp32_001",
            ack_msg,   // 👈 only this line changed
            0,
            1,
            1
        );
        }
        else if (strncmp(event->data, "RM", 2) == 0) {
        printf("DATA: %.*s\n", event->data_len, event->data);
        // 🔹 MINIMAL ADDITION
        char ack_msg[256];
        snprintf(
        ack_msg,
        sizeof(ack_msg),
        "{\"device_id\":\"esp32_001\",\"RM\":\"%.*s\",\"status\":\"received\",\"gmail\":\"%s\"}",
        event->data_len,
        event->data,
        cached_gmail
        );
        esp_mqtt_client_publish(
            event->client,
            "esp32/ack/esp32_001",
            ack_msg,   // 👈 only change
            0,
            1,
            1
        );
        }else if (strncmp(event->data, "OTA", 3) == 0) {
        ESP_LOGI(TAG, "OTA command received");
        start_ota_update();
        }
        break;

    default:
        break;
    }
}

//==================storing the data=====================

void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        nvs_flash_erase();
        nvs_flash_init();
    }
}







/* ===================== MAIN APPLICATION ===================== */

void app_main(void)
{
    /* ---------- WiFi Initialization ---------- */
    nvs_flash_init();
    wifi_manager_init();
    web_server_start();

    /* ---------- UART2 (Reader 1) ---------- */
    uart_config_t uart2_config = {
        .baud_rate  = 9600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    uart_param_config(UART_NUM_2, &uart2_config);
    uart_set_pin(
        UART_NUM_2,
        UART2_TX,
        UART2_RX,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    uart_driver_install(
        UART_NUM_2,
        2048,
        0,
        10,
        &uart2_queue,
        0
    );

    /* ---------- UART1 (Reader 2) ---------- */
    uart_config_t uart1_config = {
        .baud_rate  = 9600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    uart_param_config(UART_NUM_1, &uart1_config);
    uart_set_pin(
        UART_NUM_1,
        UART1_TX,
        UART1_RX,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    uart_driver_install(
        UART_NUM_1,
        2048,
        0,
        10,
        &uart1_queue,
        0
    );

    /* ---------- LED ---------- */
    gpio_reset_pin(LED_PIN_TEST);
    gpio_set_direction(LED_PIN_TEST, GPIO_MODE_OUTPUT);



    /* ---------- Create Tasks ---------- */
    xTaskCreate(uart2_task, "uart2_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart1_task, "uart1_task", 4096, NULL, 5, NULL);

    /* ---------- MQTT Setup ---------- */
    ESP_LOGI(TAG, "Starting ESP32 MQTT test");

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "ws://esp32accesshub.novelinfra.com/mqtt",
        .session.last_will.topic = "esp32/status/esp32_001",
        .session.last_will.msg = "{\"device_id\":\"esp32_001\",\"status\":\"offline\"}",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(
    mqtt_client,
    ESP_EVENT_ANY_ID,
    mqtt_event_handler,
    NULL
    );




    esp_mqtt_client_start(mqtt_client);

   



    //======== get message as data=======
    nvs_init();
    gpio_pin_init();


    /* ---------- Idle Loop ---------- */
    while (1) {
        // gpio_set_level(LED_PIN_TEST, 1);
        // vTaskDelay(pdMS_TO_TICKS(1000));

        // gpio_set_level(LED_PIN_TEST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}