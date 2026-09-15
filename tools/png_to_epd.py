#!/usr/bin/env python3
"""Convert a PNG into a frame buffer for the Waveshare 3.6" e-Paper (E).

The panel shows six colors and is natively 400x600 (portrait) with 4 bits per
pixel. Images of any size are scaled to the panel height and centered, cutting
off the left and right edges (or padding with white if too narrow), then
dithered to the panel palette and packed two pixels per byte. Portrait images
fill 400x600; landscape images fill 600x400 and are rotated to match the
Waveshare demo (Paint rotate 90). Send the output file with ble_upload.py.
"""

import argparse
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

def load_rgb(path: Path) -> Image.Image:
    rgba = Image.open(path).convert("RGBA")
    background = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    return Image.alpha_composite(background, rgba).convert("RGB")


def fit_height(image: Image.Image) -> Image.Image:
    target_w, target_h = NATIVE_SIZE if image.height >= image.width else LANDSCAPE_SIZE
    scaled_w = round(image.width * target_h / image.height)
    scaled = image.resize((scaled_w, target_h), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (target_w, target_h), (255, 255, 255))
    canvas.paste(scaled, ((target_w - scaled_w) // 2, 0))
    return canvas


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
    parser.add_argument("png", type=Path, help="image to convert")
    parser.add_argument("-o", "--output", type=Path, help="frame buffer to write (default: the image path with .bin)")
    parser.add_argument("--no-dither", action="store_true", help="map to nearest color without dithering")
    parser.add_argument("--saturation", type=float, default=1.8, help="1.0 leaves colors unchanged (default 1.8)")
    parser.add_argument("--contrast", type=float, default=1.2, help="1.0 leaves contrast unchanged (default 1.2)")
    parser.add_argument("--preview", type=Path, help="also save a PNG of the dithered result")
    args = parser.parse_args()

    image = enhance(fit_height(load_rgb(args.png)), args.saturation, args.contrast)
    indexed = quantize(image, dither=not args.no_dither)
    if args.preview:
        indexed.convert("RGB").save(args.preview)

    output = args.output or args.png.with_suffix(".bin")
    output.write_bytes(pack(indexed))
    print(f"wrote {output}")


if __name__ == "__main__":
    main()
