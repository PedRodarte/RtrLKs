#include "app.h"

#include <stdio.h>
#include <string.h>

#include "button.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led.h"
#include "tag_ble.h"
#include "ui.h"
#include "wifi_home.h"

#define TEXT_LEN           32
#define TICK_MS            1000
#define PASSKEY_TIMEOUT_US (60LL * 1000000) /* some a tela do passkey se o pareamento não terminar */
#define LOST_TIMEOUT_US    ((int64_t)CONFIG_LUCAS_LOST_TIMEOUT_S * 1000000)

typedef enum {
    EV_HEARTBEAT,
    EV_COMING,
    EV_COMMAND,
    EV_PAIRING_WINDOW,
    EV_PASSKEY,
    EV_PAIRING_DONE,
    EV_PROXIMITY,
    EV_HOME,
} event_type_t;

typedef struct {
    event_type_t type;
    union {
        char text[TEXT_LEN + 1];
        uint32_t passkey;
        bool flag;
        struct {
            bool connected;
            proximity_t level;
        } prox;
    };
} event_t;

/* Telas de pareamento ficam por cima do estado */
typedef enum {
    OVERLAY_NONE,
    OVERLAY_PAIRING_WINDOW,
    OVERLAY_PASSKEY,
} overlay_t;

static const char *TAG = "app";
static const char *const STATE_NAMES[] = {"DESARMADO", "ARMADO", "PERDIDO", "CHEGANDO"};

static QueueHandle_t s_queue;

static tag_state_t s_state = TAG_STATE_DISARMED;
static int64_t s_last_heartbeat_us;
static char s_coming_name[TEXT_LEN + 1];
static bool s_phone_connected;
static bool s_at_home;
static proximity_t s_level = PROXIMITY_FAR;
static overlay_t s_overlay = OVERLAY_NONE;
static uint32_t s_passkey;
static int64_t s_overlay_since_us;

/* ---------- Eventos (chamados de outras tasks) ---------- */

static void post(const event_t *ev)
{
    if (xQueueSend(s_queue, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "fila cheia; evento %d descartado", ev->type);
    }
}

static void post_text(event_type_t type, const char *text)
{
    event_t ev = {.type = type};
    strlcpy(ev.text, text, sizeof(ev.text));
    post(&ev);
}

static void on_heartbeat(const char *name)
{
    post_text(EV_HEARTBEAT, name);
}

static void on_coming(const char *name)
{
    post_text(EV_COMING, name);
}

static void on_command(const char *command)
{
    post_text(EV_COMMAND, command);
}

static void on_pairing_window(bool open)
{
    post(&(event_t){.type = EV_PAIRING_WINDOW, .flag = open});
}

static void on_passkey(uint32_t passkey)
{
    post(&(event_t){.type = EV_PASSKEY, .passkey = passkey});
}

static void on_pairing_done(bool success)
{
    post(&(event_t){.type = EV_PAIRING_DONE, .flag = success});
}

static void on_proximity(bool connected, proximity_t level)
{
    post(&(event_t){.type = EV_PROXIMITY, .prox = {.connected = connected, .level = level}});
}

static void on_home(bool at_home)
{
    post(&(event_t){.type = EV_HOME, .flag = at_home});
}

/* ---------- Saída: tela e LED ---------- */

static void render(void)
{
    switch (s_overlay) {
    case OVERLAY_PASSKEY:
        ui_show_passkey(s_passkey);
        led_show_pairing();
        return;
    case OVERLAY_PAIRING_WINDOW:
        ui_show_info("Pareamento", "Conecte o celular pelo app e digite o código que vai aparecer aqui.");
        led_show_pairing();
        return;
    case OVERLAY_NONE:
        break;
    }

    switch (s_state) {
    case TAG_STATE_DISARMED:
        ui_show_off();
        led_show_off();
        break;
    case TAG_STATE_ARMED:
        ui_show_off();
        led_show_normal();
        break;
    case TAG_STATE_LOST:
        ui_show_lost(CONFIG_LUCAS_QR_URL);
        led_show_nobody();
        break;
    case TAG_STATE_COMING: {
        proximity_t level = s_phone_connected ? s_level : PROXIMITY_FAR;
        ui_show_approaching(s_coming_name, level);
        led_show_approaching(level);
        break;
    }
    }
}

static void set_state(tag_state_t state)
{
    if (state == s_state) {
        return;
    }
    ESP_LOGI(TAG, "%s -> %s", STATE_NAMES[s_state], STATE_NAMES[state]);
    s_state = state;
    tag_ble_set_state(state);
    render();
}

static void set_overlay(overlay_t overlay)
{
    if (overlay != s_overlay) {
        s_overlay = overlay;
        s_overlay_since_us = esp_timer_get_time();
        render();
    }
}

/* ---------- Regras ---------- */

