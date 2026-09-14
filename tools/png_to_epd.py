#!/usr/bin/env python3
"""Convert a PNG into a frame buffer for the Waveshare 3.6" e-Paper (E).

The panel shows six colors and is natively 400x600 (portrait) with 4 bits per
pixel. The image is dithered to the panel palette and packed two pixels per
byte. Portrait 400x600 images are sent as-is; landscape 600x400 images are
rotated to match the Waveshare demo (Paint rotate 90). The firmware embeds the
output file at build time.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageEnhance

NATIVE_SIZE = (400, 600)
LANDSCAPE_SIZE = (600, 400)

# (RGB, panel color code) — codes from EPD_3in6e.h
PALETTE = [
    ((0, 0, 0), 0x0),
    ((255, 255, 255), 0x1),
    ((255, 255, 0), 0x2),
    ((255, 0, 0), 0x3),
    ((0, 0, 255), 0x5),
    ((0, 255, 0), 0x6),
]

DEFAULT_OUTPUT = Path(__file__).resolve().parent.parent / "firmware" / "main" / "image.bin"


def load_rgb(path: Path) -> Image.Image:
    image = Image.open(path)
    if image.size not in (NATIVE_SIZE, LANDSCAPE_SIZE):
        sys.exit(f"{path}: expected 400x600 or 600x400, got {image.width}x{image.height}")
    rgba = image.convert("RGBA")
    background = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    return Image.alpha_composite(background, rgba).convert("RGB")


def enhance(image: Image.Image, saturation: float, contrast: float) -> Image.Image:
    # The panel's six inks wash out muted art, so exaggerate before dithering.
    return ImageEnhance.Contrast(ImageEnhance.Color(image).enhance(saturation)).enhance(contrast)


def quantize(image: Image.Image, dither: bool) -> Image.Image:
    palette_image = Image.new("P", (1, 1))
    flat = [channel for rgb, _ in PALETTE for channel in rgb]
    # Pad by repeating black so Pillow can't pick unused palette slots.
    palette_image.putpalette(flat + flat[:3] * (256 - len(PALETTE)))
    method = Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE
    return image.quantize(palette=palette_image, dither=method)


def pack(indexed: Image.Image) -> bytes:
    native = indexed if indexed.size == NATIVE_SIZE else indexed.transpose(Image.Transpose.ROTATE_270)
    codes = [PALETTE[i][1] if i < len(PALETTE) else PALETTE[0][1] for i in native.tobytes()]
    return bytes((codes[i] << 4) | codes[i + 1] for i in range(0, len(codes), 2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("png", type=Path, help="400x600 or 600x400 PNG to convert")
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--no-dither", action="store_true", help="map to nearest color without dithering")
    parser.add_argument("--saturation", type=float, default=1.8, help="1.0 leaves colors unchanged (default 1.8)")
    parser.add_argument("--contrast", type=float, default=1.2, help="1.0 leaves contrast unchanged (default 1.2)")
    parser.add_argument("--preview", type=Path, help="also save a PNG of the dithered result")
    args = parser.parse_args()

    image = enhance(load_rgb(args.png), args.saturation, args.contrast)
    indexed = quantize(image, dither=not args.no_dither)
    if args.preview:
        indexed.convert("RGB").save(args.preview)

    args.output.write_bytes(pack(indexed))
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
