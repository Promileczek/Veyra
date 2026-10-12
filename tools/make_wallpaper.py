"""Tapety Veyry (res/wallpapers/, 2560×1440, deterministyczne — ten sam wynik za każdym razem):
  - veyra-domyslna.jpg (domyślna) — górskie jezioro o zmierzchu, trochę przyciemnione,
  - veyra-jezioro.jpg — to samo jezioro, jaśniejsze,
  - veyra-gory.jpg — nocny krajobraz: góry w kilku planach, mgła, las świerkowy, niebo z gwiazdami i zorzą,
  - veyra-zorza.jpg — czarne tło z niebieską i turkusowo-zieloną smugą jak zorza.
Uruchom: python tools/make_wallpaper.py"""
from pathlib import Path

import numpy as np
from PIL import Image

W, H = 2560, 1440
OUT = Path(__file__).resolve().parent.parent / "res" / "wallpapers"

y, x = np.mgrid[0:H, 0:W].astype(np.float32)
u, v = x / W, y / H


def save(img: np.ndarray, name: str, seed: int) -> None:
    rng = np.random.default_rng(seed)
    img = img + rng.normal(0, 0.006, img.shape).astype(np.float32)  # ziarno — bez pasków na gradientach
    OUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(img * 255, 0, 255).astype(np.uint8)).save(OUT / name, quality=93, subsampling=0)
    print("zapisano", name)


def aurora_wallpaper() -> np.ndarray:
    def ribbon(center, amp, freq, phase, width, tilt):
        curve = center + amp * np.sin(2 * np.pi * (freq * u + phase)) + tilt * (u - 0.5)
        d = (v - curve) / width
        return np.exp(-d * d)

    def blob(cx, cy, rx, ry):
        return np.exp(-(((u - cx) / rx) ** 2 + ((v - cy) / ry) ** 2))

    img = np.zeros((H, W, 3), np.float32)
    layers = [
        (ribbon(0.58, 0.10, 0.85, 0.10, 0.10, -0.25), np.array([0.00, 0.85, 0.70]), 0.55),
        (ribbon(0.62, 0.08, 0.85, 0.16, 0.035, -0.25), np.array([0.15, 0.95, 0.45]), 0.55),
        (ribbon(0.42, 0.12, 0.60, 0.55, 0.10, 0.20), np.array([0.10, 0.36, 1.00]), 0.40),
        (ribbon(0.45, 0.10, 0.60, 0.60, 0.04, 0.20), np.array([0.35, 0.65, 1.0]), 0.40),
        (blob(0.18, 0.30, 0.40, 0.35), np.array([0.05, 0.14, 0.55]), 0.32),
        (blob(0.85, 0.80, 0.40, 0.35), np.array([0.00, 0.35, 0.30]), 0.50),
        (blob(0.65, 0.20, 0.28, 0.22), np.array([0.05, 0.20, 0.60]), 0.20),
    ]
    for mask, color, strength in layers:
        img += mask[..., None] * color * strength
    vignette = 1.0 - 0.55 * (((u - 0.5) / 0.75) ** 2 + ((v - 0.5) / 0.7) ** 2)
    img *= np.clip(vignette, 0.25, 1.0)[..., None]
    return 1.0 - np.exp(-img * 1.35)


def ridge(rng, base, roughness, sharp=0.55):
    """Grań gór (wysokość 0–1 od góry): przesunięcie punktu środkowego — ostre, nieregularne szczyty."""
    n = 2 ** 10 + 1
    h = np.zeros(n, np.float64)
    h[0], h[-1] = rng.uniform(-1, 1) * roughness, rng.uniform(-1, 1) * roughness
    step, amp = n - 1, roughness
    while step > 1:
        half = step // 2
        for i in range(half, n - 1, step):
            h[i] = (h[i - half] + h[i + half]) / 2 + rng.uniform(-1, 1) * amp
        step, amp = half, amp * sharp
    return (base - h[np.linspace(0, n - 1, W).astype(int)]).astype(np.float32)


