#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <string.h>


#include "nvs_flash.h"
#include "wifi_manager.h"
#include "web_server.h"

#include "mqtt_client.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_log.h"


#define LED_PIN_TEST 23

#define UART2_TX 17
#define UART2_RX 16

#define UART1_TX 26
#define UART1_RX 27

QueueHandle_t uart1_queue;
QueueHandle_t uart2_queue;

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
            }
        }
    }
}


static const char *TAG = "ESP32_MQTT";

static esp_mqtt_client_handle_t mqtt_client;


/* MQTT event handler */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT CONNECTED");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT DISCONNECTED");
            break;

        default:
            break;
    }
}




static void mqtt_publish_task(void *pvParameters)
{
    int count = 0;
    char msg[64];

    while (1) {
        snprintf(msg, sizeof(msg), "Hello from ESP32 count=%d", count++);
        esp_mqtt_client_publish(
            mqtt_client,
            "esp32/test",
            msg,
            0,
            0,
            0
        );
        ESP_LOGI(TAG, "Published: %s", msg);
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 seconds
    }
}




void app_main(void)
{
    //----------wifi initilization---------
    nvs_flash_init();

    wifi_manager_init();
    web_server_start();

    // ---------- UART2 (Reader 1) ----------
    uart_config_t uart2_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    uart_param_config(UART_NUM_2, &uart2_config);
    uart_set_pin(UART_NUM_2, UART2_TX, UART2_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_2, 2048, 0, 10, &uart2_queue, 0);

    // ---------- UART1 (Reader 2) ----------
    uart_config_t uart1_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    uart_param_config(UART_NUM_1, &uart1_config);
    uart_set_pin(UART_NUM_1, UART1_TX, UART1_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, 2048, 0, 10, &uart1_queue, 0);

    // ---------- LED ----------
    gpio_reset_pin(LED_PIN_TEST);
    gpio_set_direction(LED_PIN_TEST, GPIO_MODE_OUTPUT);

    // ---------- Create tasks (ONCE) ----------
    xTaskCreate(uart2_task, "uart2_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart1_task, "uart1_task", 4096, NULL, 5, NULL);


    //-----------------mqtt setup ------------

    ESP_LOGI(TAG, "Starting ESP32 MQTT test");

    /* Initialize NVS */
    

    /* Initialize TCP/IP */
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Connect to WiFi (uses menuconfig credentials) */

    /* MQTT configuration */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "ws://esp32accesshub.novelinfra.com/mqtt",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    esp_mqtt_client_start(mqtt_client);

    /* Start publish task */
    xTaskCreate(
        mqtt_publish_task,
        "mqtt_publish_task",
        4096,
        NULL,
        5,
        NULL
    );




    

    // ---------- Idle loop ----------
    while (1) {
        gpio_set_level(LED_PIN_TEST, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(LED_PIN_TEST, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}