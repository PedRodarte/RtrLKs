#include "tag_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define NVS_NAMESPACE "findlk"
#define NAME_LEN      32
#define PHONE_LEN     16
#define URL_LEN       96

static const char *TAG = "tag_config";

static SemaphoreHandle_t s_lock;
static char s_name[NAME_LEN + 1];
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
    load("name", s_name, sizeof(s_name));
    load("phone", s_phone, sizeof(s_phone));
    load("url", s_url, sizeof(s_url));
    ESP_LOGI(TAG, "nome %s, telefone %s, url %s", s_name[0] ? "definido" : "vazio",
             s_phone[0] ? "definido" : "vazio", s_url[0] ? s_url : "(padrão)");
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
    case 'n':
        if (strlen(value) > NAME_LEN) {
            return ESP_ERR_INVALID_ARG;
        }
        target = s_name, target_len = sizeof(s_name), nvs_key = "name";
        break;
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

/* Codifica para URL: letras, dígitos e -._~ passam; o resto (inclusive UTF-8) vira %XX */
static size_t url_encode(char *out, size_t len, const char *in)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && pos + 4 < len; p++) {
        if (isalnum(*p) || strchr("-._~", *p)) {
            out[pos++] = (char)*p;
        } else {
            out[pos++] = '%';
            out[pos++] = HEX[*p >> 4];
            out[pos++] = HEX[*p & 0x0F];
        }
    }
    out[pos] = '\0';
    return pos;
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
    char name[NAME_LEN * 3 + 1];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    url_encode(name, sizeof(name), s_name);
    size_t pos = append(out, len, 0, s_url[0] ? s_url : CONFIG_FINDLK_QR_URL, "?");
    if (name[0]) {
        pos = append(out, len, pos, "n=", name);
        pos = append(out, len, pos, "&", "");
    }
    if (s_phone[0]) {
        pos = append(out, len, pos, "t=", s_phone);
        pos = append(out, len, pos, "&", "");
    }
    append(out, len, pos, "d=", CONFIG_FINDLK_BLE_NAME);
    xSemaphoreGive(s_lock);
}
