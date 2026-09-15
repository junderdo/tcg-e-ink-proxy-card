# BLE image upload protocol

How a phone (or any BLE central) sends an image to the card. The firmware
side lives in `firmware/main/upload.c` and `firmware/main/ble.c`;
`tools/ble_upload.py` is a working reference client.

## Summary

1. Convert the image to a 120,000-byte panel frame (see [Image format](#image-format)).
2. Connect, request a large MTU, subscribe to notifications on **Control**.
3. Write `START` (size + CRC-32) to **Control**, wait for `READY`.
4. Write the frame to **Data** in chunks, each prefixed with its offset.
5. Write `COMMIT` to **Control**, wait for `VERIFIED`, then `DISPLAYED` (~20-40 s later).

All multi-byte integers are **little-endian**.

## GATT layout

Advertised name `TCG Proxy Card`. The service UUID is in the scan response,
not the advertising packet, so scan by name or request active scanning.

| Item | UUID | Properties |
| --- | --- | --- |
| Service | `7f0eca18-f2cb-47d8-a350-d535917badd7` | primary |
| Control | `f22a619d-bd0e-4974-ba6f-30fda3bcf5c8` | write, notify |
| Data | `46639c60-7777-461a-b888-68f51edd3f5d` | write, write without response |

No pairing or encryption. The card accepts one connection at a time and stops
advertising while connected.

The firmware prefers an ATT MTU of 517. The largest write it accepts on either
characteristic is 512 bytes (the ATT attribute limit); longer writes are
rejected with ATT error `0x0D` (invalid attribute value length).

## Image format

Format `0x01`: the panel's native frame. Exactly **120,000 bytes**.

- 400 x 600 pixels, portrait (the panel's native orientation), row-major,
  top-left first.
- 4 bits per pixel, two pixels per byte, first pixel in the high nibble.
- Each nibble is a panel color code:

| Code | Color | RGB used for dithering |
| --- | --- | --- |
| `0x0` | black | 0, 0, 0 |
| `0x1` | white | 255, 255, 255 |
| `0x2` | yellow | 255, 255, 0 |
| `0x3` | red | 255, 0, 0 |
| `0x5` | blue | 0, 0, 255 |
| `0x6` | green | 0, 255, 0 |

`0x4` and `0x7`-`0xF` are undefined; don't send them.

The sender does all image processing. `tools/png_to_epd.py` is the reference:

1. Composite transparency onto white.
2. Scale to the panel height (600 for portrait, 400 for landscape), keeping
   aspect ratio; center on a white canvas of 400x600 (or 600x400),
   cropping the sides if wider.
3. Boost saturation (x1.8) and contrast (x1.2). Optional, but muted art
   washes out on six inks.
4. Floyd-Steinberg dither to the six RGB values above.
5. A landscape (600x400) result is rotated 90 degrees clockwise into 400x600.
6. Pack nibbles as described.

Why pre-dithered pixels rather than PNG/JPEG: the card's final representation
is this 120 KB frame anyway, a Scryfall-sized JPEG decoded to RGB is ~1.9 MB
(the firmware has no PSRAM enabled), and dithering on the phone lets the app
show an exact preview of what the panel will display.

## Control writes (central to card)

Write with response.

| Opcode | Message | Layout |
| --- | --- | --- |
| `0x01` | `START` | `u8 0x01`, `u8 format`, `u32 size`, `u32 crc32` (10 bytes) |
| `0x02` | `COMMIT` | `u8 0x02` (1 byte) |
| `0x03` | `ABORT` | `u8 0x03` (1 byte); drops the transfer, no reply |

- `format` must be `0x01`, `size` must be 120,000.
- `crc32` is the standard CRC-32 (IEEE 802.3, reflected, init and xor-out
  `0xFFFFFFFF`) of the whole frame — the same as zlib's `crc32`, Dart's
  `package:archive` `getCrc32`, or Java's `java.util.zip.CRC32`.
- `START` during a transfer discards it and starts over, so a client can
  simply retry.

## Data writes (central to card)

`u32 offset`, then up to `MTU - 7` frame bytes (at most 508).

- `offset` must equal the number of bytes received so far; chunks must be
  sent in order with no gaps.
- Write without response is recommended. It is still reliable on an intact
  link; the offset check and CRC catch anything else.
- Any chunk size from 1 to the maximum is fine and chunks may vary in size.

## Status notifications (card to central, on Control)

Always 6 bytes: `u8 event`, `u8 code`, `u32 value`.

| Event | Name | `code` | `value` |
| --- | --- | --- | --- |
| `0x01` | `READY` | 0 | frame size; start sending data |
| `0x02` | `PROGRESS` | 0 | bytes received; every 8,192 bytes and at the end |
| `0x03` | `VERIFIED` | 0 | frame size; CRC matched, saving and refreshing now |
| `0x04` | `DISPLAYED` | 0, or `0x0B` if shown but not saved | 0 |
| `0xFF` | `ERROR` | error code below | see below |

Notifications are only sent while the central is subscribed.

### Error codes

Every `ERROR` ends the transfer; start again with `START`.

| Code | Name | Meaning | `value` |
| --- | --- | --- | --- |
| `0x01` | `INVALID_MESSAGE` | wrong length or unknown opcode | 0 |
| `0x02` | `UNSUPPORTED_FORMAT` | `format` isn't `0x01` | format sent |
| `0x03` | `INVALID_SIZE` | `size` isn't 120,000 | expected size |
| `0x04` | `BUSY` | the panel is still refreshing (at boot, or from the previous upload); retry after `DISPLAYED` or in a few seconds | 0 |
| `0x05` | `NO_TRANSFER` | data or `COMMIT` without an active `START` | 0 |
| `0x06` | `BAD_OFFSET` | chunk offset isn't the next expected byte | bytes received |
| `0x07` | `OVERFLOW` | chunk runs past the declared size | declared size |
| `0x08` | `INCOMPLETE` | `COMMIT` before all bytes arrived | bytes received |
| `0x09` | `CRC_MISMATCH` | frame doesn't match `crc32` | CRC the card computed |
| `0x0A` | `NO_MEMORY` | couldn't allocate the frame buffer | 0 |
| `0x0B` | `STORAGE_FAILED` | only as `DISPLAYED`'s code: shown, but won't survive a reboot | 0 |
| `0x0C` | `DISPLAY_FAILED` | panel refresh failed (e.g. busy timeout / wiring) | 0 |
| `0x0D` | `COOLDOWN` | less than 180 s since the last refresh (including the one at boot); checked at `START` and `COMMIT` | seconds until a refresh is allowed |

## Behavior

- **Disconnect mid-transfer** drops the partial frame. After `VERIFIED` the
  card finishes saving and refreshing even if the central disconnects; it
  just can't send `DISPLAYED`.
- **Persistence.** A verified frame is written to the `image` flash partition
  before the refresh and shown again on every boot. With no valid saved frame
  the card shows the built-in `image.bin`.
- **Timing.** At 1M PHY and MTU 517 a transfer takes roughly 5-15 s depending on
  the phone's connection interval. Saving takes ~1-2 s and a refresh ~20-40 s.
  Allow at least 90 s between `VERIFIED` and `DISPLAYED` before giving up.
- **Panel care.** The panel shouldn't refresh more often than every 180 s.
  The card enforces this from the end of the previous refresh, including the
  one at boot, and answers `COOLDOWN` with the seconds left. Check before
  uploading so the user isn't left waiting after a full transfer.

## Notes for the app

The app is Flutter (iOS, Android, web).

- **BLE plugin.** `flutter_blue_plus` covers iOS and Android. Web Bluetooth
  (web builds) negotiates the MTU but doesn't expose it; there, sending
  512-byte chunks with response relies on the browser falling back to ATT
  long writes, which the card supports but which is untested here. Treat web
  as best effort.
- **MTU.** Android: call `requestMtu(517)` after connecting (flutter_blue_plus
  does this by default) and use the returned value. iOS negotiates on its own
  (typically 185 or 517 on recent devices) — use the plugin's reported MTU or
  `maximumWriteValueLength(for: .withoutResponse)`. Chunk payload is
  `min(mtu - 3, 512) - 4`.
- **Write without response** needs flow control: on iOS/Android the plugin
  waits for the OS to be ready. If the app sees write failures, fall back to
  write with response.
- **Image pipeline.** Card art is already fetched from Scryfall
  (`largeImageUrl`, 672x936 JPEG). In Dart, `package:image` can decode,
  resize, and composite; implement the Floyd-Steinberg step and nibble packing
  by hand (a few dozen lines) so the palette and error diffusion match
  `png_to_epd.py`. Run it in an isolate (`compute`) — it's ~240k pixels. The
  dithered RGB result doubles as the preview.
- **Flow.** Show progress from `PROGRESS` / chunk count, then a "refreshing"
  state from `VERIFIED` until `DISPLAYED`. On `BUSY`, wait and retry; on `COOLDOWN`, show a countdown from `value`.
