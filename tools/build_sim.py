"""Merge src/ into the MicroPython UF2 as a LittleFS image, so the simulator boots main.py like the real board."""
import struct
import sys
from pathlib import Path

from littlefs import LittleFS

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "firmware/RPI_PICO_W.uf2"
OUT = ROOT / "build/sim.uf2"

# Pico W MicroPython filesystem: last 212 blocks of 4 KB in the 2 MB flash (read from rp2.Flash() in the sim).
BLOCK_SIZE = 4096
BLOCK_COUNT = 212
FS_ADDR = 0x10000000 + 2 * 1024 * 1024 - BLOCK_SIZE * BLOCK_COUNT

UF2_HEADER = struct.Struct("<8I")
UF2_MAGIC = (0x0A324655, 0x9E5D5157)
UF2_END = 0x0AB16F30
UF2_FAMILY_FLAG = 0x2000
RP2040_FAMILY = 0xE48BFF56


def fs_image():
    fs = LittleFS(block_size=BLOCK_SIZE, block_count=BLOCK_COUNT, read_size=32, prog_size=32, lookahead_size=32)
    for f in sorted((ROOT / "src").glob("*.py")):
        with fs.open(f.name, "wb") as out:
            out.write(f.read_bytes())
    return bytes(fs.context.buffer)


def main():
    fw = FIRMWARE.read_bytes()
    chunks = [(UF2_HEADER.unpack_from(fw, i)[3], fw[i + 32 : i + 32 + 256]) for i in range(0, len(fw), 512)]
    img = fs_image()
    chunks += [(FS_ADDR + off, img[off : off + 256]) for off in range(0, len(img), 256)]

    out = bytearray()
    for n, (addr, data) in enumerate(chunks):
        out += UF2_HEADER.pack(*UF2_MAGIC, UF2_FAMILY_FLAG, addr, 256, n, len(chunks), RP2040_FAMILY)
        out += data.ljust(476, b"\0") + struct.pack("<I", UF2_END)
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_bytes(out)
    print(f"{OUT.relative_to(ROOT)}: {len(chunks)} blocks", file=sys.stderr)


main()
