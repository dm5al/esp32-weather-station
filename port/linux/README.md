# Raspberry Pi build (HDMI)

The same weather station, running on a Raspberry Pi with an HDMI display instead
of a Waveshare panel. Developed against the **Pi Zero W**, but nothing here is
specific to it — a Zero 2 W, a 3 or a 4 will all work and will all be faster.

Brought up on a Pi Zero W with a 1024x600 HDMI panel: DRM comes up at the
panel's size, the location resolves, and the forecast arrives.

## What is shared and what is not

The application, the network clients and the entire interface are the *same
source files* the ESP32 firmware builds — including `main/main.c`, which is
compiled unmodified. That was the point of the exercise: a second copy of the
refresh cadence and the fetch ordering would have drifted from the first within
a month.

What differs sits underneath:

| | ESP32 | Raspberry Pi |
|---|---|---|
| Display | RGB565 parallel panel | DRM, framebuffer or SDL |
| HTTP | `esp_http_client` | libcurl |
| Settings | NVS partition | files under `~/.config/esp32-weather` |
| Wi-Fi | `esp_wifi` | NetworkManager, via `nmcli` |
| Clock | its own SNTP client | the host's `systemd-timesyncd` |
| Threads, queue | FreeRTOS | pthreads (`port/linux/compat/freertos*`) |

The compatibility layer is thin and lives in `compat/`. It exists so the shared
code needs no `#ifdef`s in it.

## Building

On Raspberry Pi OS (Bookworm or later):

```bash
sudo apt install build-essential cmake pkg-config git \
                 libcurl4-openssl-dev libcjson-dev libdrm-dev
```

```bash
cd port/linux && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

LVGL 9.2.2 is fetched by CMake on the first configure, so that step needs a
network connection.

On a **Pi Zero W** this takes a long time — one 1 GHz ARMv6 core, and LVGL is
not a small library. Half an hour is normal. It only happens once, and building
on the Pi is far simpler than arranging a cross toolchain.

### Choosing a display backend

```bash
cmake -B build -DBSP_BACKEND=DRM     # default; Bookworm's KMS driver
cmake -B build -DBSP_BACKEND=FBDEV   # /dev/fb0, older images
cmake -B build -DBSP_BACKEND=SDL     # a window, for working on this on a desktop
```

## Running

```bash
./build/esp32-weather
```

With DRM the program needs the display, so it must not be running under a
desktop that already holds it. On a Pi set up with `raspi-config` to boot to
console this is already the case. The user needs to be in the `video` and
`render` groups.

A touchscreen is found automatically: the program looks for an input device
reporting absolute X and Y, which distinguishes a touch panel from a mouse.
Event numbers are assigned in probe order and change, so it asks the kernel
rather than trusting a path.

The device is `root:input`, so the account running this must be in the `input`
group — the packaged service asks for it. Override the choice if there is more
than one candidate:

```bash
LV_EVDEV=/dev/input/event0 ./build/esp32-weather
```

It runs perfectly well with no pointer at all: the weather screen is a display
rather than a control surface, and settings and Wi-Fi are the only places input
is needed.

## Screen size

**Defaults to 1024×600.** The layout is derived from the display size rather
than written in absolute coordinates, so it fills the panel: the cards and their
columns widen with the screen, and the extra height is shared between today's
card and the week in the proportion they already had.

For a different panel:

```bash
cmake -B build -DDISPLAY_W=1280 -DDISPLAY_H=800
```

It has to be told, rather than discovered: LVGL only reports the mode once the
display is open, which is after the interface has been laid out. If the two
disagree the program says so at startup and carries on, so a wrong `DISPLAY_W`
shows up as a warning in the log and a picture that does not quite fill the
screen, not as a puzzle.

**Fonts do not scale.** They are compiled-in bitmaps at fixed sizes, so a much
larger screen gets more whitespace rather than larger text. Up to about 1280
wide that reads as generous; past it, the font set wants a second size — which
is a job for the font generator, not the layout.

The ESP32 firmware is unaffected by any of this: `bsp/board.h` still fixes it at
800×480, and the arithmetic is arranged so every derived value comes out exactly
what it was when the layout was tuned by hand on that panel.

## Wi-Fi

The host owns the radio. The settings screen reads the current network and
signal from the kernel, and can scan and join through `nmcli` when
NetworkManager is installed — which it is by default on Bookworm. Without it,
scanning reports as unsupported rather than appearing to work and failing.

"Forget network" is not implemented on purpose: on the ESP32 it erases one
credential this program stored itself, whereas here it would delete a system
profile the owner may depend on — possibly the one they are reaching the Pi
over. Use `nmcli con delete` if that is really what is wanted.

Nothing here goes through a shell. SSIDs and passwords reach `execvp()` as an
argv array, because both come from an on-screen keyboard and a broadcast SSID,
and `; rm -rf ~` is a legal network name.

## Running it as a service

```ini
# /etc/systemd/system/weather.service
[Unit]
Description=Weather station
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/home/pi/esp32-weather-station/port/linux/build/esp32-weather
Restart=always
RestartSec=10
User=pi
SupplementaryGroups=video render input

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now weather
```

Log output goes to the journal, without colour escapes — the program checks
whether stderr is a terminal before colouring anything.

## Known gaps

- The layout adapts to the display size, but the fonts do not: a much larger
  screen gets more whitespace rather than larger text.
- **No serial console.** The ESP32 build has a REPL for tuning panel timings and
  polling the touch controller; neither applies here, and a Pi already has a
  shell.
