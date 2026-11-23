#!/usr/bin/env python3
"""
Optional icon generator for Ember Watch. The icons baked into
ember_watch/ember_icons.h were AI-generated with this script (Draw Things, a
local Stable Diffusion app, via its Automatic1111-compatible API) and then
autocropped, thresholded to 1-bit and packed. You do NOT need to run this to
build or flash the firmware; ember_icons.h is already committed.

Each icon is autocropped to its inked content (the style prompt asks for
white space around the subject, which would leave it looking small),
packed MSB-first (the format drawBitmap() expects) and written to
ember_watch/ember_icons.h. A decoded contact sheet is saved to
tools/icon_preview.png so you can check the result before reflashing.

Two sizes: STATUS_ICON_SIZE for the six severity icons (kept smaller so the
text below has room) and SPLASH_ICON_SIZE for the boot screen. Both are
generated at exactly the size drawn on-device (flame_render.cpp reads both
constants from the generated header).

Requirements: Python 3 and Pillow (pip install pillow), plus any server that
speaks the Automatic1111 /sdapi/v1/txt2img API. Set EMBER_ICON_API to its
base URL (default http://127.0.0.1:7860, Draw Things' default). Run from
anywhere:
    python3 tools/gen_icons.py
Note: output varies between runs and models, so regenerated icons will not
match the committed ones exactly.
"""
import base64
import io
import json
import os
import urllib.request
from pathlib import Path

from PIL import Image

DRAW_THINGS_URL = os.environ.get("EMBER_ICON_API", "http://127.0.0.1:7860")
GEN_SIZE = 512
STEPS = 6

STATUS_ICON_SIZE = 72   # 72/8=9, no row padding needed
SPLASH_ICON_SIZE = 144  # 144/8=18, no row padding needed

STYLE = (
    "minimalist black ink line icon, single simple pictogram, app icon style, "
    "bold clean outline, no shading, no color, no text, pure black on white, "
    "centered, plenty of white space around it"
)
NEGATIVE = (
    "color, gradient, shading, photorealistic, text, watermark, blurry, "
    "grayscale, crosshatching, clutter, busy detail, signature, multiple objects"
)

# (C identifier, prompt subject, target size) - six severity states plus
# the splash icon.
ICONS = [
    ("ICON_ALL_CLEAR", "a single checkmark inside a plain circle, calm and simple", STATUS_ICON_SIZE),
    ("ICON_LOW", "a single small campfire flame, calm and steady", STATUS_ICON_SIZE),
    ("ICON_ELEVATED", "a medium flame with a couple of small sparks flying off", STATUS_ICON_SIZE),
    ("ICON_HIGH", "a large intense flame with several sparks and embers flying", STATUS_ICON_SIZE),
    ("ICON_EXTREME", "a huge raging wildfire inferno, multiple tall flames and smoke", STATUS_ICON_SIZE),
    ("ICON_UNKNOWN", "a flame outline with a large question mark inside it", STATUS_ICON_SIZE),
    ("ICON_SPLASH", "a fire lookout watchtower on a hillside, forestry service style", SPLASH_ICON_SIZE),
]


def generate(prompt):
    payload = {
        "prompt": f"{STYLE}, {prompt}",
        "negative_prompt": NEGATIVE,
        "width": GEN_SIZE,
        "height": GEN_SIZE,
        "steps": STEPS,
    }
    req = urllib.request.Request(
        f"{DRAW_THINGS_URL}/sdapi/v1/txt2img",
        data=json.dumps(payload).encode("utf-8"),
        headers={"content-type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        result = json.loads(resp.read().decode("utf-8"))
    png_bytes = base64.b64decode(result["images"][0])
    return Image.open(io.BytesIO(png_bytes)).convert("L")


def autocrop_and_resize(img, target_size, margin_frac=0.08):
    """Crop tight to the inked bounding box (plus a small margin), padding
    with WHITE for any crop area outside the original image, then resize up
    to target_size - makes the subject fill its cell regardless of how much
    incidental whitespace the generation left around it."""
    ink_mask = img.point(lambda p: 255 if p < 140 else 0)  # 255 where ink
    bbox = ink_mask.getbbox()
    if not bbox:
        return img.resize((target_size, target_size), Image.LANCZOS)
    x0, y0, x1, y1 = bbox
    side = max(x1 - x0, y1 - y0)
    margin = max(6, int(side * margin_frac))
    half = side // 2 + margin
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    left, top = cx - half, cy - half
    box_size = half * 2
    canvas = Image.new("L", (box_size, box_size), 255)
    src_left, src_top = max(left, 0), max(top, 0)
    src_right, src_bottom = min(left + box_size, img.width), min(top + box_size, img.height)
    if src_right > src_left and src_bottom > src_top:
        region = img.crop((src_left, src_top, src_right, src_bottom))
        canvas.paste(region, (src_left - left, src_top - top))
    return canvas.resize((target_size, target_size), Image.LANCZOS)


def to_1bpp_bytes(img, size):
    img = autocrop_and_resize(img, size)
    bw = img.point(lambda p: 255 if p >= 140 else 0)
    px = bw.load()
    row_bytes = size // 8
    out = bytearray(row_bytes * size)
    for y in range(size):
        for bx in range(row_bytes):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                # Device convention: bit=1 -> drawn/foreground (black ink),
                # matching Adafruit_GFX's drawBitmap() where a set bit paints.
                if px[x, y] < 128:
                    byte |= 0x80 >> bit
            out[y * row_bytes + bx] = byte
    return bytes(out)


def emit_header(icons, path):
    lines = [
        "#ifndef EMBER_ICONS_H",
        "#define EMBER_ICONS_H",
        "",
        "#include <pgmspace.h>",
        "",
        f"#define STATUS_ICON_SIZE {STATUS_ICON_SIZE}",
        f"#define SPLASH_ICON_SIZE {SPLASH_ICON_SIZE}",
        "",
    ]
    for name, data, size in icons:
        size_bytes = (size // 8) * size
        lines.append(f"static const uint8_t {name}[{size_bytes}] PROGMEM = {{  // {size}x{size}")
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
        lines.append("};")
        lines.append("")
    lines.append("#endif")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def save_contact_sheet(icons, path):
    """Decode the packed bitmaps back into a visual contact sheet PNG so
    quality can be checked before committing to flash. Each icon shown
    at its own native size, padded to the tallest/widest cell."""
    cell = max(size for _, _, size in icons)
    sheet = Image.new("L", (cell * len(icons), cell), 200)
    for i, (name, data, size) in enumerate(icons):
        row_bytes = size // 8
        img = Image.new("L", (size, size), 255)
        px = img.load()
        for y in range(size):
            for x in range(size):
                byte = data[y * row_bytes + (x // 8)]
                bit = (byte >> (7 - (x % 8))) & 1
                px[x, y] = 0 if bit else 255
        sheet.paste(img, (i * cell + (cell - size) // 2, (cell - size) // 2))
    sheet.save(path)


def main():
    icons = []
    for name, prompt, size in ICONS:
        print(f"generating {name} ({prompt}) at {size}x{size}...")
        img = generate(prompt)
        data = to_1bpp_bytes(img, size)
        icons.append((name, data, size))
    here = Path(__file__).resolve().parent
    out_path = str(here.parent / "ember_watch" / "ember_icons.h")
    emit_header(icons, out_path)
    print(f"wrote {out_path}")
    save_contact_sheet(icons, str(here / "icon_preview.png"))
    print("wrote icon_preview.png (contact sheet) for visual review")


if __name__ == "__main__":
    main()
