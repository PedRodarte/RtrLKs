#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define DISPLAY_H_RES 172
#define DISPLAY_V_RES 320

/* Inicializa SPI, painel ST7789, backlight e LVGL (via esp_lvgl_port). */
esp_err_t display_init(void);

/* Liga/desliga backlight e o painel (tela apagada no modo normal). */
void display_set_on(bool on);
