"""Generate decoder inputs using Python's standard library."""
import pathlib
import struct
import sys
import zlib

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "nSee/tests/fixtures")
root.mkdir(parents=True, exist_ok=True)
cases = []
colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]

def rgb565(rgb):
    r, g, b = rgb
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3

def save(name, data, width=0, height=0, pixels=None):
    (root / name).write_bytes(data)
    cases.append(("ok" if pixels is not None else "bad", name))
    if pixels is not None:
        (root / (name + ".rgb565")).write_bytes(
            struct.pack("<II", width, height) + struct.pack("<" + "H" * len(pixels), *pixels))

def bmp(bits, width=3, height=2, top=False, dib=False, compression=0, masks=b"", raw=None,
        indices=None, palette=None, header_size=40):
    palette = colors if palette is None else palette
    indices = ([0, 1, 0, 1, 0, 1] if bits == 1 else [0, 1, 2, 3, 0, 1]) if indices is None else indices
    palette = palette[:2] if bits == 1 else palette
    if header_size == 12 and bits <= 8:
        palette = palette + [(0, 0, 0)] * ((1 << bits) - len(palette))
    order = range(height) if top else range(height - 1, -1, -1)
    if raw is None:
        rows = []
        for y in order:
            values = indices[y * width:(y + 1) * width]
            row = bytearray()
            if bits < 8:
                for x in range(0, width, 8 // bits):
                    value = 0
                    for j in range(8 // bits):
                        value |= (values[x + j] if x + j < width else 0) << (8 - bits * (j + 1))
                    row.append(value)
            else:
                for index in values:
                    if bits != 8:
                        r, g, b = palette[index]
                    if bits == 8:
                        row.append(index)
                    elif bits == 16:
                        value = rgb565((r, g, b)) if masks else (r >> 3) << 10 | (g >> 3) << 5 | b >> 3
                        row += struct.pack("<H", value)
                    else:
                        row += bytes((b, g, r)) + (b"\xff" if bits == 32 else b"")
            row += bytes((-len(row)) % 4)
            rows.append(row)
        raw = b"".join(rows)
    pal = b"".join(bytes((b, g, r)) + (b"" if header_size == 12 else b"\0") for r, g, b in palette) if bits <= 8 else b""
    if header_size == 12:
        header = struct.pack("<IHHHH", 12, width, height, 1, bits)
    else:
        header = struct.pack("<IiiHHIIIIII", header_size, width, -height if top else height,
                             1, bits, compression, len(raw), 0, 0, len(palette) if bits <= 8 else 0, 0)
        if header_size > 40:
            header += masks + bytes(header_size - 40 - len(masks))
            masks = b""
    offset = 14 + len(header) + len(masks) + len(pal)
    return (b"" if dib else struct.pack("<2sIHHI", b"BM", offset + len(raw), 0, 0, offset)) + header + masks + pal + raw

for bits in (1, 4, 8, 16, 24, 32):
    indices = [0, 1, 0, 1, 0, 1] if bits == 1 else [0, 1, 2, 3, 0, 1]
    pixels = [rgb565(colors[i]) for i in indices]
    for top in (False, True):
        save(f"bmp{bits}-{'top' if top else 'bottom'}.bmp", bmp(bits, top=top), 3, 2, pixels)
save("core.bmp", bmp(4, header_size=12), 3, 2, [rgb565(colors[i]) for i in [0, 1, 2, 3, 0, 1]])
save("raw.dib", bmp(24, top=True, dib=True), 3, 2, [rgb565(colors[i]) for i in [0, 1, 2, 3, 0, 1]])
save("onebit-padding.bmp", bmp(1, width=9, indices=[i % 2 for i in range(18)]), 9, 2,
     [rgb565(colors[i % 2]) for i in range(18)])
mask565 = struct.pack("<III", 0xf800, 0x07e0, 0x001f)
for size in (40, 52, 56, 108, 124):
    save(f"bitfields{size}.bmp", bmp(16, compression=3, masks=mask565, header_size=size), 3, 2,
         [rgb565(colors[i]) for i in [0, 1, 2, 3, 0, 1]])
save("bitfields32.bmp", bmp(32, compression=3, masks=struct.pack("<III", 0xff0000, 0xff00, 0xff)),
     3, 2, [rgb565(colors[i]) for i in [0, 1, 2, 3, 0, 1]])
# Absolute-mode padding, row transitions, and both encoded nibbles.
save("rle8.bmp", bmp(8, compression=1, raw=bytes([0, 3, 3, 0, 1, 0, 0, 0, 0, 3, 0, 1, 2, 0, 0, 1])),
     3, 2, [rgb565(colors[i]) for i in [0, 1, 2, 3, 0, 1]])
save("rle4.bmp", bmp(4, compression=2, raw=bytes([0, 3, 0x30, 0x10, 0, 0, 0, 3, 0x01, 0x20, 0, 1])),
     3, 2, [rgb565(colors[i]) for i in [0, 1, 2, 3, 0, 1]])
save("rle4-encoded.bmp", bmp(4, compression=2, raw=bytes([3, 0x12, 0, 0, 3, 0x30, 0, 1])),
     3, 2, [rgb565(colors[i]) for i in [3, 0, 3, 1, 2, 1]])
save("rle8-delta.bmp", bmp(8, compression=1, raw=bytes([0, 2, 1, 1, 1, 2, 0, 1])),
     3, 2, [rgb565(colors[i]) for i in [0, 2, 0, 0, 0, 0]])
for name, raw in {
    "rle-overrun": [4, 1, 0, 1], "rle-delta": [0, 2, 4, 0, 0, 1],
    "rle-y": [0, 2, 0, 2, 0, 1], "rle-palette": [1, 4, 0, 1],
    "rle-absolute": [0, 3, 0], "rle-escape": [0], "rle-no-eob": [3, 1],
}.items():
    save(name + ".bmp", bmp(8, compression=1, raw=bytes(raw)))
save("rle4-padding.bmp", bmp(4, width=5, height=1, compression=2, raw=bytes([0, 5, 0x01, 0x23, 0x00])))
save("rle4-padded.bmp", bmp(4, width=5, height=1, compression=2,
     raw=bytes([0, 5, 0x01, 0x23, 0x00, 0, 0, 1])), 5, 1,
     [rgb565(colors[i]) for i in [0, 1, 2, 3, 0]])
save("palette.bmp", bmp(8, indices=[0, 1, 4, 0, 0, 0]))
save("badmask.bmp", bmp(16, compression=3, masks=struct.pack("<III", 0xf800, 0xf800, 0x1f)))
save("noncontiguous.bmp", bmp(16, compression=3, masks=struct.pack("<III", 0xa800, 0x7e0, 0x1f)))
save("top-rle.bmp", bmp(8, top=True, compression=1, raw=bytes([0, 1])))
for name, offset, value in [("zero-width", 18, 0), ("negative-width", 18, 0xffffffff),
                            ("min-height", 22, 0x80000000), ("huge", 18, 0x7fffffff),
                            ("offset", 10, 12), ("palette-count", 46, 257)]:
    data = bytearray(bmp(8))
    struct.pack_into("<I", data, offset, value)
    save(name + ".bmp", data)

def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

def png(width, height, depth, kind, raw, interlace=0, extra=b""):
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, kind, 0, 0, interlace)) + extra +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