static void start_coming(const char *name)
{
    strlcpy(s_coming_name, name, sizeof(s_coming_name));
    if (s_state == TAG_STATE_COMING) {
        render(); /* outro responsável: atualiza o nome */
    } else {
        set_state(TAG_STATE_COMING);
    }
}

static void handle(const event_t *ev)
{
    int64_t now = esp_timer_get_time();

    switch (ev->type) {
    case EV_HEARTBEAT:
        ESP_LOGI(TAG, "heartbeat de \"%s\"", ev->text);
        s_last_heartbeat_us = now;
        if (s_at_home) {
            break; /* em casa: heartbeats não armam */
        }
        if (s_state == TAG_STATE_DISARMED) {
            set_state(TAG_STATE_ARMED);
        } else if (s_state == TAG_STATE_LOST) {
            /* Um responsável reencontrou o sinal */
            start_coming(ev->text);
        }
        break;

    case EV_COMING:
        ESP_LOGI(TAG, "\"%s\" avisou que está chegando", ev->text);
        s_last_heartbeat_us = now;
        if (!s_at_home && (s_state == TAG_STATE_LOST || s_state == TAG_STATE_COMING)) {
            start_coming(ev->text);
        }
        break;

    case EV_COMMAND:
        if (strcasecmp(ev->text, "ENCONTRADO") == 0) {
            if (s_state == TAG_STATE_LOST || s_state == TAG_STATE_COMING) {
                s_last_heartbeat_us = now;
                set_state(TAG_STATE_ARMED);
            }
        } else if (strcasecmp(ev->text, "DESARMAR") == 0) {
            set_state(TAG_STATE_DISARMED);
        } else {
            ESP_LOGW(TAG, "comando desconhecido \"%s\"", ev->text);
        }
        break;

    case EV_PAIRING_WINDOW:
        if (ev->flag) {
            set_overlay(OVERLAY_PAIRING_WINDOW);
        } else if (s_overlay == OVERLAY_PAIRING_WINDOW) {
            set_overlay(OVERLAY_NONE);
        }
        break;

    case EV_PASSKEY:
        s_passkey = ev->passkey;
        s_overlay = OVERLAY_NONE; /* força redesenho mesmo vindo de outro passkey */
        set_overlay(OVERLAY_PASSKEY);
        break;

    case EV_PAIRING_DONE:
        ESP_LOGI(TAG, "pareamento %s", ev->flag ? "concluído" : "falhou");
        if (s_overlay == OVERLAY_PASSKEY) {
            set_overlay(OVERLAY_NONE);
        }
        break;

    case EV_PROXIMITY:
        s_phone_connected = ev->prox.connected;
        s_level = ev->prox.level;
        if (s_state == TAG_STATE_COMING && s_overlay == OVERLAY_NONE) {
            render();
        }
        break;

    case EV_HOME:
        s_at_home = ev->flag;
        ESP_LOGI(TAG, "%s", s_at_home ? "em casa: desarmando" : "saiu de casa: o próximo heartbeat arma");
        if (s_at_home) {
            set_state(TAG_STATE_DISARMED);
        }
        break;
    }
}

static void check_timers(void)
{
    int64_t now = esp_timer_get_time();

    if ((s_state == TAG_STATE_ARMED || s_state == TAG_STATE_COMING) && now - s_last_heartbeat_us > LOST_TIMEOUT_US) {
        ESP_LOGW(TAG, "%d s sem heartbeat", CONFIG_LUCAS_LOST_TIMEOUT_S);
        set_state(TAG_STATE_LOST);
    }
    if (s_overlay == OVERLAY_PASSKEY && now - s_overlay_since_us > PASSKEY_TIMEOUT_US) {
        set_overlay(OVERLAY_NONE);
    }
}

static void app_task(void *arg)
{
    render();
    event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            handle(&ev);
        }
        check_timers();
    }
}

esp_err_t app_start(void)
{
    s_queue = xQueueCreate(16, sizeof(event_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "fila");
    ESP_RETURN_ON_FALSE(xTaskCreate(app_task, "app", 4096, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");

    static const tag_ble_callbacks_t callbacks = {
        .on_heartbeat = on_heartbeat,
        .on_coming = on_coming,
        .on_command = on_command,
        .on_pairing_window = on_pairing_window,
        .on_passkey = on_passkey,
        .on_pairing_done = on_pairing_done,
        .on_proximity = on_proximity,
    };
    ESP_RETURN_ON_ERROR(tag_ble_init(&callbacks), TAG, "bluetooth");
    ESP_RETURN_ON_ERROR(button_init(tag_ble_open_pairing_window), TAG, "botão");
    ESP_RETURN_ON_ERROR(wifi_home_init(on_home), TAG, "wi-fi de casa");
    return ESP_OK;
}
