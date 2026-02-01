#include "gpio_pin.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"


static const char *TAG = "GPIO_INIT";


// Initialize all GPIOs
void gpio_pin_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask =
            (1ULL << GREEN_LED_1) |
            (1ULL << RED_LED_1)   |
            (1ULL << BUZZER_1)    |
            (1ULL << GREEN_LED_2) |
            (1ULL << RED_LED_2)   |
            (1ULL << BUZZER_2)    |
            (1ULL << RELAY_1),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

// ================= GREEN LED 1 =========================
static TimerHandle_t green_led_1_timer = NULL;

static void green_led_1_off_cb(TimerHandle_t xTimer)
{
    gpio_set_level(GREEN_LED_1, 0);
}

void green_led_1_on_500ms(void)
{
    if (green_led_1_timer == NULL) {
        green_led_1_timer = xTimerCreate(
            "green_led_1_timer",
            pdMS_TO_TICKS(500),
            pdFALSE,
            NULL,
            green_led_1_off_cb
        );
        if (green_led_1_timer == NULL) {
            // ❌ Timer creation failed
            // Decide how to fail safely
            return;
        }
    }

    gpio_set_level(GREEN_LED_1, 1);
    xTimerStop(green_led_1_timer, 0);
    xTimerStart(green_led_1_timer, 0);
}

//==================================================================


//======================= RED LED 1 ==================================

static TimerHandle_t red_led_1_timer = NULL;

static void red_led_1_off_cb(TimerHandle_t xTimer)
{
    gpio_set_level(RED_LED_1, 0);
}

void red_led_1_on_500ms(void)
{
    if (red_led_1_timer == NULL) {
        red_led_1_timer = xTimerCreate(
            "red_led_1_timer",
            pdMS_TO_TICKS(500),
            pdFALSE,
            NULL,
            red_led_1_off_cb
        );
        if (red_led_1_timer == NULL) {
            // ❌ Timer creation failed
            // Decide how to fail safely
            return;
        }
    }

    gpio_set_level(RED_LED_1, 1);
    xTimerStop(red_led_1_timer, 0);
    xTimerStart(red_led_1_timer, 0);
}


//======================================================================


// -------- BUZZER 1 --------
static TimerHandle_t buzzer1_timer = NULL;

static void buzzer1_timer_cb(TimerHandle_t xTimer)
{
    gpio_set_level(BUZZER_1, 0);
}

void buzzer_1_beep(void)
{
    if (buzzer1_timer == NULL) {
        buzzer1_timer = xTimerCreate(
            "buzzer1_timer",
            pdMS_TO_TICKS(500),
            pdFALSE,   // one-shot
            NULL,
            buzzer1_timer_cb
        );
        if (buzzer1_timer == NULL) {
            // ❌ Timer creation failed
            // Decide how to fail safely
            return;
        }
        
    }

    gpio_set_level(BUZZER_1, 1);
    xTimerStop(buzzer1_timer, 0);
    xTimerStart(buzzer1_timer, 0);
}


//=========== GREEN LED 2 ============================



static TimerHandle_t green_led_2_timer = NULL;

static void green_led_2_off_cb(TimerHandle_t xTimer)
{
    gpio_set_level(GREEN_LED_2, 0);
}

void green_led_2_on_500ms(void)
{
    if (green_led_2_timer == NULL) {
        green_led_2_timer = xTimerCreate(
            "green_led_2_timer",
            pdMS_TO_TICKS(500),
            pdFALSE,
            NULL,
            green_led_2_off_cb
        );
        if (green_led_2_timer == NULL) {
            // ❌ Timer creation failed
            // Decide how to fail safely
            return;
        }
    }

    gpio_set_level(GREEN_LED_2, 1);
    xTimerStop(green_led_2_timer, 0);
    xTimerStart(green_led_2_timer, 0);
}

//======================================================



//========== RED LED 2 ===============================

static TimerHandle_t red_led_2_timer = NULL;

static void red_led_2_off_cb(TimerHandle_t xTimer)
{
    gpio_set_level(RED_LED_2, 0);
}

void red_led_2_on_500ms(void)
{
    if (red_led_2_timer == NULL) {
        red_led_2_timer = xTimerCreate(
            "red_led_2_timer",
            pdMS_TO_TICKS(500),
            pdFALSE,
            NULL,
            red_led_2_off_cb
        );
        if (red_led_2_timer == NULL) {
            // ❌ Timer creation failed
            // Decide how to fail safely
            return;
        }
    }

    gpio_set_level(RED_LED_2, 1);
    xTimerStop(red_led_2_timer, 0);
    xTimerStart(red_led_2_timer, 0);
}

//======================================================

// -------- BUZZER 2 --------
static TimerHandle_t buzzer2_timer = NULL;

static void buzzer2_timer_cb(TimerHandle_t xTimer)
{
    gpio_set_level(BUZZER_2, 0);
}

void buzzer_2_beep(void)
{
    if (buzzer2_timer == NULL) {
        buzzer2_timer = xTimerCreate(
            "buzzer2_timer",
            pdMS_TO_TICKS(500),
            pdFALSE,   // one-shot
            NULL,
            buzzer2_timer_cb
        );
        if (buzzer2_timer == NULL) {
            // ❌ Timer creation failed
            // Decide how to fail safely
            return;
        }
    }
    gpio_set_level(BUZZER_2, 1);
    xTimerStop(buzzer2_timer, 0);
    xTimerStart(buzzer2_timer, 0);
}





// -------- RELAY --------
#define RELAY_ON_TIME_MS 5000

static TimerHandle_t relay_timer = NULL;

// Timer callback → turns relay OFF
static void relay_timer_callback(TimerHandle_t xTimer)
{
    gpio_set_level(RELAY_1, 0);
}

// 🔥 ONE FUNCTION ONLY
void relay_task(void *pvParameter)
{
    // Create timer only once
    if (relay_timer == NULL) {
        relay_timer = xTimerCreate(
            "relay_timer",
            pdMS_TO_TICKS(RELAY_ON_TIME_MS),
            pdFALSE,          // one-shot timer
            NULL,
            relay_timer_callback
        );
        if (relay_timer == NULL) {
            // ❌ Timer creation failed
            // Decide how to fail safely
            return;
        }
    }

    // Turn relay ON
    gpio_set_level(RELAY_1, 1);

    // Reset / start timer
    xTimerStop(relay_timer, 0);
    xTimerStart(relay_timer, 0);
}













