"""Makes app.ico from the middle of a screenshot taken with Starfield.exe /x.

    python tools/make_icon.py shot_0.bmp app.ico

Without a screenshot, writes a plain black icon so the program can be built
for the first time.
"""
import struct
import sys
import zlib


def read_bmp(path):
    data = open(path, "rb").read()
    off, = struct.unpack_from("<I", data, 10)
    w, h = struct.unpack_from("<ii", data, 18)
    bpp, = struct.unpack_from("<H", data, 28)
    assert bpp == 24
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(abs(h)):
        row = data[off + y * stride: off + y * stride + w * 3]
        rows.append([(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]) for x in range(w)])
    if h > 0:
        rows.reverse()
    return w, abs(h), rows


def shrink(rows, x0, y0, size, out):
    """Box-filters the size x size square at x0, y0 down to out x out."""
    k = size / out
    img = []
    for oy in range(out):
        line = []
        for ox in range(out):
            ya, yb = int(y0 + oy * k), int(y0 + (oy + 1) * k)
            xa, xb = int(x0 + ox * k), int(x0 + (ox + 1) * k)
            r = g = b = n = 0
            for y in range(ya, max(yb, ya + 1)):
                for x in range(xa, max(xb, xa + 1)):
                    p = rows[y][x]
                    r += p[0]; g += p[1]; b += p[2]; n += 1
            # Small icons are mostly black; brighten them so the streaks show.
            boost = 1.0 + 1.5 * (1 - out / 256)
            line.append(tuple(min(255, int(c / n * boost)) for c in (r, g, b)))
        img.append(line)
    return img


def png(img):
    h, w = len(img), len(img[0])
    raw = b"".join(b"\0" + bytes(c for p in row for c in (*p, 255)) for row in img)
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) == 2:
        w, h, rows = 64, 64, [[(0, 0, 0)] * 64 for _ in range(64)]
        out = sys.argv[1]
    else:
        w, h, rows = read_bmp(sys.argv[1])
        out = sys.argv[2]
    size = min(w, h)
    x0, y0 = (w - size) // 2, (h - size) // 2
    images = [png(shrink(rows, x0, y0, size, s)) for s in (256, 48, 32, 16)]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for s, data in zip((256, 48, 32, 16), images):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    open(out, "wb").write(header + entries + b"".join(images))


main()
