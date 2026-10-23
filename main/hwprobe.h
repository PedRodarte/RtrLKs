#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Procura no I2C da versão touch (GPIO18/19) o touch AXS5106L e a IMU QMI8658.
 * Escreve um relatório legível em report e retorna true se achou o controlador de toque. */
bool hwprobe_touch(char *report, size_t len);
