"""Pobiera ikonki programów Sklepu do res/store/*.png (128×128, przezroczyste tło) — raz, wynik jest w repozytorium
i wbudowany w exploder-store.exe (bez pobierania przy starcie). Uruchom: python tools/fetch_store_icons.py

Źródła: dashboard-icons (homarr-labs), Wikimedia Commons, GitHub projektów, icon.horse (największa ikona strony).
Obróbka: największa klatka (ICO), białe tło połączone z krawędzią → przezroczyste, przycięcie, kwadrat z marginesem."""
import io
import re
import time
import urllib.request
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent.parent / "res" / "store"
UA = "VeyraIconFetch/1.0 (github.com/Promileczek/Veyra)"
DASH = "https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/{}.png"
WIKI = "https://commons.wikimedia.org/wiki/Special:FilePath/{}?width=256"
HORSE = "https://icon.horse/icon/{}"

SOURCES = {
    "Brave.Brave": DASH.format("brave"),
    "Google.Chrome": DASH.format("google-chrome"),
    "Mozilla.Firefox": DASH.format("firefox"),
    "Opera.OperaGX": WIKI.format("Opera_GX_Icon.svg"),
    "Discord.Discord": DASH.format("discord"),
    "Telegram.TelegramDesktop": DASH.format("telegram"),
    "OpenWhisperSystems.Signal": DASH.format("signal"),
    "Zoom.Zoom": DASH.format("zoom"),
    "Mozilla.Thunderbird": DASH.format("thunderbird"),
    "Valve.Steam": DASH.format("steam"),
    "EpicGames.EpicGamesLauncher": DASH.format("epic-games"),
    "Blizzard.BattleNet": WIKI.format("Battle.net_logo.svg"),
    "ElectronicArts.EADesktop": DASH.format("electronic-arts"),
    "Ubisoft.Connect": WIKI.format("Ubisoft_logo.svg"),
    "Spotify.Spotify": DASH.format("spotify"),
    "VideoLAN.VLC": WIKI.format("VLC_Icon.svg"),
    "OBSProject.OBSStudio": WIKI.format("OBS_Studio_Logo.svg"),
    "Audacity.Audacity": DASH.format("audacity"),
    "ShareX.ShareX": WIKI.format("ShareX_Logo.png"),
    "7zip.7zip": WIKI.format("7-Zip_Icon.svg"),
    "RARLab.WinRAR": HORSE.format("win-rar.com"),
    "voidtools.Everything": WIKI.format("Everything_(software)_logo.png"),
    "Microsoft.PowerToys": "https://raw.githubusercontent.com/microsoft/PowerToys/main/src/runner/svgs/icon.ico",
    "qBittorrent.qBittorrent": DASH.format("qbittorrent"),
    "AnyDesk.AnyDesk": DASH.format("anydesk"),
    "TeamViewer.TeamViewer": HORSE.format("teamviewer.com"),
    "REALiX.HWiNFO": HORSE.format("hwinfo.com"),
    "CPUID.CPU-Z": WIKI.format("CPU-Z_Icon.svg"),
    "Rufus.Rufus": "https://raw.githubusercontent.com/pbatard/rufus/master/res/icons/rufus-128.png",
    "Bitwarden.Bitwarden": DASH.format("bitwarden"),
    "Microsoft.VisualStudioCode": DASH.format("visual-studio-code"),
    "Notepad++.Notepad++": WIKI.format("Notepad_plus_plus.png"),
    "Git.Git": DASH.format("git"),
    "Python.Python.3.12": DASH.format("python"),
    "OpenJS.NodeJS.LTS": DASH.format("nodejs"),
    "Microsoft.WindowsTerminal": WIKI.format("Windows_Terminal_logo.svg"),
    "PuTTY.PuTTY": DASH.format("putty"),
    "WinSCP.WinSCP": WIKI.format("WinSCP_Logo.png"),
    "TheDocumentFoundation.LibreOffice": DASH.format("libreoffice"),
    "GIMP.GIMP": DASH.format("gimp"),
    "KDE.Krita": WIKI.format("Calligrakrita-base.svg"),
    "Inkscape.Inkscape": WIKI.format("Inkscape_Logo.svg"),
    "dotPDN.PaintDotNet": WIKI.format("Paint.NET_Logo.png"),
    "BlenderFoundation.Blender": DASH.format("blender"),
}
SIZE = 128
# Logo z napisem → sam znak: (lewo, góra, prawo, dół) jako ułamki rozmiaru obrazka
CROP = {
    "Blizzard.BattleNet": (0.25, 0.0, 0.75, 0.74),
    "Ubisoft.Connect": (0.0, 0.0, 1.0, 0.66),
}


