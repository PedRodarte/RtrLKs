#!/bin/sh
# Regenera as fontes LVGL do projeto (ASCII + Latin-1). Requer: pip install pillow
set -e
cd "$(dirname "$0")/.."
PY=${PYTHON:-python}
TTF=tools/fonts/Roboto-Variable.ttf

# Frases complementares: peso 400, largura 100
for size in 14 16 22; do
    "$PY" tools/gen_lvgl_font.py --ttf "$TTF" --wght 400 --wdth 100 --size "$size" --name "lucas_font_$size" \
        -o "main/fonts/lucas_font_$size.c"
done

# Frases importantes ("CRIANÇA PERDIDA", "... está chegando", "FIQUE AQUI"): peso 900, largura 75
for size in 30 40; do
    "$PY" tools/gen_lvgl_font.py --ttf "$TTF" --wght 900 --wdth 75 --size "$size" --name "lucas_font_black_$size" \
        -o "main/fonts/lucas_font_black_$size.c"
done

# Símbolo de atenção (U+F071, LV_SYMBOL_WARNING) da FontAwesome 5 Free
"$PY" tools/gen_lvgl_font.py --ttf tools/fonts/FontAwesome5-Solid+Brands+Regular.woff --size 36 \
    --name lucas_font_icon_36 --range 0xF071 -o main/fonts/lucas_font_icon_36.c

# Dígitos grandes para o passkey de pareamento
"$PY" tools/gen_lvgl_font.py --ttf "$TTF" --wght 700 --wdth 100 --size 40 --name lucas_font_digits_40 \
    --range 0x20 --range 0x30-0x39 -o main/fonts/lucas_font_digits_40.c
