#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "display.h"
#include "esp_lvgl_port.h"
#include "fonts/lucas_fonts.h"
#include "src/libs/qrcode/qrcodegen.h"

#define COLOR_LOST_TITLE lv_color_hex(0xFFD600) /* amarelo */
#define COLOR_STAY_HERE  lv_color_hex(0xFFD600) /* amarelo */
#define COLOR_PAIR_BG    lv_color_hex(0x0D2B5E)
#define QR_MARGIN        5 /* margem branca (quiet zone) em volta do QR */
#define QR_BOX_MAX       DISPLAY_H_RES
/* O painel tem cantos arredondados: conteúdo fica afastado das bordas superior/inferior */
#define CORNER_RADIUS    30
#define EDGE_PAD_V       8

static const char *const PROXIMITY_TEXT[PROXIMITY_LEVELS] = {
    [PROXIMITY_FAR]         = "Longe",
    [PROXIMITY_APPROACHING] = "Aproximando",
    [PROXIMITY_NEAR]        = "Próximo",
    [PROXIMITY_VERY_NEAR]   = "Muito perto",
    [PROXIMITY_MAX]         = "Sinal máximo",
};

/* Limpa a tela ativa e prepara um layout em coluna centralizado. */
static lv_obj_t *new_screen(lv_color_t bg, int32_t pad)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, pad, 0);
    lv_obj_set_style_pad_row(scr, 6, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return scr;
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static lv_obj_t *add_box(lv_obj_t *parent, int32_t w, int32_t h, lv_color_t color)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, color, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    return box;
}

static void show_begin(void)
{
    lvgl_port_lock(0);
}

static void show_end(void)
{
    lvgl_port_unlock();
    display_set_on(true);
}

void ui_show_off(void)
{
    display_set_on(false);
    lvgl_port_lock(0);
    new_screen(lv_color_black(), 0);
    lvgl_port_unlock();
}

void ui_show_test_pattern(void)
{
    show_begin();
    lv_obj_t *scr = new_screen(lv_color_black(), 6);

    /* Moldura de 1 px na borda exata da área visível: confere o offset X/Y */
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_set_ignore_layout(frame, true);
    lv_obj_set_size(frame, DISPLAY_H_RES, DISPLAY_V_RES);
    lv_obj_set_pos(frame, -6, -6);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(frame, lv_color_white(), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, CORNER_RADIUS, 0);

    add_label(scr, "Teste de tela", &lucas_font_22, lv_color_white());
    add_label(scr, "172 x 320", &lucas_font_14, lv_palette_main(LV_PALETTE_GREY));
    add_label(scr, "Ação, coração, pão\navó, você, já, aí\nÁÉÍÓÚ ÂÊÔ ÃÕ Ç\nàü ñ ¿¡ 25° nº",
              &lucas_font_16, lv_color_white());

    /* Barras de cor: se vermelho e azul trocarem, ative LUCAS_LCD_BGR */
    static const struct { uint32_t rgb; const char *name; } bars[] = {
        {0xFF0000, "Verm."}, {0x00FF00, "Verde"}, {0x0000FF, "Azul"},
    };
    lv_obj_t *row = add_box(scr, lv_pct(100), LV_SIZE_CONTENT, lv_color_black());
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); i++) {
        lv_obj_t *col = add_box(row, 48, LV_SIZE_CONTENT, lv_color_black());
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 2, 0);
        add_box(col, 44, 44, lv_color_hex(bars[i].rgb));
        add_label(col, bars[i].name, &lucas_font_14, lv_color_white());
    }

    /* Degradê de cinza: confere inversão de cores (preto deve ficar à esquerda) */
    lv_obj_t *grad = add_box(scr, lv_pct(100), 18, lv_color_black());
    lv_obj_set_style_bg_grad_color(grad, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_HOR, 0);
    show_end();
}

