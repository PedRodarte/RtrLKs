#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "proximity.h"

/* Valores da característica Estado (protocolo com o app) */
typedef enum {
    TAG_STATE_DISARMED = 0,
    TAG_STATE_ARMED = 1,
    TAG_STATE_LOST = 2,
    TAG_STATE_COMING = 3,
} tag_state_t;

/* Chamadas a partir da task do NimBLE ou da task de proximidade: não bloqueie nelas. */
typedef struct {
    void (*on_heartbeat)(const char *name);
    void (*on_coming)(const char *name);
    void (*on_command)(const char *command);
    void (*on_pairing_window)(bool open);
    void (*on_passkey)(uint32_t passkey);
    void (*on_pairing_done)(bool success);
    /* phone_connected: há pelo menos um celular pareado conectado; level só vale nesse caso */
    void (*on_proximity)(bool phone_connected, proximity_t level);
} tag_ble_callbacks_t;

esp_err_t tag_ble_init(const tag_ble_callbacks_t *callbacks);

/* Atualiza a característica Estado e notifica os celulares pareados inscritos */
void tag_ble_set_state(tag_state_t state);

/* Libera novos pareamentos por CONFIG_FINDLK_PAIRING_WINDOW_S segundos */
void tag_ble_open_pairing_window(void);