rgb = colors[:3] + [colors[3], (128, 64, 32), (0, 0, 0)]
raw = b"".join(b"\0" + b"".join(bytes(c) for c in rgb[y * 3:y * 3 + 3]) for y in range(2))
valid_png = png(3, 2, 8, 2, raw)
save("rgb.png", valid_png, 3, 2, [rgb565(c) for c in rgb])
rgba = b"".join(b"\0" + b"".join(bytes(c) + bytes([x * 70]) for x, c in enumerate(rgb[y * 3:y * 3 + 3])) for y in range(2))
save("rgba.png", png(3, 2, 8, 6, rgba), 3, 2, [rgb565(c) for c in rgb])
save("gray1.png", png(3, 2, 1, 0, b"\0\xa0\0\x40"), 3, 2,
     [0xffff, 0, 0xffff, 0, 0xffff, 0])
save("grayalpha.png", png(2, 1, 8, 4, b"\0\x80\0\xff\xff"), 2, 1, [rgb565((128, 128, 128)), 0xffff])
plte = chunk(b"PLTE", b"".join(bytes(c) for c in colors)) + chunk(b"tRNS", b"\0\xff\x80\xff")
save("palette.png", png(3, 2, 2, 3, b"\0\x18\0\xc4", extra=plte), 3, 2,
     [rgb565(colors[i]) for i in [0, 1, 2, 3, 0, 1]])
for alpha in (False, True):
    short_plte = chunk(b"PLTE", bytes(colors[0]))
    if alpha:
        short_plte += chunk(b"tRNS", b"\x80")
    save(f"png-palette-{alpha}.png", png(1, 1, 8, 3, b"\0\xff", extra=short_plte))
save("rgb16.png", png(3, 2, 16, 2, b"".join(b"\0" + b"".join(bytes([v, 23]) for c in rgb[y*3:y*3+3] for v in c) for y in range(2))),
     3, 2, [rgb565(c) for c in rgb])
w, h = 9, 7
pixels = [((x * 31) % 256, (y * 47) % 256, ((x + y) * 23) % 256) for y in range(h) for x in range(w)]
raw = bytearray()
for sx, sy, dx, dy in [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
                       (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]:
    for y in range(sy, h, dy):
        raw += b"\0" + b"".join(bytes(pixels[y*w+x]) for x in range(sx, w, dx))
save("adam7.png", png(w, h, 8, 2, raw, 1), w, h, [rgb565(c) for c in pixels])
for name, data in [("png-header", valid_png[:20]), ("png-idat", valid_png[:-20]),
                   ("png-end", valid_png[:-1]), ("png-crc", valid_png[:-1] + b"x"),
                   ("png-zlib", png(3, 2, 8, 2, b"\0")),
                   ("png-huge", png(0x7fffffff, 0x7fffffff, 8, 2, b""))]:
    save(name + ".png", data)
# JPEGs were produced with Pillow: RGB 8x8 solid red, quality=100,
# subsampling=0, with progressive=False/True. Runtime needs no Pillow.
for name in ("baseline", "progressive"):
    data = (pathlib.Path(__file__).parent / "jpeg" / (name + ".jpg")).read_bytes()
    save(name + ".jpg", data, 8, 8, [0xf800] * 64)
    save(name + "-truncated.jpg", data[:-2])
    save(name + "-header.jpg", data[:30])
save("unknown.bin", b"not an image")
(root / "cases.txt").write_text("".join(f"{kind} {name}\n" for kind, name in cases), encoding="ascii")
