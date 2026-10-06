"""Builds iss/globe_data.h: a per-pixel lookup for an orthographic globe (which lat/lon each screen
pixel shows, plus a dither threshold), an equirectangular land texture, a cosine table and a 5x7 font.
The Uno spins the globe by adding an offset to the longitudes. Also writes build/globe_preview.png.

Land data: Natural Earth 110m land (public domain), downloaded to build/ on first run.
"""
import json
import math
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent.parent
LAND_URL = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson"

TEX_W, TEX_H = 256, 64  # longitude 0..360 east, latitude 90N..90S
CX, CY, R = 40.0, 32.0, 27.0  # globe center and radius on screen (pixel centers at +0.5)
TILT = math.radians(20)  # viewer sits 20 degrees above the equator
ATMOSPHERE = 1.5  # pixels with R < dist <= R + this are the always-on outline
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])

# Brightness model, mirrored in iss.ino: 0..255, a pixel is lit when brightness >= its threshold.
LAND_NIGHT, LAND_DAY, OCEAN_DAY = 30, 100, 36

FONT_CHARS = " -./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ"
FONT = {  # classic 5x7, one byte per column, bit 0 = top row
    " ": [0, 0, 0, 0, 0], "-": [8, 8, 8, 8, 8], ".": [0, 0x60, 0x60, 0, 0], "/": [0x20, 0x10, 8, 4, 2],
    "0": [0x3E, 0x51, 0x49, 0x45, 0x3E], "1": [0, 0x42, 0x7F, 0x40, 0], "2": [0x42, 0x61, 0x51, 0x49, 0x46],
    "3": [0x21, 0x41, 0x45, 0x4B, 0x31], "4": [0x18, 0x14, 0x12, 0x7F, 0x10], "5": [0x27, 0x45, 0x45, 0x45, 0x39],
    "6": [0x3C, 0x4A, 0x49, 0x49, 0x30], "7": [1, 0x71, 9, 5, 3], "8": [0x36, 0x49, 0x49, 0x49, 0x36],
    "9": [6, 0x49, 0x49, 0x29, 0x1E], ":": [0, 0x36, 0x36, 0, 0],
    "A": [0x7E, 0x11, 0x11, 0x11, 0x7E], "B": [0x7F, 0x49, 0x49, 0x49, 0x36], "C": [0x3E, 0x41, 0x41, 0x41, 0x22],
    "D": [0x7F, 0x41, 0x41, 0x22, 0x1C], "E": [0x7F, 0x49, 0x49, 0x49, 0x41], "F": [0x7F, 9, 9, 9, 1],
    "G": [0x3E, 0x41, 0x49, 0x49, 0x7A], "H": [0x7F, 8, 8, 8, 0x7F], "I": [0, 0x41, 0x7F, 0x41, 0],
    "J": [0x20, 0x40, 0x41, 0x3F, 1], "K": [0x7F, 8, 0x14, 0x22, 0x41], "L": [0x7F, 0x40, 0x40, 0x40, 0x40],
    "M": [0x7F, 2, 0x0C, 2, 0x7F], "N": [0x7F, 4, 8, 0x10, 0x7F], "O": [0x3E, 0x41, 0x41, 0x41, 0x3E],
    "P": [0x7F, 9, 9, 9, 6], "Q": [0x3E, 0x41, 0x51, 0x21, 0x5E], "R": [0x7F, 9, 0x19, 0x29, 0x46],
    "S": [0x46, 0x49, 0x49, 0x49, 0x31], "T": [1, 1, 0x7F, 1, 1], "U": [0x3F, 0x40, 0x40, 0x40, 0x3F],
    "V": [0x1F, 0x20, 0x40, 0x20, 0x1F], "W": [0x3F, 0x40, 0x38, 0x40, 0x3F], "X": [0x63, 0x14, 8, 0x14, 0x63],
    "Y": [7, 8, 0x70, 8, 7], "Z": [0x61, 0x51, 0x49, 0x45, 0x43],
}


