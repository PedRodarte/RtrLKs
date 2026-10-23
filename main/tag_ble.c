#include "tag_ble.h"

#include <ctype.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* a7c300NN-1b8e-4f2a-9d6e-3c5b7e9f0a12, em ordem little-endian */
#define LUCAS_UUID(n) \
    BLE_UUID128_INIT(0x12, 0x0a, 0x9f, 0x7e, 0x5b, 0x3c, 0x6e, 0x9d, 0x2a, 0x4f, 0x8e, 0x1b, (n), 0x00, 0xc3, 0xa7)

#define TEXT_MAX           32 /* bytes UTF-8 aceitos numa escrita */
#define MAX_CONNS          CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define PROX_PERIOD_MS     1000
#define RSSI_ALPHA         0.3f /* média móvel exponencial */
#define RSSI_HYSTERESIS_DB 3
#define PROX_DWELL_SAMPLES 3 /* um nível novo precisa se manter por 3 leituras (~3 s) */

enum { CHR_HEARTBEAT, CHR_COMING, CHR_COMMAND, CHR_STATE };

typedef struct {
    bool used;
    uint16_t handle;
    bool subscribed;
    bool has_rssi;
    float rssi;
} conn_t;

static const char *TAG = "tag_ble";

static const ble_uuid128_t UUID_SVC = LUCAS_UUID(0x01);
static const ble_uuid128_t UUID_HEARTBEAT = LUCAS_UUID(0x02);
static const ble_uuid128_t UUID_COMING = LUCAS_UUID(0x03);
static const ble_uuid128_t UUID_COMMAND = LUCAS_UUID(0x04);
static const ble_uuid128_t UUID_STATE = LUCAS_UUID(0x05);

static tag_ble_callbacks_t s_cb;
static SemaphoreHandle_t s_lock;
static conn_t s_conns[MAX_CONNS];
static uint8_t s_own_addr_type;
static uint16_t s_state_handle;
static volatile uint8_t s_state = TAG_STATE_DISARMED;
static volatile int64_t s_pairing_until_us;

static int gap_event(struct ble_gap_event *event, void *arg);

/* ---------- Conexões ---------- */

static conn_t *conn_get(uint16_t handle, bool create)
{
    conn_t *free_slot = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        if (s_conns[i].used && s_conns[i].handle == handle) {
            return &s_conns[i];
        }
        if (!s_conns[i].used && !free_slot) {
            free_slot = &s_conns[i];
        }
    }
    if (create && free_slot) {
        *free_slot = (conn_t){.used = true, .handle = handle};
        return free_slot;
    }
    return NULL;
}

static bool conn_is_authenticated(uint16_t handle)
{
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(handle, &desc) == 0 && desc.sec_state.encrypted && desc.sec_state.authenticated;
}

/* ---------- Pareamento ---------- */

static bool pairing_allowed(void)
{
    int bonds = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bonds);
    /* Sem nenhum celular pareado ainda, o primeiro pareia sem precisar do botão */
    return bonds == 0 || esp_timer_get_time() < s_pairing_until_us;
}

void tag_ble_open_pairing_window(void)
{
    s_pairing_until_us = esp_timer_get_time() + (int64_t)CONFIG_LUCAS_PAIRING_WINDOW_S * 1000000;
    ESP_LOGI(TAG, "pareamento liberado por %d s", CONFIG_LUCAS_PAIRING_WINDOW_S);
    s_cb.on_pairing_window(true);
}

static void close_pairing_window(void)
{
    if (s_pairing_until_us != 0) {
        s_pairing_until_us = 0;
        s_cb.on_pairing_window(false);
    }
}

/* ---------- GATT ---------- */

