#!/usr/bin/env python3
"""Genera kernel/images.h: tres imágenes originales de LirioOS (dibujadas con código)
en 224x136 y 256 colores, listas para incluir en el kernel.

Uso:  python3 tools/gen_images.py [carpeta_de_vistas_previas]
"""
import math
import random
import sys
from PIL import Image, ImageDraw, ImageFilter

W, H = 224, 136
SS = 4  # supersampling


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(top, bottom, y / (h - 1)))
    return img


def petal(cx, cy, length, width, angle, color, img):
    """Pétalo: elipse puntiaguda girada alrededor de (cx,cy)."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    pts = []
    for i in range(41):
        t = i / 40
        x = length * t
        y = width * math.sin(math.pi * t) ** 0.8
        pts.append((x, y))
    for i in range(40, -1, -1):
        t = i / 40
        x = length * t
        y = -width * math.sin(math.pi * t) ** 0.8
        pts.append((x, y))
    ca, sa = math.cos(angle), math.sin(angle)
    poly = [(cx + x * ca - y * sa, cy + x * sa + y * ca) for x, y in pts]
    d.polygon(poly, fill=color)
    mid = [(cx + (length * t) * ca, cy + (length * t) * sa) for t in (0.1, 0.85)]
    d.line(mid, fill=lerp(color[:3], (150, 100, 190), 0.5) + (255,), width=max(1, SS // 2))
    img.paste(layer, (0, 0), layer)


def img_lirio():
    w, h = W * SS, H * SS
    img = gradient(w, h, (52, 24, 104), (178, 132, 224))
    d = ImageDraw.Draw(img)
    rnd = random.Random(7)
    # destellos suaves
    for _ in range(26):
        x, y = rnd.randrange(w), rnd.randrange(int(h * 0.7))
        r = rnd.randrange(2, 5) * SS // 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=(235, 220, 255))
    # tallo y hojas
    cx, cy = w * 0.5, h * 0.46
    d.line([(cx, cy), (cx - 6 * SS, h)], fill=(70, 130, 90), width=5 * SS)
    for sgn in (-1, 1):
        pts = [(cx - 3 * SS, h * 0.84), (cx + sgn * 44 * SS, h * 0.62), (cx + sgn * 8 * SS, h * 0.86)]
        d.polygon(pts, fill=(84, 150, 104))
    # seis pétalos (tres detrás, tres delante)
    back = (226, 200, 248, 255)
    front = (248, 232, 255, 255)
    for k in range(3):
        petal(cx, cy, 40 * SS, 11 * SS, math.radians(-90 + 120 * k + 60), back, img)
    for k in range(3):
        petal(cx, cy, 44 * SS, 12 * SS, math.radians(-90 + 120 * k), front, img)
    d = ImageDraw.Draw(img)
    # estambres
    for k in range(6):
        a = math.radians(-90 + 60 * k + 15)
        ex, ey = cx + math.cos(a) * 17 * SS, cy + math.sin(a) * 17 * SS
        d.line([(cx, cy), (ex, ey)], fill=(255, 220, 120), width=SS)
        d.ellipse([ex - 2 * SS, ey - 2 * SS, ex + 2 * SS, ey + 2 * SS], fill=(190, 90, 60))
    d.ellipse([cx - 3 * SS, cy - 3 * SS, cx + 3 * SS, cy + 3 * SS], fill=(255, 240, 160))
    return img


def img_olas():
    w, h = W * SS, H * SS
    img = gradient(w, h, (70, 36, 130), (244, 150, 190))
    d = ImageDraw.Draw(img)
    # sol
    sx, sy, sr = w * 0.68, h * 0.52, 26 * SS
    glow = Image.new("RGB", img.size, (0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([sx - sr * 2, sy - sr * 2, sx + sr * 2, sy + sr * 2], fill=(90, 50, 40))
    glow = glow.filter(ImageFilter.GaussianBlur(14 * SS))
    px = img.load()
    gp = glow.load()
    for y in range(h):
        for x in range(w):
            g = gp[x, y]
            r, gg, b = px[x, y]
            px[x, y] = (min(255, r + g[0]), min(255, gg + g[1]), min(255, b + g[2]))
    d = ImageDraw.Draw(img)
    d.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], fill=(255, 226, 170))
    # olas
    colors = [(120, 70, 190), (98, 52, 168), (76, 38, 146), (54, 26, 120), (36, 16, 92)]
    for i, col in enumerate(colors):
        base = h * (0.58 + 0.085 * i)
        amp = (4 + i * 1.5) * SS
        freq = 2 * math.pi / ((60 - i * 6) * SS)
        phase = i * 1.7
        pts = [(0, h)]
        for x in range(0, w + 1, 2):
            pts.append((x, base + amp * math.sin(x * freq + phase)))
        pts.append((w, h))
        d.polygon(pts, fill=col)
        # espuma
        for x in range(0, w, 2):
            y = base + amp * math.sin(x * freq + phase)
            d.line([(x, y), (x + 2, y)], fill=lerp(col, (240, 210, 255), 0.45), width=SS // 2)
    # reflejo del sol
    for j in range(7):
        y = h * 0.6 + j * 9 * SS
        ww = (26 - j * 3) * SS
        d.line([(sx - ww, y), (sx + ww, y)], fill=(250, 200, 190), width=SS)
    return img


def sparkle(d, x, y, r, col):
    d.polygon([(x, y - r), (x + r * 0.22, y - r * 0.22), (x + r, y), (x + r * 0.22, y + r * 0.22),
               (x, y + r), (x - r * 0.22, y + r * 0.22), (x - r, y), (x - r * 0.22, y - r * 0.22)], fill=col)


def img_destellos():
    w, h = W * SS, H * SS
    img = gradient(w, h, (14, 8, 44), (96, 52, 150))
    d = ImageDraw.Draw(img)
    rnd = random.Random(21)
    for _ in range(70):
        x, y = rnd.randrange(w), rnd.randrange(int(h * 0.75))
        r = rnd.choice([1, 1, 2]) * SS // 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=(220, 210, 255))
    for _ in range(9):
        sparkle(d, rnd.randrange(w), rnd.randrange(int(h * 0.6)), rnd.randrange(5, 11) * SS, (255, 245, 255))
    # luna creciente
    mx, my, mr = w * 0.24, h * 0.30, 22 * SS
    mask = Image.new("L", img.size, 0)
    md = ImageDraw.Draw(mask)
    md.ellipse([mx - mr, my - mr, mx + mr, my + mr], fill=255)
    md.ellipse([mx - mr + 11 * SS, my - mr - 4 * SS, mx + mr + 11 * SS, my + mr - 4 * SS], fill=0)
    img.paste((255, 240, 200), (0, 0), mask)
    d = ImageDraw.Draw(img)
    # colinas
    for i, col in enumerate([(70, 36, 120), (46, 22, 90), (28, 12, 62)]):
        base = h * (0.68 + 0.09 * i)
        pts = [(0, h)]
        for x in range(0, w + 1, 2):
            pts.append((x, base + (10 - i * 2) * SS * math.sin(x / ((70 - i * 12) * SS) + i * 2.3)))
        pts.append((w, h))
        d.polygon(pts, fill=col)
    return img


def to_indexed(img):
    small = img.resize((W, H), Image.LANCZOS)
    return small.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE)


def emit(f, name, q):
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    f.write(f"static const u32 pal_{name}[256] = {{\n")
    for i in range(0, 256, 8):
        row = ", ".join("0x%02X%02X%02X" % tuple(pal[(i + k) * 3:(i + k) * 3 + 3]) for k in range(8))
        f.write("    " + row + ",\n")
    f.write("};\n")
    data = list(q.tobytes())
    f.write(f"static const u8 idx_{name}[{W * H}] = {{\n")
    for i in range(0, len(data), 28):
        f.write("    " + ",".join(str(v) for v in data[i:i + 28]) + ",\n")
    f.write("};\n")
    f.write(f"static const struct img img_{name} = {{ {W}, {H}, pal_{name}, idx_{name} }};\n\n")


def main():
    out = "kernel/images.h"
    prev = sys.argv[1] if len(sys.argv) > 1 else None
    imgs = [("lirio", img_lirio()), ("olas", img_olas()), ("destellos", img_destellos())]
    with open(out, "w") as f:
        f.write("/* Imágenes de ejemplo de LirioOS, generadas por tools/gen_images.py (no editar a mano).\n"
                " * Cada imagen: 224x136 píxeles, paleta de 256 colores (0xRRGGBB) e índices. */\n\n"
                "#ifndef LIRIO_IMAGES_H\n#define LIRIO_IMAGES_H\n\n"
                "struct img { u16 w, h; const u32 *pal; const u8 *idx; };\n\n")
        for name, im in imgs:
            q = to_indexed(im)
            emit(f, name, q)
            if prev:
                q.convert("RGB").resize((W * 3, H * 3), Image.NEAREST).save(f"{prev}/{name}.png")
        f.write("#endif\n")
    print("escrito", out)


if __name__ == "__main__":
    main()
