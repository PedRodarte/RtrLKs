#include "wifi_home.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX_HOME_SSIDS 4

static const char *TAG = "wifi_home";

static char s_ssids[MAX_HOME_SSIDS][33];
static int s_ssid_count;
static void (*s_on_change)(bool at_home);

/* "Casa;Casa 5G" -> {"Casa", "Casa 5G"}. Espaços no meio do nome são mantidos. */
static void parse_ssids(const char *list)
{
    const char *p = list;
    while (*p && s_ssid_count < MAX_HOME_SSIDS) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0 && len < sizeof(s_ssids[0])) {
            memcpy(s_ssids[s_ssid_count], p, len);
            s_ssids[s_ssid_count][len] = '\0';
            s_ssid_count++;
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
}

static bool home_visible(void)
{
    wifi_scan_config_t scan_cfg = {.show_hidden = false};
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        ESP_LOGW(TAG, "falha no scan");
        return false;
    }

    /* Lê um resultado por vez: em lugares com muitas redes, a de casa não fica de fora */
    uint16_t total = 0;
    esp_wifi_scan_get_ap_num(&total);
    wifi_ap_record_t record;
    while (esp_wifi_scan_get_ap_record(&record) == ESP_OK) {
        for (int j = 0; j < s_ssid_count; j++) {
            if (strcmp((const char *)record.ssid, s_ssids[j]) == 0) {
                ESP_LOGI(TAG, "rede de casa \"%s\" visível (RSSI %d)", s_ssids[j], record.rssi);
                esp_wifi_clear_ap_list(); /* libera os resultados não lidos */
                return true;
            }
        }
    }
    ESP_LOGI(TAG, "%d redes, nenhuma de casa", total);
    return false;
}

static void wifi_task(void *arg)
{
    bool at_home = false;
    int misses = 0;
    for (;;) {
        if (home_visible()) {
            misses = 0;
            if (!at_home) {
                at_home = true;
                s_on_change(true);
            }
        } else if (at_home && ++misses >= CONFIG_LUCAS_HOME_MISSES_TO_LEAVE) {
            /* Só sai de casa depois de algumas buscas seguidas sem ver a rede */
            at_home = false;
            s_on_change(false);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LUCAS_HOME_SCAN_PERIOD_S * 1000));
    }
}

esp_err_t wifi_home_init(void (*on_change)(bool at_home))
{
    s_on_change = on_change;
    parse_ssids(CONFIG_LUCAS_HOME_SSIDS);
    if (s_ssid_count == 0) {
        ESP_LOGI(TAG, "nenhuma rede de casa configurada: desarme por Wi-Fi desligado");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "modo");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    ESP_LOGI(TAG, "procurando %d rede(s) de casa a cada %d s", s_ssid_count, CONFIG_LUCAS_HOME_SCAN_PERIOD_S);
    ESP_RETURN_ON_FALSE(xTaskCreate(wifi_task, "wifi_home", 4096, NULL, 3, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "task");
    return ESP_OK;
}
