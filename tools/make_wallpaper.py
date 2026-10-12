"""Domyślna tapeta Veyry (res/wallpapers/veyra.jpg): czarne tło, miękkie smugi jak zorza — głęboki niebieski i turkusowa zieleń.
Uruchom: python tools/make_wallpaper.py   (2560×1440, deterministyczna — ten sam wynik za każdym razem)"""
from pathlib import Path

import numpy as np
from PIL import Image

W, H = 2560, 1440
OUT = Path(__file__).resolve().parent.parent / "res" / "wallpapers" / "veyra.jpg"
rng = np.random.default_rng(7)

y, x = np.mgrid[0:H, 0:W].astype(np.float32)
u, v = x / W, y / H


def ribbon(center, amp, freq, phase, width, tilt):
    """Fala (smuga) przez ekran: jasność maleje z odległością od krzywej y = center + amp·sin(...)."""
    curve = center + amp * np.sin(2 * np.pi * (freq * u + phase)) + tilt * (u - 0.5)
    d = (v - curve) / width
    return np.exp(-d * d)


def blob(cx, cy, rx, ry):
    return np.exp(-(((u - cx) / rx) ** 2 + ((v - cy) / ry) ** 2))


# Warstwy światła: (maska, kolor RGB 0–1, siła)
blue = np.array([0.10, 0.36, 1.00])
deep_blue = np.array([0.05, 0.14, 0.55])
teal = np.array([0.00, 0.85, 0.70])
green = np.array([0.15, 0.95, 0.45])

img = np.zeros((H, W, 3), np.float32)
layers = [
    (ribbon(0.58, 0.10, 0.85, 0.10, 0.10, -0.25), teal, 0.55),
    (ribbon(0.62, 0.08, 0.85, 0.16, 0.035, -0.25), green, 0.55),  # jasny, wąski rdzeń smugi
    (ribbon(0.42, 0.12, 0.60, 0.55, 0.10, 0.20), blue, 0.40),
    (ribbon(0.45, 0.10, 0.60, 0.60, 0.04, 0.20), np.array([0.35, 0.65, 1.0]), 0.40),
    (blob(0.18, 0.30, 0.40, 0.35), deep_blue, 0.32),
    (blob(0.85, 0.80, 0.40, 0.35), np.array([0.00, 0.35, 0.30]), 0.50),
    (blob(0.65, 0.20, 0.28, 0.22), np.array([0.05, 0.20, 0.60]), 0.20),
]
for mask, color, strength in layers:
    img += mask[..., None] * color * strength

# Winieta (ciemne brzegi) i ton: miękkie nasycenie, czerń zostaje czarna
vignette = 1.0 - 0.55 * (((u - 0.5) / 0.75) ** 2 + ((v - 0.5) / 0.7) ** 2)
img *= np.clip(vignette, 0.25, 1.0)[..., None]
img = 1.0 - np.exp(-img * 1.35)  # łagodne „przepalenie” jasnych miejsc
img += rng.normal(0, 0.006, img.shape).astype(np.float32)  # ziarno — bez pasków na gradientach

OUT.parent.mkdir(parents=True, exist_ok=True)
Image.fromarray(np.clip(img * 255, 0, 255).astype(np.uint8)).save(OUT, quality=93, subsampling=0)
print("zapisano", OUT)
