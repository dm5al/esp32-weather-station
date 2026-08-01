#!/usr/bin/env python3
"""Render SVG mockups of the three screens into docs/img/.

These are NOT photographs. They are drawn from the same layout constants and
palette the firmware uses (main/ui/ui.h, ui_weather.c, weather_icon.c), so they
show what the code lays out rather than what a camera saw. Regenerate with:

    python tools/gen_mockups.py
"""
import os

W, H = 800, 480
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "img")

# ---- palette (main/ui/ui.h) -------------------------------------------------
BG      = "#0F172A"
CARD    = "#1E293B"
CARD_HI = "#334155"
TEXT    = "#F1F5F9"
MUTED   = "#94A3B8"
ACCENT  = "#38BDF8"
WARM    = "#FBBF24"
COOL    = "#60A5FA"
DAYOFF  = "#FF6B6B"

FONT = "Segoe UI,Roboto,Helvetica,Arial,sans-serif"

# ---- sky tints (main/ui/weather_icon.c) -------------------------------------
SKY = {
    "sun":     ("#1E88E5", "#7FD1F5"),
    "partly":  ("#2B84D8", "#86C8EE"),
    "cloud":   ("#46608A", "#8AA4C4"),
    "rain":    ("#27486F", "#5B85B4"),
    "drizzle": ("#2F5580", "#6E9AC4"),
    "storm":   ("#1B2440", "#44507A"),
}


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.parts = []
        self.defs = []

    def rect(self, x, y, w, h, fill, r=0, opacity=None):
        o = f' opacity="{opacity}"' if opacity is not None else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"{o}/>')

    def circle(self, cx, cy, r, fill, opacity=None):
        o = f' opacity="{opacity}"' if opacity is not None else ""
        self.parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"{o}/>')

    def text(self, x, y, s, size, fill, anchor="start", weight="400"):
        self.parts.append(
            f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" '
            f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{esc(s)}</text>')

    def grad(self, name, c1, c2):
        self.defs.append(
            f'<linearGradient id="{name}" x1="0" y1="0" x2="0" y2="1">'
            f'<stop offset="0" stop-color="{c1}"/><stop offset="1" stop-color="{c2}"/>'
            f'</linearGradient>')

    def render(self):
        defs = f"<defs>{''.join(self.defs)}</defs>" if self.defs else ""
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}">{defs}'
                f'<rect width="{self.w}" height="{self.h}" fill="{BG}"/>'
                + "".join(self.parts) + "</svg>")


def cloud(svg, x, y, w, fill):
    """Three puffs on a slab, matching draw_cloud() proportions."""
    h = w * 0.62
    slab = h * 0.45
    svg.rect(x, y + h - slab, w, slab, fill, r=slab / 2)
    svg.circle(x + w * 0.18 + w * 0.275, y + w * 0.275, w * 0.275, fill)
    svg.circle(x + w * 0.58 + w * 0.20, y + h * 0.22 + w * 0.20, w * 0.20, fill)
    svg.circle(x + w * 0.17, y + h * 0.28 + w * 0.17, w * 0.17, fill)


