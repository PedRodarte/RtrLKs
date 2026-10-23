#include "button.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_BOOT      9
#define POLL_MS       50
#define LONG_PRESS_MS 3000

static const char *TAG = "button";

static void (*s_on_long_press)(void);

static void button_task(void *arg)
{
    uint32_t held_ms = 0;
    bool fired = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (gpio_get_level(PIN_BOOT) != 0) { /* solto (ativo em nível baixo) */
            held_ms = 0;
            fired = false;
            continue;
        }
        held_ms += POLL_MS;
        if (!fired && held_ms >= LONG_PRESS_MS) {
            fired = true; /* um disparo por toque */
            s_on_long_press();
        }
    }
}

esp_err_t button_init(void (*on_long_press)(void))
{
    s_on_long_press = on_long_press;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio");
    ESP_RETURN_ON_FALSE(xTaskCreate(button_task, "button", 2048, NULL, 3, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "task");
    return ESP_OK;
}