def land_texture():
    """TEX_H x TEX_W bool array, column 0 = longitude 0, increasing east."""
    path = HERE / "build/ne_110m_land.geojson"
    if not path.exists():
        path.parent.mkdir(exist_ok=True)
        urllib.request.urlretrieve(LAND_URL, path)
    scale = 8
    img = Image.new("L", (TEX_W * scale, TEX_H * scale), 0)
    d = ImageDraw.Draw(img)
    for feature in json.loads(path.read_text())["features"]:
        geom = feature["geometry"]
        polys = geom["coordinates"] if geom["type"] == "MultiPolygon" else [geom["coordinates"]]
        for poly in polys:
            ring = [((lon + 180) / 360 * img.width, (90 - lat) / 180 * img.height) for lon, lat in poly[0]]
            d.polygon(ring, fill=255)
    a = np.asarray(img, dtype=float).reshape(TEX_H, scale, TEX_W, scale).mean(axis=(1, 3)) > 110
    return np.roll(a, -TEX_W // 2, axis=1)  # drawn from -180; shift so column 0 is longitude 0


def pixel_table():
    """Rows of (x0, entries) where each entry is (lon_rel index, lat row, threshold)."""
    rows = []
    for y in range(64):
        entries, x0 = [], None
        for x in range(128):
            px, py = (x + 0.5 - CX) / R, (CY - (y + 0.5)) / R
            dist = math.hypot(px, py) * R
            if dist > R + ATMOSPHERE:
                continue
            if x0 is None:
                x0 = x
            if dist > R:
                entries.append((0, 0, 0))  # atmosphere outline: always lit
                continue
            pz = math.sqrt(max(0.0, 1 - px * px - py * py))
            # View (right=px, up=py, toward=pz) -> earth frame with the view-center meridian on +x.
            ex = pz * math.cos(TILT) - py * math.sin(TILT)
            ez = pz * math.sin(TILT) + py * math.cos(TILT)
            lat = math.degrees(math.asin(max(-1, min(1, ez))))
            lon = math.degrees(math.atan2(px, ex)) % 360
            row = min(TEX_H - 1, int((90 - lat) / 180 * TEX_H))
            lon_i = int(lon / 360 * TEX_W) % TEX_W
            limb = 0.45 + 0.55 * pz  # darker towards the edge: reads as a sphere
            thr = int(min(255, max(1, (BAYER[y % 4, x % 4] * 16 + 8) / limb)))
            entries.append((lon_i, row, thr))
        rows.append((x0 or 0, entries))
    return rows


def brightness(land, s):
    """s = sun elevation term in -127..127. Mirrors iss.ino."""
    if land:
        return LAND_DAY + (s * (255 - LAND_DAY) >> 7) if s > 0 else LAND_NIGHT
    return (s * OCEAN_DAY >> 7) if s > 0 else 0


def render_preview(tex, rows, spin, sun_lat, sun_lon):
    img = np.zeros((64, 128), bool)
    for y, (x0, entries) in enumerate(rows):
        for i, (lon_rel, row, thr) in enumerate(entries):
            if thr == 0:
                img[y, x0 + i] = True
                continue
            lon = (lon_rel + spin) % TEX_W
            lat = math.radians(90 - (row + 0.5) * 180 / TEX_H)
            s = int(127 * (math.sin(lat) * math.sin(sun_lat) + math.cos(lat) * math.cos(sun_lat) * math.cos(2 * math.pi * lon / TEX_W - sun_lon)))
            img[y, x0 + i] = brightness(tex[row, lon], s) >= thr
    return img


def c_bytes(data, per_line=24):
    data = list(data)
    return ",\n".join("  " + ",".join(str(v) for v in data[i : i + per_line]) for i in range(0, len(data), per_line))


def main():
    tex = land_texture()
    rows = pixel_table()

    tex_bytes = []
    for r in range(TEX_H):
        for c in range(0, TEX_W, 8):
            tex_bytes.append(sum(1 << k for k in range(8) if tex[r, c + k]))
    pix = [v for _, entries in rows for e in entries for v in e]
    cos_tab = [round(127 * math.cos(2 * math.pi * i / 256)) for i in range(256)]
    font = [b for ch in FONT_CHARS for b in FONT[ch]]

    out = [
        "// Generated by iss/gen_globe.py. Do not edit.",
        f"const uint8_t GLOBE_CX = {int(CX)}, GLOBE_CY = {int(CY)}, GLOBE_R = {int(R)};",
        f"const float GLOBE_TILT = {TILT:.6f};",
        f"const uint8_t LAND_NIGHT = {LAND_NIGHT}, LAND_DAY = {LAND_DAY}, OCEAN_DAY = {OCEAN_DAY};",
        f"const uint8_t ROW_X0[64] PROGMEM = {{{','.join(str(x0) for x0, _ in rows)}}};",
        f"const uint8_t ROW_N[64] PROGMEM = {{{','.join(str(len(e)) for _, e in rows)}}};",
        f"// Per pixel, row-major: longitude index relative to the view center, texture row, dither threshold (0 = always lit).",
        f"const uint8_t PIXELS[{len(pix)}] PROGMEM = {{\n{c_bytes(pix)}\n}};",
        f"// Land bitmap: {TEX_H} rows x {TEX_W // 8} bytes, bit k of byte c = longitude index c*8+k.",
        f"const uint8_t LAND[{len(tex_bytes)}] PROGMEM = {{\n{c_bytes(tex_bytes)}\n}};",
        f"const int8_t COS_TAB[256] PROGMEM = {{\n{c_bytes(cos_tab)}\n}};",
        f'const char FONT_CHARS[] PROGMEM = "{FONT_CHARS}";',
        f"const uint8_t FONT[{len(font)}] PROGMEM = {{\n{c_bytes(font)}\n}};",
    ]
    (HERE / "iss/globe_data.h").write_text("\n".join(out) + "\n")

    frames = [render_preview(tex, rows, spin, math.radians(-5), math.radians(225)) for spin in range(0, 256, 32)]
    sheet = Image.new("L", (4 * 132, 2 * 68), 40)
    for i, f in enumerate(frames):
        sheet.paste(Image.fromarray((f * 255).astype(np.uint8)), ((i % 4) * 132 + 2, (i // 4) * 68 + 2))
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(HERE / "build/globe_preview.png")
    print(f"pixels: {len(pix) // 3}, PIXELS {len(pix)} B, LAND {len(tex_bytes)} B")


main()