def icon(svg, kind, x, y, s, idx):
    """Weather glyph on its sky tile, mirroring weather_icon_create()."""
    key = {"sun": "sun", "partly": "partly", "cloud": "cloud",
           "rain": "rain", "drizzle": "drizzle", "storm": "storm"}[kind]
    gid = f"sky{idx}"
    svg.grad(gid, *SKY[key])
    svg.rect(x, y, s, s, f"url(#{gid})", r=s / 5)

    if kind == "sun":
        r = s * 0.22
        cx, cy = x + s / 2, y + s / 2
        svg.circle(cx, cy, r * 1.25, "#FFF06B", opacity="0.30")
        ray_l, ray_t, gap = r * 0.70, max(2, r * 0.32), r * 0.40
        svg.rect(cx - ray_t / 2, cy - r - gap - ray_l, ray_t, ray_l, "#FFE066", r=ray_t / 2)
        svg.rect(cx - ray_t / 2, cy + r + gap, ray_t, ray_l, "#FFE066", r=ray_t / 2)
        svg.rect(cx - r - gap - ray_l, cy - ray_t / 2, ray_l, ray_t, "#FFE066", r=ray_t / 2)
        svg.rect(cx + r + gap, cy - ray_t / 2, ray_l, ray_t, "#FFE066", r=ray_t / 2)
        svg.grad(f"sund{idx}", "#FFF06B", "#FF8A00")
        svg.circle(cx, cy, r, f"url(#sund{idx})")
        return

    svg.grad(f"cl{idx}", "#FFFFFF", "#BFD4EE")
    white = f"url(#cl{idx})"

    if kind == "partly":
        r = s * 0.15
        cx, cy = x + s * 0.34, y + s * 0.32
        svg.circle(cx, cy, r * 1.25, "#FFF06B", opacity="0.30")
        svg.grad(f"sund{idx}", "#FFF06B", "#FF8A00")
        svg.circle(cx, cy, r, f"url(#sund{idx})")
        cloud(svg, x + s * 0.20, y + s * 0.42, s * 0.72, white)
    elif kind == "cloud":
        svg.grad(f"st{idx}", "#93A9C9", "#5C7397")
        cloud(svg, x + s * 0.26, y + s * 0.14, s * 0.60, f"url(#st{idx})")
        cloud(svg, x + s * 0.04, y + s * 0.34, s * 0.74, white)
    elif kind == "storm":
        svg.grad(f"st{idx}", "#93A9C9", "#5C7397")
        cloud(svg, x + s * 0.12, y + s * 0.06, s * 0.76, f"url(#st{idx})")
        svg.text(x + s / 2, y + s * 0.86, "⚡", s * 0.34, "#FFE83D", anchor="middle")
    else:  # rain / drizzle
        n = 3 if kind == "rain" else 2
        cloud(svg, x + s * 0.12, y + s * 0.08, s * 0.76, white)
        svg.grad(f"rn{idx}", "#9BE3FF", "#0284C7")
        span, left = s * 0.72, x + s * 0.14
        slot = span / n
        dw = max(3, slot * 0.26)
        for i in range(n):
            dx = left + slot * i + slot / 2 - dw / 2
            dy = y + s * 0.60 + (dw * 1.5 if i % 2 else 0)
            svg.rect(dx, dy, dw, dw * 3, f"url(#rn{idx})", r=dw / 2)


def header(svg, city, region, clock, date, link):
    svg.text(24, 32, city, 28, TEXT, weight="600")
    svg.text(24, 51, region, 14, MUTED)
    svg.text(400, 32, clock, 28, TEXT, anchor="middle", weight="600")
    svg.text(400, 52, date, 14, MUTED, anchor="middle")
    svg.text(650, 26, link, 16, MUTED, anchor="end")
    svg.text(650, 47, "Updated 14:00", 12, MUTED, anchor="end")
    for bx, glyph in ((664, "↻"), (724, "⚙")):
        svg.rect(bx, 10, 52, 40, CARD, r=10)
        svg.text(bx + 26, 36, glyph, 18, TEXT, anchor="middle")


