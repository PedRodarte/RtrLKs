#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Procura periodicamente os SSIDs de casa (CONFIG_FINDLK_HOME_SSIDS). Só escaneia, não conecta.
 * on_change é chamado (da task do Wi-Fi) quando a tag chega em casa ou sai.
 * Com a lista vazia, o Wi-Fi nem é iniciado. */
esp_err_t wifi_home_init(void (*on_change)(bool at_home));
