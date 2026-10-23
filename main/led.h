#pragma once

#include "esp_err.h"
#include "proximity.h"

/* LED WS2812 (GPIO8). As funções podem ser chamadas de qualquer task. */
esp_err_t led_init(void);

void led_show_off(void);
/* Armado, celular por perto: verde fraco "respirando" (ou apagado, se o brilho for 0) */
void led_show_normal(void);
/* Ninguém por perto (PERDIDO): sirene vermelho/azul */
void led_show_nobody(void);
/* Responsável chegando: transição suave, sem apagar, entre a cor do nível atual e a do anterior,
 * mais rápido quanto mais perto */
void led_show_approaching(proximity_t level);
/* Pareamento: roxo respirando devagar */
void led_show_pairing(void);