def weather_screen():
    svg = Svg(W, H)
    header(svg, "Bad Marienberg", "Rheinland-Pfalz, Germany",
           "14:32", "31.07.2026", "● Dm5al  -52 dBm")

    svg.rect(12, 64, 776, 196, CARD, r=14)
    icon(svg, "cloud", 36, 98, 128, 0)
    svg.text(192, 140, "21°", 48, TEXT, weight="600")
    svg.text(192, 181, "Overcast", 24, ACCENT)
    svg.text(192, 212, "Feels like 20°C", 16, MUTED)

    stats = [("FEELS LIKE", "20 °C"), ("HUMIDITY", "78 %"), ("WIND", "3.4 m/s"),
             ("PRESSURE", "1013 hPa"), ("PRECIPITATION", "0.4 mm"),
             ("SUNRISE / SUNSET", "05:48 / 21:16")]
    for i, (name, val) in enumerate(stats):
        x = 424 + (i % 3) * 122
        y = 98 + (i // 3) * 72
        svg.text(x, y + 10, name, 12, MUTED)
        svg.text(x, y + 35, val, 18, TEXT)

    days = [
        ("Today", "31.07", "cloud", "22°", "14°", "40%", None, None),
        ("Sat", "01.08", "rain", "19°", "13°", "80%", WARM, None),
        ("Sun", "02.08", "drizzle", "20°", "12°", "60%", DAYOFF, None),
        ("Mon", "03.08", "partly", "23°", "13°", "20%", None, None),
        ("Tue", "04.08", "sun", "26°", "15°", "", None, None),
        ("Wed", "05.08", "sun", "27°", "16°", "", None, None),
        ("Thu", "06.08", "storm", "24°", "15°", "70%", None, None),
    ]
    for i, (nm, dt, kind, hi, lo, pp, mark, hol) in enumerate(days):
        x = 12 + i * 112
        svg.rect(x, 270, 104, 196, CARD, r=14)
        if mark:
            svg.rect(x, 270, 104, 4, mark, r=2)
        svg.text(x + 52, 292, nm, 18, mark or TEXT, anchor="middle", weight="600")
        svg.text(x + 52, 311, dt, 12, MUTED, anchor="middle")
        icon(svg, kind, x + 28, 316, 48, i + 1)
        svg.text(x + 52, 388, hi, 20, TEXT, anchor="middle", weight="600")
        svg.text(x + 52, 410, lo, 16, MUTED, anchor="middle")
        if pp:
            svg.text(x + 52, 430, "☁ " + pp, 14, COOL, anchor="middle")
        if hol:
            svg.text(x + 52, 448, hol, 12, WARM, anchor="middle")
    return svg


def holiday_screen():
    """Same layout with a holiday in view, which the live window did not have."""
    svg = Svg(W, H)
    header(svg, "Bad Marienberg", "Rheinland-Pfalz, Germany",
           "09:05", "02.06.2026", "● Dm5al  -49 dBm")

    svg.rect(12, 64, 776, 196, CARD, r=14)
    icon(svg, "partly", 36, 98, 128, 0)
    svg.text(192, 140, "24°", 48, TEXT, weight="600")
    svg.text(192, 181, "Partly cloudy", 24, ACCENT)
    svg.text(192, 212, "Feels like 25°C", 16, MUTED)

    stats = [("FEELS LIKE", "25 °C"), ("HUMIDITY", "54 %"), ("WIND", "2.1 m/s"),
             ("PRESSURE", "1018 hPa"), ("PRECIPITATION", "0.0 mm"),
             ("SUNRISE / SUNSET", "05:19 / 21:38")]
    for i, (name, val) in enumerate(stats):
        x = 424 + (i % 3) * 122
        y = 98 + (i // 3) * 72
        svg.text(x, y + 10, name, 12, MUTED)
        svg.text(x, y + 35, val, 18, TEXT)

    days = [
        ("Today", "02.06", "partly", "24°", "13°", "", None, None),
        ("Wed", "03.06", "sun", "26°", "14°", "", None, None),
        ("Thu", "04.06", "sun", "27°", "15°", "", DAYOFF, "Fronleichnam"),
        ("Fri", "05.06", "partly", "25°", "14°", "20%", None, None),
        ("Sat", "06.06", "rain", "21°", "13°", "70%", WARM, None),
        ("Sun", "07.06", "drizzle", "20°", "12°", "50%", DAYOFF, None),
        ("Mon", "08.06", "cloud", "22°", "13°", "30%", None, None),
    ]
    for i, (nm, dt, kind, hi, lo, pp, mark, hol) in enumerate(days):
        x = 12 + i * 112
        svg.rect(x, 270, 104, 196, CARD, r=14)
        if mark:
            svg.rect(x, 270, 104, 4, mark, r=2)
        svg.text(x + 52, 292, nm, 18, mark or TEXT, anchor="middle", weight="600")
        svg.text(x + 52, 311, dt, 12, MUTED, anchor="middle")
        icon(svg, kind, x + 28, 316, 48, i + 20)
        svg.text(x + 52, 388, hi, 20, TEXT, anchor="middle", weight="600")
        svg.text(x + 52, 410, lo, 16, MUTED, anchor="middle")
        if pp:
            svg.text(x + 52, 430, "☁ " + pp, 14, COOL, anchor="middle")
        if hol:
            svg.text(x + 52, 449, hol, 11, DAYOFF, anchor="middle")
    return svg


def wifi_screen():
    svg = Svg(W, H)
    svg.rect(40, 20, 150, 48, CARD_HI, r=10)
    svg.text(115, 50, "‹  Back", 18, TEXT, anchor="middle")
    svg.text(210, 46, "Choose a Wi-Fi network", 28, TEXT, weight="600")
    svg.rect(570, 20, 190, 48, CARD_HI, r=10)
    svg.text(665, 50, "↻  Rescan", 18, TEXT, anchor="middle")

    svg.rect(40, 84, 720, 330, CARD, r=14)
    nets = [("Dm5al", "-52 dBm", True), ("FRITZ!Box 7590", "-61 dBm", True),
            ("Vodafone-2C4A", "-70 dBm", True), ("Gaeste-WLAN", "-74 dBm", False),
            ("o2-WLAN-8823", "-81 dBm", True)]
    for i, (ssid, rssi, sec) in enumerate(nets):
        y = 96 + i * 62
        if i == 0:
            svg.rect(48, y, 704, 56, CARD_HI, r=8)
        svg.text(68, y + 36, "●", 18, ACCENT)
        svg.text(96, y + 36, ssid, 20, TEXT)
        svg.text(736, y + 34, ("⌨  " if sec else "") + rssi, 16, MUTED, anchor="end")
    svg.text(40, 444, "Tap a network to connect.", 18, MUTED)
    return svg


def settings_screen():
    svg = Svg(W, H)
    svg.rect(40, 20, 150, 48, CARD_HI, r=10)
    svg.text(115, 50, "‹  Back", 18, TEXT, anchor="middle")
    svg.text(210, 46, "Settings", 28, TEXT, weight="600")

    svg.rect(40, 90, 720, 156, CARD, r=14)
    svg.text(64, 116, "Language", 20, TEXT, weight="600")
    svg.text(64, 142, "Interface language", 14, MUTED)
    for i, (name, active) in enumerate([("English", True), ("Русский", False),
                                        ("Deutsch", False)]):
        x = 64 + i * 230
        svg.rect(x, 174, 210, 52, ACCENT if active else CARD_HI, r=10)
        svg.text(x + 105, 207, name, 20, BG if active else TEXT, anchor="middle")

    svg.rect(40, 266, 720, 150, CARD, r=14)
    svg.text(64, 292, "Wi-Fi", 20, TEXT, weight="600")
    svg.text(64, 318, "Choose or change the network", 14, MUTED)
    svg.rect(64, 348, 320, 52, ACCENT, r=10)
    svg.text(224, 381, "Choose a Wi-Fi network", 18, BG, anchor="middle")
    svg.text(432, 381, "●  Dm5al", 16, MUTED)
    return svg


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in (("weather-screen", weather_screen),
                     ("weather-holiday", holiday_screen),
                     ("wifi-screen", wifi_screen),
                     ("settings-screen", settings_screen)):
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(fn().render())
        print("wrote", path, os.path.getsize(path), "bytes")


if __name__ == "__main__":
    main()
