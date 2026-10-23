#pragma once

#include <stddef.h>

#include "esp_err.h"

/* Configuração feita pelo app (WhatsApp do responsável, URL da página),
 * guardada em NVS. Usada para montar a URL do QR code. */
esp_err_t tag_config_init(void);

/* Aplica "chave=valor": t=<telefone>, u=<url base>. Valor vazio apaga a chave.
 * Retorna ESP_ERR_INVALID_ARG se a chave ou o valor forem inválidos. */
esp_err_t tag_config_set(const char *assignment);

/* URL completa do QR: <base>?t=<telefone>&d=<nome da tag> */
void tag_config_qr_url(char *out, size_t len);
