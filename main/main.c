#include "app.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hwprobe.h"
#include "led.h"
#include "nvs_flash.h"
#include "ui.h"

static const char *TAG = "main";

#if CONFIG_LUCAS_DISPLAY_TEST
static void display_test_loop(void)
{
    static const char *const names[] = {"Mãe", "Pai", "Babá", "Parente"};
    const int name_count = sizeof(names) / sizeof(names[0]);

    for (int cycle = 0;; cycle++) {
        ESP_LOGI(TAG, "teste: padrão de cores/acentos (LED: verde respirando)");
        ui_show_test_pattern();
        led_show_normal();
        vTaskDelay(pdMS_TO_TICKS(8000));

        ESP_LOGI(TAG, "teste: tela PERDIDO (QR: %s) (LED: sirene)", CONFIG_LUCAS_QR_URL);
        ui_show_lost(CONFIG_LUCAS_QR_URL);
        led_show_nobody();
        vTaskDelay(pdMS_TO_TICKS(8000));

        /* Cada nível dura duas respirações do LED */
        for (proximity_t level = PROXIMITY_FAR; level < PROXIMITY_LEVELS; level++) {
            ESP_LOGI(TAG, "teste: %s chegando, nível %d", names[cycle % name_count], level);
            ui_show_approaching(names[cycle % name_count], level);
            led_show_approaching(level);
            vTaskDelay(pdMS_TO_TICKS(4000));
        }

        ESP_LOGI(TAG, "teste: passkey (LED: roxo respirando)");
        ui_show_passkey(123456);
        led_show_pairing();
        vTaskDelay(pdMS_TO_TICKS(4000));

        ESP_LOGI(TAG, "teste: tela e LED apagados");
        ui_show_off();
        led_show_off();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#endif

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s versão %s", CONFIG_LUCAS_BLE_NAME, esp_app_get_description()->version);
    init_nvs();
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(led_init());

#if CONFIG_LUCAS_HW_PROBE
    char report[256];
    bool touch = hwprobe_touch(report, sizeof(report));
    ui_show_info(touch ? "Placa COM touch" : "Placa SEM touch", report);
    vTaskDelay(pdMS_TO_TICKS(10000));
#endif

#if CONFIG_LUCAS_DISPLAY_TEST
    display_test_loop();
#else
    ESP_ERROR_CHECK(app_start());
#endif
}
