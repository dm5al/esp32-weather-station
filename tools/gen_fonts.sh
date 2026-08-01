#!/usr/bin/env bash
# Regenerate the UI fonts in main/ui/fonts/.
#
# The stock LVGL Montserrat fonts are ASCII + symbols only, which cannot render
# Russian or the German umlauts. Montserrat-Medium.ttf (shipped inside the LVGL
# component) does contain Cyrillic and Latin-1, so we rebuild it ourselves with
# the wider ranges rather than mixing in a second typeface.
#
# Needs node; lv_font_conv is fetched on demand by npx.
#
#   ./tools/gen_fonts.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT="$(dirname "$HERE")"
SRC="$PROJECT/managed_components/lvgl__lvgl/scripts/built_in_font"
OUT="$PROJECT/main/ui/fonts"

CONV="npx --yes lv_font_conv@1.5.3"

# ASCII + degree + bullet, Latin-1 supplement (aou umlauts, sharp s), Cyrillic.
TEXT_RANGE='0x20-0x7F,0xB0,0x2022,0xA0-0xFF,0x400-0x4FF'

# The LVGL built-in symbol set, copied verbatim from
# scripts/built_in_font/built_in_font_gen.py so LV_SYMBOL_* keeps working.
SYMS='61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650'

mkdir -p "$OUT"
cd "$SRC"

for size in 12 14 16 18 20 24 28; do
    echo "==> lv_font_ui_${size}"
    $CONV --no-compress --no-prefilter --bpp 4 --size "$size" \
        --font Montserrat-Medium.ttf -r "$TEXT_RANGE" \
        --font 'FontAwesome5-Solid+Brands+Regular.woff' -r "$SYMS" \
        --format lvgl -o "$OUT/lv_font_ui_${size}.c" --force-fast-kern-format
done

# The 48px face only ever renders the big temperature readout, so it carries
# just digits, sign, separators and the degree sign. Full coverage at this size
# would cost ~60 KB of flash for glyphs nothing draws.
echo "==> lv_font_ui_48 (numerals only)"
$CONV --no-compress --no-prefilter --bpp 4 --size 48 \
    --font Montserrat-Medium.ttf -r '0x20,0x2B,0x2D,0x2E,0x30-0x3A,0xB0' \
    --format lvgl -o "$OUT/lv_font_ui_48.c" --force-fast-kern-format

echo
echo "Done. Generated:"
ls -la "$OUT"/*.c | awk '{printf "  %-28s %8d bytes\n", $9, $5}'