static int chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const uintptr_t chr = (uintptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t state = s_state;
        return os_mbuf_append(ctxt->om, &state, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > TEXT_MAX) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    char text[TEXT_MAX + 1];
    if (ble_hs_mbuf_to_flat(ctxt->om, text, TEXT_MAX, &len) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    /* Apps de teste às vezes mandam \n ou \0 no fim */
    while (len > 0 && (text[len - 1] == '\0' || isspace((unsigned char)text[len - 1]))) {
        len--;
    }
    text[len] = '\0';
    if (len == 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    switch (chr) {
    case CHR_HEARTBEAT:
        s_cb.on_heartbeat(text);
        break;
    case CHR_COMING:
        s_cb.on_coming(text);
        break;
    case CHR_COMMAND:
        s_cb.on_command(text);
        break;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

#define WRITE_PAIRED (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)

static const struct ble_gatt_svc_def GATT_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = &UUID_HEARTBEAT.u, .access_cb = chr_access, .arg = (void *)CHR_HEARTBEAT, .flags = WRITE_PAIRED},
            {.uuid = &UUID_COMING.u, .access_cb = chr_access, .arg = (void *)CHR_COMING, .flags = WRITE_PAIRED},
            {.uuid = &UUID_COMMAND.u, .access_cb = chr_access, .arg = (void *)CHR_COMMAND, .flags = WRITE_PAIRED},
            {
                .uuid = &UUID_STATE.u,
                .access_cb = chr_access,
                .arg = (void *)CHR_STATE,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN |
                         BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_state_handle,
            },
            {0},
        },
    },
    {0},
};

void tag_ble_set_state(tag_state_t state)
{
    s_state = state;
    uint16_t targets[MAX_CONNS];
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_CONNS; i++) {
        if (s_conns[i].used && s_conns[i].subscribed) {
            targets[n++] = s_conns[i].handle;
        }
    }
    xSemaphoreGive(s_lock);

    for (int i = 0; i < n; i++) {
        /* Só celulares pareados recebem o estado */
        if (conn_is_authenticated(targets[i])) {
            ble_gatts_notify(targets[i], s_state_handle);
        }
    }
}

/* ---------- GAP ---------- */

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = &UUID_SVC,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields: %d", rc);
        return;
    }

    /* O nome vai na resposta de scan: flags + UUID de 128 bits já ocupam o anúncio */
    const char *name = ble_svc_gap_device_name();
    struct ble_hs_adv_fields rsp = {
        .name = (const uint8_t *)name,
        .name_len = strlen(name),
        .name_is_complete = 1,
    };
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan rsp: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "adv start: %d", rc);
    }
}

