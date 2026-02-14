#ifndef GPIO_PIN_H
#define GPIO_PIN_H

#include "driver/gpio.h"

// -------- PIN DEFINITIONS --------
#define GREEN_LED_1   GPIO_NUM_25
#define RED_LED_1     GPIO_NUM_32
#define BUZZER_1      GPIO_NUM_22

#define GREEN_LED_2   GPIO_NUM_13
#define RED_LED_2     GPIO_NUM_14
#define BUZZER_2      GPIO_NUM_33

#define RELAY_1       GPIO_NUM_23

// -------- FUNCTION DECLARATIONS --------
void gpio_pin_init(void);

void green_led_1_on_500ms(void);
void red_led_1_on_500ms(void);
void buzzer_1_beep(void);   // ✅ ADD THIS


void green_led_2_on_500ms(void);
void red_led_2_on_500ms(void);
void buzzer_2_beep(void);   // ✅ ADD THIS

void relay_task(void *pvParameter);

#endif
