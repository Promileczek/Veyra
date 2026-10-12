"""Generuje ikony aplikacji Explodera (res/*.ico) — Terminal, Kalkulator, Sklep. Uruchom: python tools/make_icons.py"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent / "res"
S = 512  # rysujemy duże, potem zmniejszamy
SIZES = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)]


def base(top, bottom):
    """Zaokrąglony kwadrat z pionowym gradientem (styl Big Sur / Liquid Glass)."""
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        gd.line([(0, y), (S, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([16, 16, S - 16, S - 16], radius=112, fill=255)
    img.paste(grad, (0, 0), mask)
    # Delikatny połysk u góry
    gloss = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(gloss).rounded_rectangle([16, 16, S - 16, S // 2], radius=112, fill=(255, 255, 255, 28))
    img.alpha_composite(Image.composite(gloss, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask))
    return img


def font(size, bold=True):
    for name in (["consolab.ttf", "segoeuib.ttf"] if bold else ["consola.ttf", "segoeui.ttf"]):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def terminal():
    img = base((0x2A, 0x2C, 0x34), (0x0E, 0x0F, 0x13))  # ciemne tło terminala
    d = ImageDraw.Draw(img)
    d.line([(110, 170), (210, 255), (110, 340)], fill=(0x8A, 0xE2, 0x34), width=44, joint="curve")
    d.rounded_rectangle([250, 318, 400, 356], radius=14, fill=(0xEE, 0xEE, 0xEC))
    return img


def calculator():
    img = base((0x2A, 0x2A, 0x31), (0x18, 0x18, 0x1D))  # grafit Veyry
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([84, 78, S - 84, 172], radius=20, fill=(0x34, 0x34, 0x3C), outline=(0x55, 0x55, 0x60), width=3)
    for i in range(9):
        x, y = 84 + (i % 3) * 118, 200 + (i // 3) * 82
        blue = i % 3 == 2
        d.rounded_rectangle([x, y, x + 104, y + 66], radius=16, fill=(0x61, 0xB3, 0xFF) if blue else (0x3A, 0x3A, 0x43))
    return img


def store():
    img = base((0x4C, 0xA8, 0xFF), (0x1E, 0x5B, 0xF0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([120, 190, S - 120, 410], radius=34, fill=(255, 255, 255))
    d.arc([186, 100, S - 186, 280], start=180, end=360, fill=(255, 255, 255), width=30)
    d.line([(S // 2, 240), (S // 2, 350)], fill=(0x1E, 0x5B, 0xF0), width=30)
    d.line([(S // 2 - 52, 302), (S // 2, 354), (S // 2 + 52, 302)], fill=(0x1E, 0x5B, 0xF0), width=30, joint="curve")
    return img


def settings():
    import math
    img = base((0x2A, 0x2A, 0x31), (0x18, 0x18, 0x1D))  # grafit Veyry
    d = ImageDraw.Draw(img)
    c, teeth, r_out, r_in = S / 2, 8, 176, 140
    pts = []
    for i in range(teeth * 4):  # ząb: dwa punkty na zewnątrz, dwa wewnątrz
        a = 2 * math.pi * i / (teeth * 4) - math.pi / (teeth * 4)
        r = r_out if (i % 4) in (1, 2) else r_in
        pts.append((c + r * math.cos(a), c + r * math.sin(a)))
    d.polygon(pts, fill=(0x61, 0xB3, 0xFF))
    d.ellipse([c - 120, c - 120, c + 120, c + 120], fill=(0x61, 0xB3, 0xFF))
    d.ellipse([c - 58, c - 58, c + 58, c + 58], fill=(0x20, 0x20, 0x26))
    return img


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    for name, fn in (("terminal", terminal), ("calculator", calculator), ("store", store), ("settings", settings)):
        fn().save(OUT / f"{name}.ico", sizes=SIZES)
        fn().resize((128, 128), Image.LANCZOS).save(OUT / f"{name}.png")
        print("ok", name)
