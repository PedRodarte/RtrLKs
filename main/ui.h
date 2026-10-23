#pragma once

#include <stdint.h>

#include "proximity.h"

/* Todas as funções pegam o lock do LVGL; podem ser chamadas de qualquer task. */

void ui_show_off(void);
void ui_show_test_pattern(void);
void ui_show_lost(const char *url);
/* level: nível de proximidade do celular mais próximo */
void ui_show_approaching(const char *name, proximity_t level);
void ui_show_passkey(uint32_t passkey);
/* Tela genérica de informação (diagnóstico) */
void ui_show_info(const char *title, const char *body);
