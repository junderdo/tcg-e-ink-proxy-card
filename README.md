# tcg-e-ink-proxy-card

An ESP32-S3 driving a Waveshare 3.6" e-Paper HAT+ (E) — 600x400, six colors
(black, white, yellow, red, blue, green) — to show PNG graphics.

Reference: [Waveshare wiki](https://www.waveshare.com/wiki/3.6inch_e-Paper_HAT%2B_(E)_Manual#ESP32)

## Layout

| Path | What |
| --- | --- |
| `firmware/` | ESP-IDF project |
| `firmware/components/epd_3in6e/` | Panel driver, ported from Waveshare's Arduino demo to hardware SPI |
| `firmware/main/` | App that accepts images over BLE and shows each one as it arrives |
| `tools/png_to_epd.py` | Converts an image into a panel frame buffer (`.bin`) |
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
board's USB port with `usbipd` first. Use `flash`, not `app-flash`, whenever
the partition table changes.

The firmware doesn't refresh the panel at boot; it keeps whatever it last
showed until an image is uploaded (see below). `images/test-pattern.png` has an
arrow marking the top, a red square at the top-left, a gray dither ramp, and
the six colors — upload it to confirm wiring, orientation, and colors. The log
reports how long the refresh took; a `busy timeout` usually means a wiring
problem.

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

This writes `images/my-image.bin`. Check `/tmp/preview.png` for the dithered
result, then upload it over BLE (below). Pass `--no-dither` for hard-edged
graphics. The image stays on screen with the board unpowered.

## Upload an image over BLE

The board advertises as **TCG Proxy Card**. A connected client can send an
image, which the firmware shows. Booting doesn't refresh the panel, so the
last image stays on screen. See [docs/ble-image-upload.md](docs/ble-image-upload.md)
for the protocol.

From a computer with Bluetooth (uses the same conversion as `png_to_epd.py`):

```sh
.venv/bin/python tools/ble_upload.py images/my-image.png
```

## Panel care

- Don't leave the panel powered between refreshes; the firmware puts it to
  sleep and cuts power.
- Refresh no more often than every 180 s (the firmware rejects uploads sooner,
  counting from boot too, since it can't tell how long ago the last refresh
  was), and at least once every 24 h when in regular use.
- Clear to white before long-term storage.
