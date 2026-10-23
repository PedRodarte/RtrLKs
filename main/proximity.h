#pragma once

/* Proximidade do celular pareado mais próximo (medida pela tag via RSSI a partir da etapa 3) */
typedef enum {
    PROXIMITY_FAR,         /* longe */
    PROXIMITY_APPROACHING, /* aproximando */
    PROXIMITY_NEAR,        /* próximo */
    PROXIMITY_VERY_NEAR,   /* muito perto */
    PROXIMITY_MAX,         /* sinal máximo */
    PROXIMITY_LEVELS,
} proximity_t;