void ui_show_lost(const char *url)
{
    show_begin();
    /* Fundo preto: só o QR fica branco, para a câmera não perder o foco/exposição */
    lv_obj_t *scr = new_screen(lv_color_black(), 0);
    lv_obj_set_style_pad_ver(scr, 6, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Só maiúsculas: a altura reservada para acentos sobre maiúsculas pode ser encolhida */
    lv_obj_t *title = add_label(scr, "CRIANÇA PERDIDA", &lucas_font_black_40, COLOR_LOST_TITLE);
    lv_obj_set_style_text_line_space(title, -8, 0);

    /* O lv_qrcode desenha módulos inteiros e preenche a sobra com branco. Usando um
     * múltiplo exato do número de módulos, a margem branca fica só a QR_MARGIN. */
    size_t url_len = strlen(url);
    int32_t qr_size = QR_BOX_MAX - 2 * QR_MARGIN;
    int version = qrcodegen_getMinFitVersion(qrcodegen_Ecc_MEDIUM, url_len); /* mesmo ECC do lv_qrcode */
    if (version > 0) {
        int modules = qrcodegen_version2size(version);
        qr_size = qr_size / modules * modules;
    }

    lv_obj_t *qr_box = add_box(scr, qr_size + 2 * QR_MARGIN, qr_size + 2 * QR_MARGIN, lv_color_white());
    lv_obj_set_style_pad_all(qr_box, QR_MARGIN, 0);
    lv_obj_t *qr = lv_qrcode_create(qr_box);
    lv_qrcode_set_size(qr, qr_size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, url, url_len);

    lv_obj_t *body = add_label(scr, "Escaneie para avisar aos pais", &lucas_font_22, lv_color_white());
    lv_obj_set_style_pad_hor(body, 6, 0);
    show_end();
}

void ui_show_approaching(const char *name, proximity_t level)
{
    if (level >= PROXIMITY_LEVELS) {
        level = PROXIMITY_MAX;
    }

    char title[64];
    snprintf(title, sizeof(title), "%s está chegando:", name);

    show_begin();
    lv_obj_t *scr = new_screen(lv_color_black(), 8);
    lv_obj_set_style_pad_ver(scr, EDGE_PAD_V, 0);
    lv_obj_set_style_pad_row(scr, 4, 0);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    add_label(scr, title, &lucas_font_black_30, lv_color_white());

    /* Indicador estilo "sinal": barras crescentes, preenchidas até o nível atual */
    lv_obj_t *bars = add_box(scr, lv_pct(100), 60, lv_color_black());
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(bars, 6, 0);
    for (int i = 0; i < PROXIMITY_LEVELS; i++) {
        lv_obj_t *bar = add_box(bars, 22, 12 + i * 12, lv_color_white());
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_bg_opa(bar, i <= (int)level ? LV_OPA_COVER : LV_OPA_20, 0);
    }
    add_label(scr, PROXIMITY_TEXT[level], &lucas_font_16, lv_color_white());

    add_label(scr, LV_SYMBOL_WARNING, &lucas_font_icon_36, COLOR_STAY_HERE);
    lv_obj_t *stay = add_label(scr, "FIQUE PARADO", &lucas_font_black_40, COLOR_STAY_HERE);
    lv_obj_set_style_text_line_space(stay, -8, 0);
    show_end();
}

void ui_show_passkey(uint32_t passkey)
{
    char digits[8];
    snprintf(digits, sizeof(digits), "%06lu", (unsigned long)(passkey % 1000000));

    show_begin();
    lv_obj_t *scr = new_screen(COLOR_PAIR_BG, 8);
    lv_obj_set_style_pad_row(scr, 12, 0);
    add_label(scr, "Pareamento", &lucas_font_22, lv_color_white());
    add_label(scr, "Digite este código no celular:", &lucas_font_16, lv_color_white());
    add_label(scr, digits, &lucas_font_digits_40, lv_color_white());
    add_label(scr, "LUCAS-TAG", &lucas_font_14, lv_palette_lighten(LV_PALETTE_BLUE, 2));
    show_end();
}

void ui_show_info(const char *title, const char *body)
{
    show_begin();
    lv_obj_t *scr = new_screen(lv_color_black(), 8);
    lv_obj_set_style_pad_ver(scr, 24, 0);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 12, 0);
    add_label(scr, title, &lucas_font_black_30, COLOR_STAY_HERE);
    lv_obj_t *text = add_label(scr, body, &lucas_font_16, lv_color_white());
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_LEFT, 0);
    show_end();
}