def noise1d(rng, freqs):
    """Gładki szum wzdłuż x: suma fal o losowych częstotliwościach i fazach (0–1)."""
    xs = np.linspace(0, 1, W)
    out = np.zeros(W, np.float32)
    for f in freqs:
        out += np.sin(2 * np.pi * (f * rng.uniform(0.8, 1.25) * xs + rng.uniform(0, 1))) / len(freqs)
    return (out + 1) / 2


def landscape_wallpaper() -> np.ndarray:
    rng = np.random.default_rng(42)
    # Niebo: granat u góry → zielonkawo-niebieska poświata nad horyzontem
    top, horizon = np.array([0.008, 0.016, 0.045]), np.array([0.02, 0.16, 0.20])
    t = np.clip(v / 0.62, 0, 1) ** 1.6
    img = top * (1 - t[..., None]) + horizon * t[..., None]

    # Zorza: miękkie, nieregularne kurtyny — pas faluje, jasność smug zmienia się losowo wzdłuż x
    band = 0.20 + 0.07 * np.sin(2 * np.pi * (0.7 * u + 0.15)) + 0.025 * np.sin(2 * np.pi * (2.1 * u + 0.6)) + 0.05 * (u - 0.5)
    d = (v - band) / 0.10
    curtain = np.exp(-np.where(d < 0, d * d * 0.30, d * d * 2.5))  # długi ogon w górę, ostrzejszy dół
    streaks = 0.35 + 0.65 * noise1d(rng, [3, 7, 13, 23, 37, 61])[None, :] ** 1.5
    strength = 0.25 + 0.75 * noise1d(rng, [0.8, 1.7, 2.9])[None, :]  # gdzieniegdzie zorza prawie znika
    green, teal = np.array([0.20, 1.00, 0.55]), np.array([0.05, 0.55, 0.95])
    mix = np.clip((v - band + 0.10) / 0.18, 0, 1)[..., None]  # wyżej niebieska, niżej zielona
    img += (curtain * streaks * strength)[..., None] * (teal * (1 - mix) + green * mix) * 0.50

    # Gwiazdy (rzadsze nisko i w zorzy)
    n = 2600
    sx, sy = rng.integers(0, W, n), (rng.random(n) ** 1.6 * H * 0.6).astype(int)
    bright = rng.random(n) ** 3 * 0.9 + 0.1
    for px, py, b in zip(sx, sy, bright):
        fade = (1 - py / (H * 0.6)) * (1 - 0.7 * curtain[py, px])
        img[py, px] += b * fade
        if b > 0.75:  # jaśniejsze gwiazdy — mały krzyżyk
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if 0 <= px + dx < W and 0 <= py + dy < H:
                    img[py + dy, px + dx] += b * fade * 0.35

    # Góry: płaskie plany od najdalszego (jaśniejszy, zamglony) do najbliższego (prawie czarny)
    planes = [
        (ridge(rng, 0.58, 0.12), np.array([0.045, 0.115, 0.150])),
        (ridge(rng, 0.66, 0.10), np.array([0.026, 0.075, 0.098])),
        (ridge(rng, 0.74, 0.07), np.array([0.016, 0.050, 0.064])),
    ]
    rows = v[:, :1]  # (H,1)
    for line, color in planes:
        mask = (rows >= line[None, :]).astype(np.float32)
        # lekki gradient w dół i mgła przy podstawie planu (oddziela plany)
        below = np.clip((rows - line[None, :]) / 0.10, 0, 1)
        shade = color * (1.0 - 0.25 * below[..., None])
        img = img * (1 - mask[..., None]) + shade * mask[..., None]
    for line, _ in planes[1:]:
        haze = np.exp(-((rows - (line[None, :] - 0.012)) / 0.025) ** 2) * (rows < line[None, :])
        img += haze[..., None] * np.array([0.03, 0.09, 0.10])

    # Las świerków na pierwszym planie: trójkątne sylwetki o różnej wysokości
    forest = np.zeros((H, W), np.float32)
    base_line = 0.86 + 0.02 * np.sin(2 * np.pi * (1.3 * np.linspace(0, 1, W) + 0.2))
    xpos = 0
    while xpos < W:
        w_tree = rng.integers(26, 60)
        h_tree = rng.uniform(0.07, 0.16)
        cx = xpos + w_tree // 2
        top_y = base_line[min(cx, W - 1)] - h_tree
        x0, x1 = max(0, cx - w_tree), min(W, cx + w_tree)
        xx = np.arange(x0, x1)
        # sylwetka świerka: trójkąt z „piętrami” gałęzi
        rel = np.abs(xx - cx) / w_tree
        for yy in range(int(top_y * H), H):
            frac = (yy / H - top_y) / h_tree  # 0 u czubka, 1 u podstawy
            if frac < 0:
                continue
            tier = (frac * 6) % 1.0
            halfw = min(1.0, frac) * (0.55 + 0.45 * tier)
            forest[yy, x0:x1] = np.maximum(forest[yy, x0:x1], (rel <= halfw).astype(np.float32))
        xpos += int(w_tree * rng.uniform(0.55, 0.95))
    # Mgła nad linią lasu — oddziela las od gór
    mist = np.exp(-((v - (base_line[None, :] - 0.03)) / 0.035) ** 2)
    img += mist[..., None] * np.array([0.02, 0.07, 0.08])
    ground = (v >= base_line[None, :] + 0.005).astype(np.float32)
    trees = np.maximum(forest, ground)
    img = img * (1 - trees[..., None]) + np.array([0.004, 0.012, 0.014]) * trees[..., None]

    vignette = 1.0 - 0.35 * (((u - 0.5) / 0.8) ** 2 + ((v - 0.45) / 0.8) ** 2)
    img *= np.clip(vignette, 0.4, 1.0)[..., None]
    return 1.0 - np.exp(-img * 1.6)


