#pragma once

#include "esp_err.h"

/* Botão BOOT (GPIO9). on_long_press é chamado (da task do botão) ao segurar por 3 s. */
esp_err_t button_init(void (*on_long_press)(void));
