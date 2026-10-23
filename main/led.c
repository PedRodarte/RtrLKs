#include "led.h"

#include <math.h>
#include <stdint.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define PIN_LED              8
#define STEP_MS              20
#define PAIRING_PERIOD_MS    3000

typedef enum {
    LED_MODE_OFF,
    LED_MODE_NORMAL,
    LED_MODE_NOBODY,
    LED_MODE_APPROACHING,
    LED_MODE_PAIRING,
} led_mode_t;

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static const rgb_t PROXIMITY_COLORS[PROXIMITY_LEVELS] = {
    [PROXIMITY_FAR]         = {255, 0, 0},   /* vermelho */
    [PROXIMITY_APPROACHING] = {255, 70, 0},  /* laranja */
    [PROXIMITY_NEAR]        = {255, 170, 0}, /* amarelo */
    [PROXIMITY_VERY_NEAR]   = {0, 255, 0},   /* verde */
    [PROXIMITY_MAX]         = {0, 0, 255},   /* azul */
};
/* Duração de um ciclo atual → anterior → atual. Como um sensor de ré: mais perto, mais rápido */
static const uint16_t PROXIMITY_PERIOD_MS[PROXIMITY_LEVELS] = {
    [PROXIMITY_FAR]         = 1400,
    [PROXIMITY_APPROACHING] = 1200,
    [PROXIMITY_NEAR]        = 1000,
    [PROXIMITY_VERY_NEAR]   = 800,
    [PROXIMITY_MAX]         = 600,
};
static const rgb_t COLOR_GREEN = {0, 255, 0};
static const rgb_t COLOR_RED = {255, 0, 0};
static const rgb_t COLOR_BLUE = {0, 0, 255};
static const rgb_t COLOR_PURPLE = {160, 0, 255};

static const char *TAG = "led";

static led_strip_handle_t s_strip;
static TaskHandle_t s_task;
static volatile led_mode_t s_mode = LED_MODE_OFF;
static volatile proximity_t s_level = PROXIMITY_FAR;
static volatile proximity_t s_prev_level = PROXIMITY_FAR;

static void set_color(rgb_t c, uint8_t brightness)
{
    led_strip_set_pixel(s_strip, 0, c.r * brightness / 255, c.g * brightness / 255, c.b * brightness / 255);
    led_strip_refresh(s_strip);
}

/* Espera ms (ou para sempre). Retorna true se o modo mudou durante a espera. */
static bool wait_ms(uint32_t ms)
{
    return ulTaskNotifyTake(pdTRUE, ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(ms)) != 0;
}

/* Um ciclo de transição suave a → b → a, sem apagar. Retorna true se o modo mudou. */
static bool crossfade(rgb_t a, rgb_t b, uint8_t brightness, uint32_t period_ms)
{
    const uint32_t steps = period_ms / STEP_MS;
    for (uint32_t i = 0; i < steps; i++) {
        float t = (1.0f - cosf(2.0f * (float)M_PI * i / steps)) / 2.0f; /* 0 → 1 → 0 */
        rgb_t c = {
            .r = (uint8_t)lroundf(a.r + (b.r - a.r) * t),
            .g = (uint8_t)lroundf(a.g + (b.g - a.g) * t),
            .b = (uint8_t)lroundf(a.b + (b.b - a.b) * t),
        };
        set_color(c, brightness);
        if (wait_ms(STEP_MS)) {
            return true;
        }
    }
    return false;
}

/* Uma respiração completa: acende e apaga suavemente. Retorna true se o modo mudou. */
static bool breathe(rgb_t c, uint8_t max_brightness, uint32_t period_ms)
{
    const uint32_t steps = period_ms / STEP_MS;
    for (uint32_t i = 0; i < steps; i++) {
        float x = (1.0f - cosf(2.0f * (float)M_PI * i / steps)) / 2.0f; /* 0 → 1 → 0 */
        /* Curva quadrática: o olho percebe brilho de forma não linear, assim a subida parece uniforme */
        set_color(c, (uint8_t)lroundf(max_brightness * x * x));
        if (wait_ms(STEP_MS)) {
            return true;
        }
    }
    return false;
}

static void led_task(void *arg)
{
    for (;;) {
        switch (s_mode) {
        case LED_MODE_OFF:
            led_strip_clear(s_strip);
            wait_ms(UINT32_MAX);
            break;
        case LED_MODE_NORMAL:
            if (CONFIG_LUCAS_LED_NORMAL_BRIGHTNESS == 0) {
                led_strip_clear(s_strip);
                wait_ms(UINT32_MAX);
            } else {
                breathe(COLOR_GREEN, CONFIG_LUCAS_LED_NORMAL_BRIGHTNESS, CONFIG_LUCAS_LED_BREATH_PERIOD_MS);
            }
            break;
        case LED_MODE_NOBODY:
            set_color(COLOR_RED, CONFIG_LUCAS_LED_ALERT_BRIGHTNESS);
            if (wait_ms(CONFIG_LUCAS_LED_SIREN_HALF_PERIOD_MS)) {
                break;
            }
            set_color(COLOR_BLUE, CONFIG_LUCAS_LED_ALERT_BRIGHTNESS);
            wait_ms(CONFIG_LUCAS_LED_SIREN_HALF_PERIOD_MS);
            break;
        case LED_MODE_APPROACHING: {
            /* Transição suave, sem apagar, entre a cor do nível atual e a do anterior (mostra a
             * direção). Os níveis são lidos no início do ciclo, quando a cor é a do nível atual.
             * Sem mudança de nível ainda, as duas cores são iguais e a cor fica fixa. */
            proximity_t level = s_level;
            crossfade(PROXIMITY_COLORS[level], PROXIMITY_COLORS[s_prev_level], CONFIG_LUCAS_LED_ACTIVE_BRIGHTNESS,
                      PROXIMITY_PERIOD_MS[level]);
            break;
        }
        case LED_MODE_PAIRING:
            breathe(COLOR_PURPLE, CONFIG_LUCAS_LED_ACTIVE_BRIGHTNESS, PAIRING_PERIOD_MS);
            break;
        }
    }
}

static void set_mode(led_mode_t mode)
{
    if (mode != s_mode) {
        s_mode = mode;
        xTaskNotifyGive(s_task);
    }
}

esp_err_t led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = PIN_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
#if CONFIG_LUCAS_LED_GRB
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#else
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
#endif
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip), TAG, "led strip");
    ESP_RETURN_ON_FALSE(xTaskCreate(led_task, "led", 2560, NULL, 3, &s_task) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

void led_show_off(void)
{
    set_mode(LED_MODE_OFF);
}

void led_show_normal(void)
{
    set_mode(LED_MODE_NORMAL);
}

void led_show_nobody(void)
{
    set_mode(LED_MODE_NOBODY);
}

void led_show_approaching(proximity_t level)
{
    if (level >= PROXIMITY_LEVELS) {
        level = PROXIMITY_MAX;
    }
    if (s_mode != LED_MODE_APPROACHING) {
        s_prev_level = level; /* sem histórico: cor fixa */
    } else if (level != s_level) {
        /* "Anterior" é o vizinho do lado de onde veio: preserva a direção num salto de vários
         * níveis e nunca alterna vermelho/azul, que é a sirene de PERDIDO */
        s_prev_level = level > s_level ? level - 1 : level + 1;
    }
    s_level = level;
    set_mode(LED_MODE_APPROACHING);
}

void led_show_pairing(void)
{
    set_mode(LED_MODE_PAIRING);
}
