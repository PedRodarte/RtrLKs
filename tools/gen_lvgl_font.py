#!/usr/bin/env python3
"""Gera uma fonte LVGL (formato lv_font_fmt_txt, 4 bpp, sem compressão) a partir de uma TTF.

Substitui o lv_font_conv (Node.js) para este projeto. A saída tem a mesma estrutura dos
arquivos lv_font_montserrat_*.c do LVGL 9.

Uso:
    pip install pillow
    python tools/gen_lvgl_font.py --ttf tools/fonts/Roboto-Variable.ttf --wght 400 --wdth 100 --size 16 \
        --name lucas_font_16 --range 0x20-0x7E --range 0xA0-0xFF -o main/fonts/lucas_font_16.c

Sem --range, usa ASCII imprimível + Latin-1 (0x20-0x7E, 0xA0-0xFF).
"""

import argparse
import os

from PIL import Image, ImageDraw, ImageFont

BPP = 4


def parse_range(text):
    if "-" in text:
        a, b = text.split("-", 1)
        return int(a, 0), int(b, 0)
    v = int(text, 0)
    return v, v


def merge_ranges(ranges):
    cps = sorted({c for a, b in ranges for c in range(a, b + 1)})
    out = []
    for c in cps:
        if out and c == out[-1][1] + 1:
            out[-1][1] = c
        else:
            out.append([c, c])
    return [(a, b) for a, b in out]


def render_glyph(font, cp):
    ch = chr(cp)
    adv_w = round(font.getlength(ch) * 16)
    left, top, right, bottom = font.getbbox(ch, anchor="ls")
    w, h = right - left, bottom - top
    if w <= 0 or h <= 0:
        return adv_w, 0, 0, 0, 0, b""

    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-left, -top), ch, font=font, fill=255, anchor="ls")

    # Recorta linhas/colunas vazias que o bbox do FreeType às vezes inclui
    bbox = img.getbbox()
    if bbox is None:
        return adv_w, 0, 0, 0, 0, b""
    img = img.crop(bbox)
    left += bbox[0]
    bottom = top + bbox[3]
    w, h = img.size

    # 4 bpp, nibble alto primeiro, linhas contíguas (sem alinhamento por linha)
    nibbles = [min(15, (v + 8) // 17) for v in img.tobytes()]
    if len(nibbles) % 2:
        nibbles.append(0)
    data = bytes((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))
    return adv_w, w, h, left, -bottom, data


def ink_height(font, ch):
    _, top, _, _ = font.getbbox(ch, anchor="ls")
    return max(0, -top)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ttf", required=True)
    ap.add_argument("--size", type=int, required=True, help="altura em pixels")
    ap.add_argument("--wght", type=float, help="fonte variável: peso (100-900)")
    ap.add_argument("--wdth", type=float, help="fonte variável: largura (ex. 75 condensada, 100 normal)")
    ap.add_argument("--name", required=True, help="nome do símbolo C (lv_font_t)")
    ap.add_argument("--range", action="append", help="faixa de codepoints, ex. 0x20-0x7E (repetível)")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    ranges = merge_ranges([parse_range(r) for r in (args.range or ["0x20-0x7E", "0xA0-0xFF"])])
    font = ImageFont.truetype(args.ttf, args.size)
    if args.wght is not None or args.wdth is not None:
        wanted = {b"Weight": args.wght, b"Width": args.wdth}
        font.set_variation_by_axes([
            wanted.get(ax["name"]) if wanted.get(ax["name"]) is not None else ax["default"]
            for ax in font.get_variation_axes()
        ])
    ascent, descent = font.getmetrics()

    glyphs = []
    bitmap = bytearray()
    for a, b in ranges:
        for cp in range(a, b + 1):
            adv_w, w, h, ox, oy, data = render_glyph(font, cp)
            glyphs.append((cp, len(bitmap), adv_w, w, h, ox, oy))
            bitmap += data

    name = args.name
    guard = name.upper()
    opts = " ".join(f"-r {a:#x}-{b:#x}" for a, b in ranges)
    for axis in ("wght", "wdth"):
        if getattr(args, axis) is not None:
            opts = f"--{axis} {getattr(args, axis):g} {opts}"
    lines = []
    w = lines.append
    w("/*******************************************************************************")
    w(f" * Size: {args.size} px")
    w(f" * Bpp: {BPP}")
    w(f" * Gerado por tools/gen_lvgl_font.py --ttf {os.path.basename(args.ttf)} --size {args.size} {opts}")
    w(" ******************************************************************************/")
    w("")
    w('#include "lvgl.h"')
    w("")
    w(f"#ifndef {guard}")
    w(f"#define {guard} 1")
    w("#endif")
    w("")
    w(f"#if {guard}")
    w("")
    w("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    offsets = [g[1] for g in glyphs] + [len(bitmap)]
    for i, g in enumerate(glyphs):
        chunk = bitmap[offsets[i]:offsets[i + 1]]
        if not chunk:
            continue
        w(f"    /* U+{g[0]:04X} */")
        for j in range(0, len(chunk), 16):
            w("    " + ", ".join(f"0x{v:02x}" for v in chunk[j:j + 16]) + ",")
        w("")
    if not bitmap:
        w("    0x00")
    w("};")
    w("")
    w("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    w("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,")
    for cp, start, adv_w, gw, gh, ox, oy in glyphs:
        w(f"    {{.bitmap_index = {start}, .adv_w = {adv_w}, .box_w = {gw}, .box_h = {gh}, "
          f".ofs_x = {ox}, .ofs_y = {oy}}}, /* U+{cp:04X} */")
    w("};")
    w("")
    w("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    gid = 1
    for a, b in ranges:
        w("    {")
        w(f"        .range_start = {a}, .range_length = {b - a + 1}, .glyph_id_start = {gid},")
        w("        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
          ".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY")
        w("    },")
        gid += b - a + 1
    w("};")
    w("")
    w("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    w("    .glyph_bitmap = glyph_bitmap,")
    w("    .glyph_dsc = glyph_dsc,")
    w("    .cmaps = cmaps,")
    w("    .kern_dsc = NULL,")
    w("    .kern_scale = 0,")
    w(f"    .cmap_num = {len(ranges)},")
    w(f"    .bpp = {BPP},")
    w("    .kern_classes = 0,")
    w("    .bitmap_format = 0,")
    w("};")
    w("")
    w(f"const lv_font_t {name} = {{")
    w("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    w("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    w(f"    .line_height = {ascent + descent},")
    w(f"    .base_line = {descent},")
    w("#if LV_VERSION_CHECK(9, 6, 0) || LVGL_VERSION_MAJOR >= 10")
    w(f"    .cap_height = {ink_height(font, 'H')},")
    w(f"    .x_height = {ink_height(font, 'x')},")
    w("#endif")
    w("    .subpx = LV_FONT_SUBPX_NONE,")
    w("    .underline_position = -1,")
    w("    .underline_thickness = 1,")
    w("#if LV_VERSION_CHECK(9, 3, 0)")
    w("    .static_bitmap = 1,")
    w("#endif")
    w("    .dsc = &font_dsc,")
    w("    .fallback = NULL,")
    w("    .user_data = NULL,")
    w("};")
    w("")
    w(f"#endif /* {guard} */")
    w("")

    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{args.output}: {len(glyphs)} glifos, {len(bitmap)} bytes de bitmap, "
          f"line_height={ascent + descent}, base_line={descent}")


if __name__ == "__main__":
    main()
