#include "tag_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define NVS_NAMESPACE "findlk"
#define PHONE_LEN     16
#define URL_LEN       96

static const char *TAG = "tag_config";

static SemaphoreHandle_t s_lock;
static char s_phone[PHONE_LEN + 1];
static char s_url[URL_LEN + 1];

static void load(const char *key, char *out, size_t len)
{
    nvs_handle_t nvs;
    out[0] = '\0';
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t size = len;
        if (nvs_get_str(nvs, key, out, &size) != ESP_OK) {
            out[0] = '\0';
        }
        nvs_close(nvs);
    }
}

static esp_err_t save(const char *key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = value[0] ? nvs_set_str(nvs, key, value) : nvs_erase_key(nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; /* apagar o que não existe */
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t tag_config_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    load("phone", s_phone, sizeof(s_phone));
    load("url", s_url, sizeof(s_url));
    ESP_LOGI(TAG, "telefone %s, url %s", s_phone[0] ? "definido" : "vazio", s_url[0] ? s_url : "(padrão)");
    return ESP_OK;
}

static bool valid_phone(const char *v)
{
    size_t n = strlen(v);
    if (n == 0) {
        return true;
    }
    if (n < 10 || n > PHONE_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isdigit((unsigned char)v[i])) {
            return false;
        }
    }
    return true;
}

static bool valid_url(const char *v)
{
    /* O GPS do navegador só funciona em HTTPS; parâmetros são acrescentados pela tag */
    return v[0] == '\0' || (strncmp(v, "https://", 8) == 0 && strlen(v) <= URL_LEN && !strpbrk(v, "?# "));
}

esp_err_t tag_config_set(const char *assignment)
{
    const char *eq = strchr(assignment, '=');
    if (!eq || eq - assignment != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    const char key = assignment[0];
    const char *value = eq + 1;

    char *target;
    size_t target_len;
    const char *nvs_key;
    switch (key) {
    case 't':
        if (!valid_phone(value)) {
            return ESP_ERR_INVALID_ARG;
        }
        target = s_phone, target_len = sizeof(s_phone), nvs_key = "phone";
        break;
    case 'u':
        if (!valid_url(value)) {
            return ESP_ERR_INVALID_ARG;
        }
        target = s_url, target_len = sizeof(s_url), nvs_key = "url";
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = save(nvs_key, value);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        strlcpy(target, value, target_len);
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "%s atualizado", nvs_key);
    }
    return err;
}

static size_t append(char *out, size_t len, size_t pos, const char *a, const char *b)
{
    if (pos < len) {
        int n = snprintf(out + pos, len - pos, "%s%s", a, b);
        pos += n > 0 ? (size_t)n : 0;
    }
    return pos;
}

void tag_config_qr_url(char *out, size_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t pos = append(out, len, 0, s_url[0] ? s_url : CONFIG_FINDLK_QR_URL, "?");
    if (s_phone[0]) {
        pos = append(out, len, pos, "t=", s_phone);
        pos = append(out, len, pos, "&", "");
    }
    append(out, len, pos, "d=", CONFIG_FINDLK_BLE_NAME);
    xSemaphoreGive(s_lock);
}
