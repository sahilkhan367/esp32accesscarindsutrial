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


/* ===================== GPIO & UART DEFINES ===================== */

#define LED_PIN_TEST   23

#define UART2_TX       17
#define UART2_RX       16

#define UART1_TX       26
#define UART1_RX       27

/* ===================== FORWARD DECLARATIONS ===================== */

void mqtt_publish(const char *data, const char *type);

/* ===================== GLOBAL VARIABLES ===================== */

QueueHandle_t uart1_queue;
QueueHandle_t uart2_queue;

static const char *TAG = "ESP32_MQTT";
static esp_mqtt_client_handle_t mqtt_client;


#define DEVICE_ID "esp32_001"



/* ===================== UART TASKS ===================== */

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
                if (rfid_exists("5400CAA21B27")) {
                    printf("ACCESS GRANTED\n");
                } else {
                    printf("ACCESS DENIED\n");
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
                if (rfid_exists("5400CAA21B27")) {
                    printf("ACCESS GRANTED\n");
                } else {
                    printf("ACCESS DENIED\n");
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
            "esp32/cmd/esp32_001",
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
        // 👉 handle command here
        if (strncmp(event->data, "ADD", event->data_len) == 0) {
            ESP_LOGI(TAG, "Data recived");
            printf("DATA: %.*s\n", event->data_len, event->data);
            //data_parsing();
            //--------data add in esp32 code--------------


            //-------ACK check for recived data
            esp_mqtt_client_publish(
            event->client,
            "esp32/ack/esp32_001",
            "{\"cmd\":\"ADD\",\"status\":\"received\"}",
            0,
            1,
            0
        );
        }else if(strncmp(event->data, "RM", event->data_len)==0){
            printf("DATA: %.*s\n", event->data_len, event->data);
            //data_parsing("RM event");
            //--------data add in esp32 code--------------


            //-------ACK check for recived data
            esp_mqtt_client_publish(
            event->client,
            "esp32/ack/esp32_001",
            "{\"cmd\":\"RM\",\"status\":\"received\"}",
            0,
            1,
            0
        );
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

    esp_mqtt_client_publish(
    mqtt_client,
    "esp32/status/esp32_001",
    "{\"device_id\":\"esp32_001\",\"status\":\"online\"}",
    0,
    1,
    1
    );

    


    //======== get message as data=======
    nvs_init();









    /* ---------- Idle Loop ---------- */
    while (1) {
        // gpio_set_level(LED_PIN_TEST, 1);
        // vTaskDelay(pdMS_TO_TICKS(1000));

        // gpio_set_level(LED_PIN_TEST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