def lake_wallpaper() -> np.ndarray:
    """Jaśniejsza: górskie jezioro o zmierzchu — błękit → turkus nad horyzontem, poświata, góry w planach, odbicie w wodzie."""
    rng = np.random.default_rng(11)
    water = 0.70  # linia brzegu (ułamek wysokości)
    # Niebo
    stops = [(0.00, (0.030, 0.090, 0.260)), (0.28, (0.080, 0.300, 0.580)), (0.46, (0.250, 0.660, 0.760)), (0.56, (0.560, 0.900, 0.820)),
             (0.70, (0.620, 0.930, 0.860))]
    sky = np.zeros((H, W, 3), np.float32)
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        t = np.clip((v - a) / (b - a), 0, 1)[..., None]
        sel = ((v >= a) & (v <= b))[..., None]
        sky = np.where(sel, np.array(ca) * (1 - t) + np.array(cb) * t, sky)
    sky = np.where((v > stops[-1][0])[..., None], np.array(stops[-1][1]), sky)
    img = sky.astype(np.float32)
    # Poświata nad horyzontem (zachodzące słońce za górami)
    glow = np.exp(-(((u - 0.62) / 0.25) ** 2 + ((v - 0.52) / 0.12) ** 2))
    img += glow[..., None] * np.array([0.30, 0.38, 0.22])
    # Obłoki: szum 2D (mała losowa siatka powiększona i rozmyta), rozciągnięty poziomo, tylko w środku nieba
    def noise2d(gw, gh):
        small = Image.fromarray((rng.random((gh, gw)) * 255).astype(np.uint8))
        return np.asarray(small.resize((W, H), Image.BICUBIC), np.float32) / 255
    clouds = 0.6 * noise2d(9, 18) + 0.3 * noise2d(24, 40) + 0.1 * noise2d(60, 90)
    clouds = np.clip((clouds - 0.55) / 0.25, 0, 1) ** 1.5
    band = np.exp(-((v - 0.30) / 0.12) ** 2)
    img += (clouds * band)[..., None] * np.array([0.30, 0.42, 0.48])
    # Gwiazdy tylko wysoko
    n = 500
    sx, sy = rng.integers(0, W, n), (rng.random(n) ** 2 * H * 0.25).astype(int)
    for px, py, b in zip(sx, sy, rng.random(n) ** 3 * 0.6):
        img[py, px] += b * (1 - py / (H * 0.25))
    # Góry: od jasnego, zamglonego planu do ciemnego turkusu
    rows = v[:, :1]
    planes = [
        (ridge(rng, 0.53, 0.12), np.array([0.280, 0.560, 0.620])),
        (ridge(rng, 0.60, 0.10), np.array([0.120, 0.400, 0.420])),
        (ridge(rng, 0.66, 0.06), np.array([0.050, 0.240, 0.230])),
    ]
    for line, color in planes:
        line = np.minimum(line, water)
        mask = (rows >= line[None, :]).astype(np.float32)
        below = np.clip((rows - line[None, :]) / 0.10, 0, 1)
        img = img * (1 - mask[..., None]) + (color * (1.0 - 0.20 * below[..., None])) * mask[..., None]
        haze = np.exp(-((rows - (line[None, :] + 0.03)) / 0.02) ** 2) * mask * 0.22
        img += haze[..., None] * np.array([0.10, 0.18, 0.18])
    # Las na brzegu
    shore = water - 0.012 - 0.006 * noise1d(rng, [2, 5, 11])
    forest = np.zeros((H, W), np.float32)
    xpos = 0
    while xpos < W:
        w_tree, h_tree = rng.integers(10, 26), rng.uniform(0.025, 0.065)
        cx = xpos + w_tree // 2
        top_y = shore[min(cx, W - 1)] - h_tree
        x0, x1 = max(0, cx - w_tree), min(W, cx + w_tree)
        rel = np.abs(np.arange(x0, x1) - cx) / w_tree
        for yy in range(int(top_y * H), int(water * H) + 1):
            frac = (yy / H - top_y) / h_tree
            if frac >= 0:
                forest[yy, x0:x1] = np.maximum(forest[yy, x0:x1], (rel <= min(1.0, frac) * (0.6 + 0.4 * ((frac * 5) % 1.0))).astype(np.float32))
        xpos += int(w_tree * rng.uniform(0.6, 1.0))
    forest = np.maximum(forest, ((rows >= shore[None, :]) & (rows < water)).astype(np.float32))
    img = img * (1 - forest[..., None]) + np.array([0.015, 0.075, 0.060]) * forest[..., None]
    # Jezioro: odbicie lustrzane z falowaniem, ciemniejsze i chłodniejsze
    wy = int(water * H)
    depth = H - wy
    src_rows = (wy - 1 - np.arange(depth) * 1.0).astype(int).clip(0, wy - 1)
    refl = img[src_rows].copy()
    ripple = (np.sin(np.arange(depth)[:, None] * 0.9 + np.linspace(0, 40, W)[None, :]) * (2 + np.arange(depth)[:, None] * 0.02)).astype(int)
    cols = (np.arange(W)[None, :] + ripple).clip(0, W - 1)
    refl = refl[np.arange(depth)[:, None], cols]
    fade = (0.75 - 0.35 * (np.arange(depth) / depth))[:, None, None]
    img[wy:] = refl * fade * np.array([0.80, 0.97, 0.95]) + np.array([0.0, 0.03, 0.03])
    img[wy:wy + 2] += 0.12  # jasna linia brzegu
    vignette = 1.0 - 0.30 * (((u - 0.5) / 0.85) ** 2 + ((v - 0.5) / 0.85) ** 2)
    img *= np.clip(vignette, 0.5, 1.0)[..., None]
    return np.clip(img, 0, 1)


if __name__ == "__main__":
    lake = lake_wallpaper()
    save(lake, "veyra-jezioro.jpg", 3)
    save(np.clip(lake, 0, 1) ** 1.2 * 0.80, "veyra-domyslna.jpg", 3)  # domyślna: to samo jezioro, trochę ciemniejsze
    save(landscape_wallpaper(), "veyra-gory.jpg", 1)
    save(aurora_wallpaper(), "veyra-zorza.jpg", 7)
