#include "hwprobe.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Pinos do ESP32-C6-Touch-LCD-1.47 (wiki da Waveshare). Na versão sem touch são pinos livres
 * do conector. O INT do touch (GPIO21) não é usado: na versão sem touch ele é o reset do LCD. */
#define PROBE_SDA      18
#define PROBE_SCL      19
#define PROBE_TP_RST   20
#define ADDR_AXS5106L  0x63
#define ADDR_QMI8658_A 0x6A
#define ADDR_QMI8658_B 0x6B

static const char *TAG = "hwprobe";

static const char *known_name(uint16_t addr)
{
    switch (addr) {
    case ADDR_AXS5106L:
        return "touch AXS5106L";
    case ADDR_QMI8658_A:
    case ADDR_QMI8658_B:
        return "IMU QMI8658";
    default:
        return "";
    }
}

bool hwprobe_touch(char *report, size_t len)
{
    /* Tira o touch do reset, caso exista */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << PROBE_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(PROBE_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = PROBE_SDA,
        .scl_io_num = PROBE_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        snprintf(report, len, "Erro ao iniciar o I2C");
        gpio_reset_pin(PROBE_TP_RST);
        return false;
    }

    bool touch = false;
    int found = 0;
    int pos = snprintf(report, len, "I2C nos GPIO%d/%d:\n", PROBE_SDA, PROBE_SCL);
    for (uint16_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 20) != ESP_OK) {
            continue;
        }
        found++;
        touch |= addr == ADDR_AXS5106L;
        ESP_LOGI(TAG, "dispositivo em 0x%02X %s", addr, known_name(addr));
        if (pos >= 0 && (size_t)pos < len) {
            pos += snprintf(report + pos, len - pos, "0x%02X %s\n", addr, known_name(addr));
        }
    }
    if (found == 0 && pos >= 0 && (size_t)pos < len) {
        snprintf(report + pos, len - pos, "nenhum dispositivo\n(sem touch e sem IMU)");
    }
    ESP_LOGI(TAG, "%d dispositivo(s); touch %s", found, touch ? "ENCONTRADO" : "não encontrado");

    i2c_del_master_bus(bus);
    gpio_reset_pin(PROBE_SDA);
    gpio_reset_pin(PROBE_SCL);
    gpio_reset_pin(PROBE_TP_RST);
    return touch;
}
