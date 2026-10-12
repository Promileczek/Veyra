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
    img = base((0x5E, 0x27, 0x50), (0x2C, 0x00, 0x1E))  # bakłażan Ubuntu
    d = ImageDraw.Draw(img)
    d.line([(110, 170), (210, 255), (110, 340)], fill=(0x8A, 0xE2, 0x34), width=44, joint="curve")
    d.rounded_rectangle([250, 318, 400, 356], radius=14, fill=(0xEE, 0xEE, 0xEC))
    return img


def calculator():
    img = base((0x24, 0x26, 0x30), (0x12, 0x13, 0x18))  # własny styl kalkulatora: karta + fioletowe działania
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([88, 80, S - 88, 176], radius=26, fill=(0x2E, 0x31, 0x3D))
    for i in range(9):
        x, y = 88 + (i % 3) * 116, 200 + (i // 3) * 80
        if i == 8:
            grad = Image.new("RGBA", (100, 64))
            gd = ImageDraw.Draw(grad)
            for gx in range(100):
                t = gx / 99
                gd.line([(gx, 0), (gx, 64)], fill=(int(0x7C + (0x3D - 0x7C) * t), int(0x5C + (0x8B - 0x5C) * t), 0xFF, 255))
            m = Image.new("L", (100, 64), 0)
            ImageDraw.Draw(m).rounded_rectangle([0, 0, 99, 63], radius=20, fill=255)
            img.paste(grad, (x, y), m)
        else:
            d.rounded_rectangle([x, y, x + 100, y + 64], radius=20, fill=(0x3A, 0x33, 0x5C) if i % 3 == 2 else (0x34, 0x37, 0x44))
    return img


def store():
    img = base((0x4C, 0xA8, 0xFF), (0x1E, 0x5B, 0xF0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([120, 190, S - 120, 410], radius=34, fill=(255, 255, 255))
    d.arc([186, 100, S - 186, 280], start=180, end=360, fill=(255, 255, 255), width=30)
    d.line([(S // 2, 240), (S // 2, 350)], fill=(0x1E, 0x5B, 0xF0), width=30)
    d.line([(S // 2 - 52, 302), (S // 2, 354), (S // 2 + 52, 302)], fill=(0x1E, 0x5B, 0xF0), width=30, joint="curve")
    return img


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    for name, fn in (("terminal", terminal), ("calculator", calculator), ("store", store)):
        fn().save(OUT / f"{name}.ico", sizes=SIZES)
        fn().resize((128, 128), Image.LANCZOS).save(OUT / f"{name}.png")
        print("ok", name)