def resource_name(package_id: str) -> str:
    """Nazwa pliku/zasobu: identyfikator winget z samymi literami i cyframi (reszta → _)."""
    return re.sub(r"[^A-Za-z0-9]", "_", package_id)


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def largest_frame(data: bytes) -> Image.Image:
    im = Image.open(io.BytesIO(data))
    if im.format == "ICO":
        im.size = max(im.ico.sizes())
    return im.convert("RGBA")


def strip_white(im: Image.Image) -> Image.Image:
    """Białe/jasne tło połączone z krawędzią → przezroczyste (tylko gdy rogi są jasne i nieprzezroczyste)."""
    w, h = im.size
    px = im.load()
    corners = [px[0, 0], px[w - 1, 0], px[0, h - 1], px[w - 1, h - 1]]
    if not any(c[3] > 200 and min(c[:3]) > 230 for c in corners):
        return im
    seen = set()
    stack = [(x, 0) for x in range(w)] + [(x, h - 1) for x in range(w)] + [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        r, g, b, a = px[x, y]
        if a == 0 or min(r, g, b) <= 225:
            continue
        seen.add((x, y))
        px[x, y] = (0, 0, 0, 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return im


def crop_mark(pid: str, im: Image.Image) -> Image.Image:
    if pid not in CROP:
        return im
    l, t, r, b = CROP[pid]
    return im.crop((int(l * im.width), int(t * im.height), int(r * im.width), int(b * im.height)))


def lighten_dark(im: Image.Image) -> Image.Image:
    """Ciemne, jednokolorowe logo (np. Ubisoft, LibreOffice) zniknęłoby na czarnym tle → odwracamy na jasne."""
    px = [p for p in im.getdata() if p[3] > 160]
    if not px:
        return im
    lum = sum(0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b, _ in px) / len(px) / 255
    sat = sum(max(r, g, b) - min(r, g, b) for r, g, b, _ in px) / len(px) / 255
    if lum < 0.3 and sat < 0.15:
        im.putdata([(255 - r, 255 - g, 255 - b, a) for r, g, b, a in im.getdata()])
    return im


def square(im: Image.Image) -> Image.Image:
    box = im.getchannel("A").getbbox()
    if box:
        im = im.crop(box)
    side = int(max(im.size) * 1.08)  # mały margines
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(im, ((side - im.width) // 2, (side - im.height) // 2), im)
    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for pid, url in SOURCES.items():
        out = OUT / f"{resource_name(pid)}.png"
        if out.exists():
            continue
        for attempt in range(3):
            try:
                square(lighten_dark(strip_white(crop_mark(pid, largest_frame(fetch(url)))))).save(out)
                print("ok   ", pid)
                break
            except Exception as e:  # noqa: BLE001 — log i kolejna próba (Wikimedia ogranicza tempo)
                print("blad ", pid, e)
                time.sleep(5)
        time.sleep(1.5)
    # Lista zasobów dla exploder-store.exe
    rc = Path(__file__).resolve().parent.parent / "apps" / "store" / "store_icons.rc"
    lines = ["// Wygenerowane przez tools/fetch_store_icons.py — ikonki programów Sklepu (RCDATA, nazwa = identyfikator winget)"]
    for png in sorted(OUT.glob("*.png")):
        lines.append(f'I_{png.stem.upper()} RCDATA "../../res/store/{png.name}"')
    rc.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("zapisano", rc.name)
