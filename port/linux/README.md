# Raspberry Pi build (HDMI)

The same weather station, running on a Raspberry Pi with an HDMI display instead
of a Waveshare panel. Developed against the **Pi Zero W**, but nothing here is
specific to it — a Zero 2 W, a 3 or a 4 will all work and will all be faster.

**Not tested on hardware yet.** It is written, it is complete, and it has not
been run on a Pi. Treat the first build as bring-up rather than as an install.

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
                 libcurl4-openssl-dev libcjson-dev libdrm-dev libinput-dev
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

Optional pointer input, if a touchscreen or mouse is attached:

```bash
LV_EVDEV=/dev/input/event0 ./build/esp32-weather
```

There is deliberately no input by default. An HDMI monitor usually has no touch
panel, and the weather screen is a display rather than a control surface — the
settings and Wi-Fi screens are the only places a pointer is needed.

## The screen size question

**The interface is drawn at a fixed 800×480 and does not reflow.** Positions are
absolute pixel coordinates throughout `ui_weather.c`. On a larger monitor the
layout sits in the top-left corner; on a smaller one it is clipped. The program
says so at startup rather than leaving it to be worked out.

Pin the HDMI mode in `/boot/firmware/config.txt`:

```
hdmi_group=2
hdmi_mode=87
hdmi_cvt=800 480 60 6 0 0 0
hdmi_force_hotplug=1
```

Reboot afterwards. Not every monitor accepts 800×480 — many will letterbox it or
refuse — so a display that does is worth choosing deliberately. Making the
layout resolution-independent is the obvious future work and is a real job: it
means replacing every absolute coordinate with a layout.

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
ExecStart=/home/pi/esp32_weather/port/linux/build/esp32-weather
Restart=always
RestartSec=10
User=pi
SupplementaryGroups=video render

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now weather
```

Log output goes to the journal, without colour escapes — the program checks
whether stderr is a terminal before colouring anything.

## Known gaps

- **Not yet run on hardware.** Expect bring-up problems, most likely in DRM
  setup and in the pinned HDMI mode.
- **Fixed 800×480**, as above.
- **No serial console.** The ESP32 build has a REPL for tuning panel timings and
  polling the touch controller; neither applies here, and a Pi already has a
  shell.
