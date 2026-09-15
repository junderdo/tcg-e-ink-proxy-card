#!/usr/bin/env python3
"""Upload an image to the TCG Proxy Card over BLE and wait for it to be displayed.

Accepts any image Pillow can read (converted like png_to_epd.py) or a ready
400x600 frame buffer (.bin). Implements docs/ble-image-upload.md.
"""

import argparse
import asyncio
import struct
import sys
import zlib
from pathlib import Path

from bleak import BleakClient, BleakScanner

import png_to_epd

DEVICE_NAME = "TCG Proxy Card"
SERVICE_UUID = "7f0eca18-f2cb-47d8-a350-d535917badd7"
CONTROL_UUID = "f22a619d-bd0e-4974-ba6f-30fda3bcf5c8"
DATA_UUID = "46639c60-7777-461a-b888-68f51edd3f5d"

FRAME_SIZE = 120_000
FORMAT_EPD_3IN6E_4BPP = 0x01
OP_START, OP_COMMIT = 0x01, 0x02
EVENT_READY, EVENT_PROGRESS, EVENT_VERIFIED, EVENT_DISPLAYED, EVENT_ERROR = 0x01, 0x02, 0x03, 0x04, 0xFF
DATA_HEADER_LEN = 4
MAX_ATT_VALUE = 512

ERRORS = {
    0x01: "INVALID_MESSAGE",
    0x02: "UNSUPPORTED_FORMAT",
    0x03: "INVALID_SIZE",
    0x04: "BUSY",
    0x05: "NO_TRANSFER",
    0x06: "BAD_OFFSET",
    0x07: "OVERFLOW",
    0x08: "INCOMPLETE",
    0x09: "CRC_MISMATCH",
    0x0A: "NO_MEMORY",
    0x0B: "STORAGE_FAILED",
    0x0C: "DISPLAY_FAILED",
}


class UploadError(Exception):
    pass


def load_frame(path: Path, dither: bool) -> bytes:
    if path.suffix.lower() == ".bin":
        return path.read_bytes()
    image = png_to_epd.enhance(png_to_epd.fit_height(png_to_epd.load_rgb(path)), 1.8, 1.2)
    return png_to_epd.pack(png_to_epd.quantize(image, dither))


async def find_device(address: str | None):
    if address:
        return address
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: d.name == DEVICE_NAME or SERVICE_UUID in adv.service_uuids, timeout=15
    )
    if device is None:
        raise UploadError(f'no "{DEVICE_NAME}" found')
    return device


async def upload(address: str | None, frame: bytes, with_response: bool) -> None:
    events: asyncio.Queue[tuple[int, int, int]] = asyncio.Queue()

    def on_status(_, data: bytearray) -> None:
        events.put_nowait(struct.unpack("<BBI", data))

    def handle(event: int, code: int, value: int) -> None:
        if event == EVENT_ERROR:
            raise UploadError(f"{ERRORS.get(code, hex(code))} (value {value})")
        if event == EVENT_PROGRESS:
            print(f"\r{value}/{len(frame)} bytes", end="", flush=True)

    async def expect(wanted: int, timeout: float) -> int:
        while True:
            event, code, value = await asyncio.wait_for(events.get(), timeout)
            handle(event, code, value)
            if event == wanted:
                return code

    async with BleakClient(await find_device(address)) as client:
        # BlueZ reports the default MTU of 23 until asked for the negotiated one.
        if hasattr(client._backend, "_acquire_mtu"):
            await client._backend._acquire_mtu()
        await client.start_notify(CONTROL_UUID, on_status)
        chunk = min(client.mtu_size - 3, MAX_ATT_VALUE) - DATA_HEADER_LEN
        print(f"connected, MTU {client.mtu_size}, {chunk} bytes per chunk")

        start = struct.pack("<BBII", OP_START, FORMAT_EPD_3IN6E_4BPP, len(frame), zlib.crc32(frame))
        await client.write_gatt_char(CONTROL_UUID, start, response=True)
        await expect(EVENT_READY, 5)

        for offset in range(0, len(frame), chunk):
            payload = struct.pack("<I", offset) + frame[offset:offset + chunk]
            await client.write_gatt_char(DATA_UUID, payload, response=with_response)
            while not events.empty():
                handle(*events.get_nowait())

        await client.write_gatt_char(CONTROL_UUID, bytes([OP_COMMIT]), response=True)
        await expect(EVENT_VERIFIED, 10)
        print("\nverified; saving and refreshing the panel")
        code = await expect(EVENT_DISPLAYED, 90)
        if code:
            print(f"displayed, but {ERRORS.get(code, hex(code))}")
        else:
            print("displayed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="image to convert, or a .bin frame buffer")
    parser.add_argument("--address", help="device address; scans by name if omitted")
    parser.add_argument("--no-dither", action="store_true", help="map to nearest color without dithering")
    parser.add_argument("--with-response", action="store_true", help="acknowledge every data chunk (slower)")
    args = parser.parse_args()

    frame = load_frame(args.image, dither=not args.no_dither)
    if len(frame) != FRAME_SIZE:
        sys.exit(f"frame is {len(frame)} bytes, expected {FRAME_SIZE}")
    try:
        asyncio.run(upload(args.address, frame, args.with_response))
    except (UploadError, TimeoutError) as e:
        sys.exit(f"\nupload failed: {e}")


if __name__ == "__main__":
    main()