static void advertise_if_room(void)
{
    int used = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_CONNS; i++) {
        used += s_conns[i].used;
    }
    xSemaphoreGive(s_lock);
    if (used < MAX_CONNS) {
        advertise();
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "conexão %s; handle=%d", event->connect.status == 0 ? "ok" : "falhou",
                 event->connect.conn_handle);
        if (event->connect.status == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            conn_get(event->connect.conn_handle, true);
            xSemaphoreGive(s_lock);
        }
        advertise_if_room();
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "desconectado; handle=%d motivo=0x%x", event->disconnect.conn.conn_handle,
                 event->disconnect.reason);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        conn_t *c = conn_get(event->disconnect.conn.conn_handle, false);
        if (c) {
            c->used = false;
        }
        xSemaphoreGive(s_lock);
        advertise_if_room();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise_if_room();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_state_handle) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            conn_t *sub = conn_get(event->subscribe.conn_handle, false);
            if (sub) {
                sub->subscribed = event->subscribe.cur_notify;
            }
            xSemaphoreGive(s_lock);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) {
            return 0;
        }
        if (!pairing_allowed()) {
            ESP_LOGW(TAG, "pareamento recusado: segure o BOOT por 3 s para liberar");
            ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        {
            struct ble_sm_io io = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = esp_random() % 1000000,
            };
            ESP_LOGI(TAG, "passkey exibido na tela");
            s_cb.on_passkey(io.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE: {
        bool ok = event->enc_change.status == 0 && conn_is_authenticated(event->enc_change.conn_handle);
        ESP_LOGI(TAG, "criptografia %s; handle=%d status=%d", ok ? "autenticada" : "falhou",
                 event->enc_change.conn_handle, event->enc_change.status);
        s_cb.on_pairing_done(ok);
        if (ok) {
            close_pairing_window();
        }
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* O celular perdeu o vínculo e quer parear de novo: só dentro da janela de pareamento */
        struct ble_gap_conn_desc desc;
        if (!pairing_allowed() || ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) != 0) {
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        return 0;
    }
}

static void on_sync(void)
{
    /* Privacidade: anuncia com endereço privado resolvível (RPA), gerado pelo controlador e
     * trocado periodicamente. Só celulares pareados (que receberam o IRK) reconhecem a tag. */
    int rc = ble_hs_id_infer_auto(1, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "endereço: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "anunciando como \"%s\" (endereço tipo %d)", ble_svc_gap_device_name(), s_own_addr_type);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "pilha BLE reiniciada; motivo=%d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------- Proximidade (RSSI) ---------- */

static proximity_t level_for_rssi(float rssi, proximity_t current)
{
    /* thresholds[i] = RSSI mínimo do nível i + 1 */
    static const int thresholds[PROXIMITY_LEVELS - 1] = {
        CONFIG_LUCAS_RSSI_APPROACHING,
        CONFIG_LUCAS_RSSI_NEAR,
        CONFIG_LUCAS_RSSI_VERY_NEAR,
        CONFIG_LUCAS_RSSI_MAX,
    };
    proximity_t level = PROXIMITY_FAR;
    for (int i = 0; i < PROXIMITY_LEVELS - 1; i++) {
        /* Histerese: subir exige um pouco mais que o limite, descer exige um pouco menos */
        int limit = thresholds[i] + (i + 1 > (int)current ? RSSI_HYSTERESIS_DB : -RSSI_HYSTERESIS_DB);
        if (rssi >= limit) {
            level = i + 1;
        }
    }
    return level;
}

static void proximity_task(void *arg)
{
    bool reported_connected = false;
    proximity_t level = PROXIMITY_FAR;
    proximity_t candidate = PROXIMITY_FAR;
    int candidate_count = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PROX_PERIOD_MS));

        if (s_pairing_until_us != 0 && esp_timer_get_time() >= s_pairing_until_us) {
            ESP_LOGI(TAG, "janela de pareamento encerrada");
            close_pairing_window();
        }

        uint16_t handles[MAX_CONNS];
        int n = 0;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < MAX_CONNS; i++) {
            if (s_conns[i].used) {
                handles[n++] = s_conns[i].handle;
            }
        }
        xSemaphoreGive(s_lock);

        bool connected = false;
        float best = -127;
        for (int i = 0; i < n; i++) {
            int8_t rssi;
            if (!conn_is_authenticated(handles[i]) || ble_gap_conn_rssi(handles[i], &rssi) != 0) {
                continue;
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            conn_t *c = conn_get(handles[i], false);
            if (c) {
                c->rssi = c->has_rssi ? c->rssi + RSSI_ALPHA * (rssi - c->rssi) : rssi;
                c->has_rssi = true;
                if (c->rssi > best) {
                    best = c->rssi;
                }
                connected = true;
            }
            xSemaphoreGive(s_lock);
        }

        if (!connected) {
            if (reported_connected) {
                reported_connected = false;
                candidate_count = 0;
                s_cb.on_proximity(false, PROXIMITY_FAR);
            }
            continue;
        }

        proximity_t raw = level_for_rssi(best, level);
        candidate_count = raw == candidate ? candidate_count + 1 : 1;
        candidate = raw;
        bool changed = false;
        if (!reported_connected) {
            /* Primeira leitura: reporta logo, sem esperar a confirmação */
            level = raw;
            changed = true;
        } else if (raw != level && candidate_count >= PROX_DWELL_SAMPLES) {
            level = raw;
            changed = true;
        }
        if (changed) {
            ESP_LOGI(TAG, "proximidade: nível %d (RSSI médio %.0f dBm)", level, best);
            reported_connected = true;
            s_cb.on_proximity(true, level);
        }
    }
}

/* ---------- Inicialização ---------- */

void ble_store_config_init(void);

esp_err_t tag_ble_init(const tag_ble_callbacks_t *callbacks)
{
    s_cb = *callbacks;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(GATT_SERVICES);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(GATT_SERVICES);
    }
    if (rc == 0) {
        rc = ble_svc_gap_device_name_set(CONFIG_LUCAS_BLE_NAME);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT: %d", rc);
        return ESP_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(host_task);

    if (xTaskCreate(proximity_task, "ble_prox", 3072, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
