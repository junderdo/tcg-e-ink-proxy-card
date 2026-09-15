# tcg-e-ink-display

An ESP32-S3 driving a Waveshare 3.6" e-Paper HAT+ (E) — 600x400, six colors
(black, white, yellow, red, blue, green) — to show PNG graphics.

Reference: [Waveshare wiki](https://www.waveshare.com/wiki/3.6inch_e-Paper_HAT%2B_(E)_Manual#ESP32)

## Layout

| Path | What |
| --- | --- |
| `firmware/` | ESP-IDF project |
| `firmware/components/epd_3in6e/` | Panel driver, ported from Waveshare's Arduino demo to hardware SPI |
| `firmware/main/` | App that shows the last uploaded image (or `image.bin`) and accepts new ones over BLE |
| `tools/png_to_epd.py` | Converts an image into `firmware/main/image.bin` |
| `tools/ble_upload.py` | Uploads an image over BLE |
| `docs/ble-image-upload.md` | BLE upload protocol |
| `images/` | Source PNGs |

## Wiring (Seeed XIAO ESP32-S3)

| e-Paper | XIAO ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| CS | GPIO1 |
| DC | GPIO2 |
| RST | GPIO3 |
| BUSY | GPIO4 |
| DIN | GPIO5 |
| CLK | GPIO6 |
| PWR | GPIO7 (D8) |

Change them under **e-Paper pins** in `idf.py menuconfig`.

## Build and flash

Requires ESP-IDF v5.5.

```sh
. ~/esp/v5.5.2/esp-idf/export.sh
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The target (`esp32s3`) comes from `sdkconfig.defaults`. Under WSL, attach the
board's USB port with `usbipd` first.

`images/test-pattern.png` has an arrow marking the top, a red square at the
top-left, a gray dither ramp, and the six colors — convert and flash it to
confirm wiring, orientation, and colors. The log reports how long the
refresh took; a `busy timeout` usually means a wiring problem.

## Show your own image

Any size works. The image is scaled to the panel height and centered, so a
card image such as Scryfall's 672x936 PNGs loses only a sliver of its left and
right edges; an image narrower than the panel is padded with white. Portrait
images fill 400x600 (the panel's native orientation); landscape ones fill
600x400. Transparency is composited onto white. Colors
are boosted (`--saturation 1.8 --contrast 1.2` by default; pass `1.0` to
disable) and dithered to the six panel colors.

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/png_to_epd.py images/my-image.png --preview /tmp/preview.png
```

This rewrites `firmware/main/image.bin`. Check `/tmp/preview.png` for the
dithered result, then build and flash. Pass `--no-dither` for hard-edged
graphics. The image stays on screen with the board unpowered.

## Upload an image over BLE

The board advertises as **TCG Proxy Card**. A connected client can send an
image, which the firmware saves to the `image` flash partition and shows; it
is shown again after a reboot. Until an image is uploaded, the built-in
`image.bin` is shown. See [docs/ble-image-upload.md](docs/ble-image-upload.md)
for the protocol.

From a computer with Bluetooth (uses the same conversion as `png_to_epd.py`):

```sh
.venv/bin/python tools/ble_upload.py images/my-image.png
```

The saved image lives in the `image` partition of `firmware/partitions.csv`,
which `idf.py flash` writes (`app-flash` alone doesn't). To go back to the
built-in image, erase it:

```sh
python -m esptool --chip esp32s3 -p /dev/ttyACM0 erase_region 0x190000 0x40000
```

## Panel care

- Don't leave the panel powered between refreshes; the firmware puts it to
  sleep and cuts power.
- Refresh no more often than every 180 s, and at least once every 24 h when in
  regular use.
- Clear to white before long-term storage.
