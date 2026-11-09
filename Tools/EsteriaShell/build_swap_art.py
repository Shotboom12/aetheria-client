"""Builds the loose art the character-swap addon needs, with the standard library only (no numpy / Pillow).

  python build_swap_art.py --out <client folder>

Writes, under the given client folder:
  Interface/Esteria/SwapSkyBack.tga, SwapSkyFront.tga          the swap sky (cloud cover + in-world curtain)
  Interface/AddOns/EsteriaSwap/Textures/<name>.tga             the wheel art, converted from easycontroller/*.png

The sky follows Tools/EsteriaShell/build_shell_mpq.py swap_sky (fractal cloud noise over a blue gradient, plus a faster
front layer of wisps); the noise is smoothly interpolated value noise rather than bicubic-resized, so it is similar,
not byte-identical. The wheel art is EasyController's (MIT, see easycontroller/README.md).
"""
import argparse
import pathlib
import random
import struct
import zlib

HERE = pathlib.Path(__file__).resolve().parent


def read_png_rgba(path):
    data = pathlib.Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, ihdr = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        pos += 12 + length
    w, h, depth, ctype, _, _, interlace = ihdr
    assert depth == 8 and ctype == 6 and interlace == 0, "only 8-bit RGBA, non-interlaced: " + str(path)
    raw = zlib.decompress(b"".join(idat))
    bpp, stride = 4, w * 4
    rows, prev = [], bytearray(stride)
    i = 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                left = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows  # rows top to bottom, RGBA


def write_tga(path, w, h, rows_rgba):
    """Uncompressed 32-bit BGRA, top-left origin."""
    head = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x28)
    out = bytearray(head)
    for row in rows_rgba:
        bgra = bytearray(len(row))
        bgra[0::4], bgra[1::4], bgra[2::4], bgra[3::4] = row[2::4], row[1::4], row[0::4], row[3::4]
        out += bgra
    pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(path).write_bytes(bytes(out))


def cloud_noise(width, height, seed, octaves):
    """Fractal value noise normalised to [0, 1], as rows of floats (smooth bilinear interpolation)."""
    rng = random.Random(seed)
    total = [[0.0] * width for _ in range(height)]
    for octave in range(octaves):
        cells = 4 * 2 ** octave
        gw, gh = cells + 1, cells * height // width + 2
        grid = [[rng.random() for _ in range(gw)] for _ in range(gh)]
        weight = 1.0 / 2 ** octave
        xs = []
        for x in range(width):
            fx = x * (gw - 1) / (width - 1)
            x0 = min(int(fx), gw - 2)
            tx = fx - x0
            xs.append((x0, tx * tx * (3.0 - 2.0 * tx)))
        for y in range(height):
            fy = y * (gh - 1) / (height - 1)
            y0 = min(int(fy), gh - 2)
            ty = fy - y0
            ty = ty * ty * (3.0 - 2.0 * ty)
            r0, r1, row = grid[y0], grid[y0 + 1], total[y]
            for x in range(width):
                x0, tx = xs[x]
                top = r0[x0] + (r0[x0 + 1] - r0[x0]) * tx
                bot = r1[x0] + (r1[x0 + 1] - r1[x0]) * tx
                row[x] += (top + (bot - top) * ty) * weight
    lo = min(min(r) for r in total)
    hi = max(max(r) for r in total)
    span = hi - lo or 1.0
    return [[(v - lo) / span for v in r] for r in total]


def blur(rows, radius):
    """Separable box blur (wrapping horizontally so the layer still tiles), applied twice for a smooth falloff."""
    height, width = len(rows), len(rows[0])
    for _ in range(2):
        out = []
        for row in rows:
            acc = sum(row[i % width] for i in range(-radius, radius + 1))
            line = [0.0] * width
            for x in range(width):
                line[x] = acc / (2 * radius + 1)
                acc += row[(x + radius + 1) % width] - row[(x - radius) % width]
            out.append(line)
        rows = out
        cols = [list(c) for c in zip(*rows)]
        done = []
        for col in cols:
            acc = sum(col[min(max(i, 0), height - 1)] for i in range(-radius, radius + 1))
            line = [0.0] * height
            for y in range(height):
                line[y] = acc / (2 * radius + 1)
                acc += col[min(y + radius + 1, height - 1)] - col[max(y - radius, 0)]
            done.append(line)
        rows = [list(r) for r in zip(*done)]
    return rows


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def swap_sky(width=2048, height=512):
    noise = blur(cloud_noise(width, height, 12340, 6), 2)
    wisps = blur(cloud_noise(width, height, 33580, 5), 3)
    top, horizon = (58, 112, 178), (170, 205, 235)
    back, front = [], []
    for y in range(height):
        t = y / (height - 1)
        sky = [top[i] + (horizon[i] - top[i]) * t for i in range(3)]
        brow, frow = bytearray(), bytearray()
        for x in range(width):
            n = noise[y][x]
            cover = clamp((n - 0.62 + 0.35 * t) * 3.0)
            cloud = 246 - 40 * (1 - n)
            brow += bytes((int(sky[0] * (1 - cover) + cloud * cover),
                           int(sky[1] * (1 - cover) + (cloud + 2) * cover),
                           int(sky[2] * (1 - cover) + (cloud + 6) * cover), 255))
            w = wisps[y][x]
            # the passes rush toward the viewer, so the cloud is even from top to bottom and fairly dense
            alpha = clamp((w - 0.45) * 2.6) * 0.9
            shade = int(clamp((235 + 20 * w) / 255.0) * 255)
            frow += bytes((shade, shade, min(255, shade + 4), int(alpha * 255)))
        back.append(bytes(brow))
        front.append(bytes(frow))
    return width, height, back, front


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="client folder (the one holding Data and Interface)")
    out = pathlib.Path(ap.parse_args().out)
    for png in sorted((HERE / "easycontroller").glob("*.png")):
        w, h, rows = read_png_rgba(png)
        write_tga(out / "Interface" / "AddOns" / "EsteriaSwap" / "Textures" / (png.stem + ".tga"), w, h, rows)
    w, h, back, front = swap_sky()
    # the sky layers are rgb in the original; written as rgba with alpha 255 for the back layer
    write_tga(out / "Interface" / "Esteria" / "SwapSkyBack.tga", w, h, back)
    write_tga(out / "Interface" / "Esteria" / "SwapSkyFront.tga", w, h, front)
    print("wrote art under", out)


if __name__ == "__main__":
    main()
