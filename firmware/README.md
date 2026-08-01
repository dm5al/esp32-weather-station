# Prebuilt firmware

Flash these to run the weather station without installing ESP-IDF.

Built from commit [`e69d43d`](../../commit/e69d43d) with ESP-IDF v5.5.4.

> **These images are for the 8 MB flash variant** of the Waveshare
> ESP32-S3-Touch-LCD-7 (ESP32-S3R8, 8 MB octal PSRAM). Check yours first:
>
> ```bash
> esptool.py --port COM14 flash_id
> ```
>
> If it reports **16 MB**, do not use these — build from source instead, with
> `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` in `sdkconfig.defaults`. Flashing an
> 8 MB-configured image to a 16 MB part boots, but the partition table stops
> short and you waste half the flash.

## Files

| File | Offset | Purpose |
|---|---|---|
| `esp32_weather-merged.bin` | `0x0` | everything in one image — **use this** |
| `bootloader.bin` | `0x0` | second-stage bootloader |
| `partition-table.bin` | `0x8000` | partition table |
| `esp32_weather.bin` | `0x10000` | application |

Verify the download before flashing:

```bash
sha256sum -c SHA256SUMS.txt
```

## Flashing

You need [esptool](https://github.com/espressif/esptool) — no full ESP-IDF
install required:

```bash
pip install esptool
```

Connect the board by USB-C and find its serial port (`COM*` on Windows,
`/dev/ttyUSB*` or `/dev/ttyACM*` on Linux, `/dev/cu.*` on macOS).

### One file, one command

```bash
esptool.py --chip esp32s3 --port COM14 --baud 460800 write_flash 0x0 esp32_weather-merged.bin
```

### Or the three images separately

```bash
esptool.py --chip esp32s3 --port COM14 --baud 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 esp32_weather.bin
```

### From a browser, no install at all

[ESP Launchpad](https://espressif.github.io/esp-launchpad/) can flash from
Chrome or Edge over WebSerial: open it, choose **DIY**, upload
`esp32_weather-merged.bin`, set the offset to `0x0` and flash.

## After flashing

The board reboots into the network picker. Tap your Wi-Fi network, enter the
password on the on-screen keyboard, and it will locate itself and fetch the
forecast — about ten seconds. Credentials are saved, so later boots go straight
to the weather screen.

## Troubleshooting

**`Invalid head of packet (0x…)` or `Serial data stream stopped`**
Serial noise, most often at high baud. Drop to `--baud 115200`. If it persists,
reseat the USB cable — a flaky connector can drop the port mid-write.

**`Failed to connect: No serial data received`**
Hold **BOOT**, tap **RESET**, release **BOOT**, then flash. Most boards enter
download mode automatically and never need this.

**Blank screen after flashing, or a shifted/trembling picture**
Panel timings vary between board revisions. Connect a serial terminal at
115200 baud and type `lcd` at the `weather>` prompt — see
[Tuning the panel timings](../README.md#tuning-the-panel-timings).

**Starting completely fresh**
Erasing removes saved Wi-Fi credentials, the language choice, the cached
location and any panel-timing overrides:

```bash
esptool.py --chip esp32s3 --port COM14 erase_flash
```

## Watching the log

Any serial terminal at **115200 baud** works — `idf.py monitor`, `screen`,
PuTTY, or the Arduino IDE's serial monitor. The same connection gives you the
`weather>` console for tuning panel timings and probing the touch controller.
