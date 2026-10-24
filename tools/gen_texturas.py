#!/usr/bin/env python3
"""
gen_texturas: genera las texturas de materiales de los modelos (madera, piedra, tejas...).

Todas son procedurales y deterministas (misma semilla = mismo resultado) y las que cubren
superficies grandes se repiten SIN costuras: el ruido se calcula sobre una rejilla periodica.
El acabado es el de la PS1, igual que texconv.py: 32 niveles por canal y trama Bayer 4x4.

Uso:
  python tools/gen_texturas.py                 # escribe en assets/textures/
  python tools/gen_texturas.py --out carpeta --preview   # y ampliaciones x6 para revisar

Requiere:  pip install pillow numpy
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texconv import quantize_ordered  # noqa: E402  (misma cuantizacion que el resto)


# ---------------------------------------------------------------------------
# Ruido periodico
# ---------------------------------------------------------------------------
def value_noise(rng, size, cells):
    """Ruido de valor periodico: rejilla de cells x cells puntos aleatorios, interpolada con
    smoothstep. Como la rejilla se repite (np.roll), el resultado encaja consigo mismo."""
    h, w = size
    cy, cx = cells
    grid = rng.random((cy, cx)).astype(np.float32)
    ys = np.arange(h, dtype=np.float32) * cy / h
    xs = np.arange(w, dtype=np.float32) * cx / w
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    fy = ys - y0
    fx = xs - x0
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    y1 = (y0 + 1) % cy
    x1 = (x0 + 1) % cx
    a = grid[np.ix_(y0 % cy, x0 % cx)]
    b = grid[np.ix_(y0 % cy, x1)]
    c = grid[np.ix_(y1, x0 % cx)]
    d = grid[np.ix_(y1, x1)]
    top = a + (b - a) * fx[None, :]
    bot = c + (d - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]


def fbm(rng, size, base=(4, 4), octaves=4):
    """Suma de octavas de ruido periodico, normalizada a 0..1."""
    total = np.zeros(size, np.float32)
    amp, norm = 1.0, 0.0
    cy, cx = base
    for _ in range(octaves):
        total += amp * value_noise(rng, size, (cy, cx))
        norm += amp
        amp *= 0.5
        cy, cx = min(cy * 2, size[0]), min(cx * 2, size[1])
    return total / norm


def tint(value, dark, light):
    """Mapa 0..1 -> color entre 'dark' y 'light' (RGB 0..255)."""
    dark = np.array(dark, np.float32) / 255.0
    light = np.array(light, np.float32) / 255.0
    return dark + (light - dark) * value[..., None]


# ---------------------------------------------------------------------------
# Texturas que se repiten
# ---------------------------------------------------------------------------
def madera(rng, size=64, planks=4, dark=(58, 36, 22), light=(138, 92, 56), vertical=True):
    """Tablas de madera: veta estirada a lo largo de la tabla, juntas oscuras y algun nudo."""
    h = w = size
    grain = fbm(rng, (h, w), base=(2, 16))  # estirado: pocas celdas a lo largo, muchas a lo ancho
    fine = fbm(rng, (h, w), base=(8, 32), octaves=2)
    v = 0.55 * grain + 0.25 * fine
    pw = w // planks
    x = np.arange(w)
    for p in range(planks):
        cols = (x >= p * pw) & (x < (p + 1) * pw)
        v[:, cols] += rng.uniform(-0.12, 0.12)  # cada tabla con su tono
        # nudo: una mancha oscura eliptica en un sitio al azar de la tabla
        if rng.random() < 0.6:
            ky, kx = rng.integers(0, h), p * pw + rng.integers(3, pw - 3)
            yy, xx = np.mgrid[0:h, 0:w]
            dy = np.minimum(abs(yy - ky), h - abs(yy - ky)) / 3.5
            dx = (xx - kx) / 1.8
            v -= 0.35 * np.exp(-(dx * dx + dy * dy))
    v = np.clip(v, 0, 1)
    rgb = tint(v, dark, light)
    seam = (x % pw == 0)
    rgb[:, seam] *= 0.35          # junta entre tablas
    rgb[:, (x % pw == 1)] *= 1.12  # borde de la tabla un poco mas claro
    if not vertical:
        rgb = np.transpose(rgb, (1, 0, 2))
    return np.clip(rgb, 0, 1)


def piedra(rng, size=64, row=16, dark=(70, 68, 64), light=(158, 152, 140), mortar=(52, 48, 44)):
    """Sillares irregulares: filas de piedras de ancho variable con mortero y volumen."""
    h = w = size
    out = np.zeros((h, w, 3), np.float32)
    detail = fbm(rng, (h, w), base=(8, 8))
    yy, xx = np.mgrid[0:h, 0:w]
    for r in range(h // row):
        # anchos que suman exactamente 'w' para que la fila encaje al repetirse
        widths = []
        while sum(widths) < w - 12:
            widths.append(int(rng.integers(12, 24)))
        widths.append(w - sum(widths))
        x0 = int(rng.integers(0, w))  # desplazamiento de la fila (con vuelta)
        for wd in widths:
            tone = rng.uniform(0.25, 0.85)
            rows = (yy >= r * row) & (yy < (r + 1) * row)
            local_x = (xx - x0) % w
            cols = (local_x >= 0) & (local_x < wd)
            mask = rows & cols
            ly = (yy - r * row) / (row - 1)
            lx = local_x / max(wd - 1, 1)
            # volumen: mas claro arriba a la izquierda, mas oscuro abajo
            shade = 0.18 * (0.5 - ly) + 0.08 * (0.5 - lx)
            val = np.clip(tone + 0.35 * (detail - 0.5) + shade, 0, 1)
            col = tint(val, dark, light)
            out[mask] = col[mask]
            edge = mask & ((local_x == 0) | (yy == r * row))
            out[edge] = np.array(mortar, np.float32) / 255.0
            x0 = (x0 + wd) % w
    return np.clip(out, 0, 1)


def yeso(rng, size=64, base=(184, 172, 146), stain=(98, 86, 66)):
    """Revoque claro con manchas de humedad y motas."""
    blot = fbm(rng, (size, size), base=(3, 3))
    speck = rng.random((size, size)) < 0.04
    v = np.clip((blot - 0.4) * 2.0, 0, 1) ** 1.3
    rgb = tint(1.0 - v * 0.8, stain, base)
    rgb *= (0.94 + 0.08 * fbm(rng, (size, size), base=(16, 16), octaves=2))[..., None]
    rgb[speck] *= 0.82
    return np.clip(rgb, 0, 1)


def tejas(rng, size=64, rows=8, cols=8, dark=(62, 26, 20), light=(150, 66, 44)):
    """Tejado de tejas planas: hileras desplazadas a media teja, sombra en el borde inferior."""
    h = w = size
    rh, cw = h // rows, w // cols
    noise = fbm(rng, (h, w), base=(8, 8), octaves=3)
    out = np.zeros((h, w, 3), np.float32)
    yy, xx = np.mgrid[0:h, 0:w]
    for r in range(rows):
        off = (cw // 2) * (r % 2)
        for c in range(cols):
            tone = rng.uniform(0.35, 0.85)
            lx = (xx - off - c * cw) % w
            mask = (yy >= r * rh) & (yy < (r + 1) * rh) & (lx < cw)
            ly = (yy - r * rh) / (rh - 1)
            val = np.clip(tone + 0.3 * (noise - 0.5) - 0.45 * ly ** 3, 0, 1)
            col = tint(val, dark, light)
            out[mask] = col[mask]
            gap = mask & (lx == 0)
            out[gap] *= 0.45
    return np.clip(out, 0, 1)


def paja(rng, size=64, dark=(84, 64, 30), light=(196, 164, 92)):
    """Techo de paja: hebras casi verticales y franjas de sombra entre capas."""
    h = w = size
    strands = fbm(rng, (h, w), base=(2, 32), octaves=3)
    v = strands
    yy = np.arange(h)[:, None]
    layer = (yy % 16) / 15.0
    v = np.clip(v - 0.35 * layer ** 2 + 0.1, 0, 1)
    return tint(v, dark, light)


def lona(rng, size=64, base=(200, 188, 158), dirt=(120, 104, 80)):
    """Lona de las aspas: tejido fino con suciedad."""
    yy, xx = np.mgrid[0:size, 0:size]
    weave = 0.5 + 0.5 * ((xx % 2) ^ (yy % 2))
    d = np.clip((fbm(rng, (size, size), base=(4, 4)) - 0.4) * 2.0, 0, 1)
    v = (1.0 - 0.7 * d) * (0.93 + 0.07 * weave)
    return tint(v, dirt, base)


def metal(rng, size=32, dark=(40, 38, 36), light=(108, 96, 84), rust=(118, 62, 30)):
    """Hierro oscuro con oxido (aros de cubos y barriles, herrajes)."""
    v = fbm(rng, (size, size), base=(4, 4))
    r = np.clip((fbm(rng, (size, size), base=(4, 4)) - 0.55) * 3.0, 0, 1)
    rgb = tint(v, dark, light)
    rust = np.array(rust, np.float32) / 255.0
    return np.clip(rgb * (1 - r[..., None]) + rust * r[..., None], 0, 1)


def agua(rng, size=64, dark=(10, 24, 34), light=(52, 92, 108)):
    """Agua quieta y oscura: ondas suaves alargadas y algun brillo."""
    ondas = fbm(rng, (size, size), base=(8, 2), octaves=3)
    v = np.clip((ondas - 0.3) * 1.6, 0, 1) ** 1.4
    rgb = tint(v, dark, light)
    brillo = (fbm(rng, (size, size), base=(16, 4), octaves=2) > 0.72)
    rgb[brillo] = np.minimum(rgb[brillo] + 0.12, 1.0)
    return rgb


def oscuro(rng, size=16, base=(16, 14, 12)):
    """Casi negro con algo de ruido: huecos (el interior del pozo, una chimenea)."""
    v = fbm(rng, (size, size), base=(4, 4), octaves=2)
    return tint(v, base, (34, 30, 26))


def tierra(rng, size=64, dark=(40, 28, 20), light=(118, 86, 58), raices=True):
    """Pared de tierra de los tuneles: vetas horizontales de estratos, piedrecitas y raices
    finas que bajan serpenteando."""
    h = w = size
    yy, xx = np.mgrid[0:h, 0:w]
    estratos = fbm(rng, (h, w), base=(8, 2), octaves=3)   # bandas casi horizontales
    grumos = fbm(rng, (h, w), base=(8, 8), octaves=3)
    v = 0.55 * estratos + 0.45 * grumos
    rgb = tint(np.clip((v - 0.2) * 1.5, 0, 1), dark, light)
    # piedrecitas: manchas pequenas con su luz arriba y su sombra abajo
    for _ in range(10):
        cy, cx = rng.integers(0, h), rng.integers(0, w)
        r = rng.uniform(1.0, 2.2)
        dy = (yy - cy + h // 2) % h - h // 2
        dx = (xx - cx + w // 2) % w - w // 2
        d = np.sqrt(dx * dx + dy * dy)
        piedra = d < r
        # del color de la tierra clara, mas o menos aclarado: no destacan como motas grises
        tono = np.array(light, np.float32) / 255.0 * rng.uniform(0.55, 0.95)
        luz = 1.0 + 0.3 * np.clip(-dy[piedra] / r, -1, 1)
        rgb[piedra] = tono * luz[..., None]
        rgb[(d >= r) & (d < r + 1) & (dy > 0)] *= 0.6
    if raices:
        # raices: lineas oscuras que bajan con un vaiven periodico (encajan al repetirse)
        # (suma de senos de frecuencia entera: vaiven irregular pero periodico; y con cortes)
        for _ in range(2):
            x0 = rng.uniform(0, w)
            ondas = [(k, rng.uniform(1, 6) / k, rng.uniform(0, 2 * np.pi)) for k in (1, 2, 3, 5)]
            corte = value_noise(rng, (h, 1), (4, 1))[:, 0]
            for y in range(h):
                if corte[y] < 0.35:
                    continue
                x = int(x0 + sum(a * np.sin(2 * np.pi * k * y / h + f) for k, a, f in ondas)) % w
                rgb[y, x] = rgb[y, x] * 0.4
                rgb[y, (x + 1) % w] = np.minimum(rgb[y, (x + 1) % w] * 1.15, 1.0)
    return np.clip(rgb, 0, 1)


def roca(rng, size=64, dark=(34, 34, 40), light=(112, 110, 118)):
    """Roca oscura y fracturada (derrumbes, orilla del lago)."""
    v = fbm(rng, (size, size), base=(4, 4), octaves=4)
    grietas = np.abs(fbm(rng, (size, size), base=(4, 4), octaves=3) - 0.5) < 0.025
    rgb = tint(np.clip((v - 0.2) * 1.4, 0, 1), dark, light)
    rgb[grietas] *= 0.45
    return np.clip(rgb, 0, 1)


def losas(rng, size=64, n=2, dark=(46, 44, 46), light=(120, 116, 112), junta=(26, 24, 24)):
    """Suelo de losas grandes y gastadas (la cripta)."""
    h = w = size
    out = np.zeros((h, w, 3), np.float32)
    detalle = fbm(rng, (h, w), base=(8, 8))
    yy, xx = np.mgrid[0:h, 0:w]
    lado = size // n
    for j in range(n):
        for i in range(n):
            m = (yy // lado == j) & (xx // lado == i)
            tono = rng.uniform(0.3, 0.7)
            col = tint(np.clip(tono + 0.4 * (detalle - 0.5), 0, 1), dark, light)
            out[m] = col[m]
    borde = (yy % lado == 0) | (xx % lado == 0)
    out[borde] = np.array(junta, np.float32) / 255.0
    # grietas y desgaste
    grietas = np.abs(fbm(rng, (h, w), base=(4, 4), octaves=3) - 0.5) < 0.02
    out[grietas] *= 0.55
    return np.clip(out, 0, 1)


def calaveras(rng, size=64, hueso=(178, 164, 132), sombra=(30, 24, 20)):
    """Pared de osario: hileras de calaveras apiladas con huesos largos entre ellas."""
    h = w = size
    out = tint(fbm(rng, (h, w), base=(8, 8), octaves=2) * 0.4, sombra, (70, 58, 44))
    yy, xx = np.mgrid[0:h, 0:w]
    hueso = np.array(hueso, np.float32) / 255.0
    fila = 16
    for r in range(h // fila):
        y0 = r * fila
        if r % 2 == 1:
            # hilera de huesos largos de canto: barras con los extremos abultados
            for k in range(4):
                tono = rng.uniform(0.7, 1.0)
                yb = y0 + 3 + 3 * k
                barra = (yy >= yb) & (yy < yb + 2)
                out[barra] = hueso * tono * (0.85 + 0.15 * ((xx[barra] // 4) % 2))[..., None]
                for xe in range(0, w, 16):
                    nudo = (np.abs(yy - yb - 0.5) < 2) & (((xx - xe) % w) < 3)
                    out[nudo] = hueso * tono
            continue
        for c in range(w // 16):
            cx = c * 16 + 8 + (4 if r % 4 == 2 else 0)
            cy = y0 + 7
            tono = rng.uniform(0.6, 1.0)
            dx = ((xx - cx + w // 2) % w - w // 2)
            dy = yy - cy
            craneo = (dx / 6.5) ** 2 + (dy / 6.0) ** 2 < 1.0
            mandibula = (np.abs(dx) < 3.5) & (dy >= 3) & (dy < 7)
            cara = craneo | mandibula
            luz = np.clip(1.0 - 0.06 * dy - 0.03 * dx, 0.6, 1.1)
            out[cara] = hueso * tono * luz[cara][..., None]
            for ox in (-2.5, 2.5):
                ojo = ((dx - ox) / 1.8) ** 2 + ((dy - 0.5) / 1.6) ** 2 < 1.0
                out[ojo] = np.array(sombra, np.float32) / 255.0
            nariz = (np.abs(dx) < 1) & (dy >= 2) & (dy < 4)
            out[nariz] = np.array(sombra, np.float32) / 255.0
            dientes = mandibula & (dy == 5) & (dx % 2 == 0)
            out[dientes] *= 0.5
    return np.clip(out, 0, 1)


# ---------------------------------------------------------------------------
# Piezas que no se repiten (puerta, ventanas)
# ---------------------------------------------------------------------------
def puerta(rng, w=32, h=64):
    """Puerta de tablas con marco, dos bisagras de hierro y tirador."""
    wood = madera(rng, size=64, planks=4, dark=(48, 30, 18), light=(118, 78, 46))
    img = wood[:h, :w].copy()
    frame = np.array([40, 26, 16], np.float32) / 255.0
    img[:, :2] = frame
    img[:, -2:] = frame
    img[:2, :] = frame
    iron = np.array([34, 32, 30], np.float32) / 255.0
    for y in (12, h - 16):
        img[y:y + 3, 2:w - 8] = iron
    img[h // 2 - 2:h // 2 + 2, w - 8:w - 5] = np.array([150, 130, 70], np.float32) / 255.0
    return np.clip(img, 0, 1)


def ventana(rng, size=32, lit=False):
    """Ventana de cuatro cristales con marco de madera; 'lit' = luz calida dentro."""
    frame = np.array([66, 44, 26], np.float32) / 255.0
    if lit:
        glow = fbm(rng, (size, size), base=(2, 2), octaves=2)
        glass = tint(0.6 + 0.4 * glow, (170, 110, 40), (255, 214, 120))
    else:
        refl = np.clip(np.add.outer(np.arange(size), np.arange(size)) / (2 * size), 0, 1)
        glass = tint(0.15 + 0.35 * refl, (14, 18, 26), (70, 86, 104))
    img = glass.copy()
    m = size // 2
    img[:3, :] = frame
    img[-3:, :] = frame
    img[:, :3] = frame
    img[:, -3:] = frame
    img[m - 1:m + 1, :] = frame
    img[:, m - 1:m + 1] = frame
    return np.clip(img, 0, 1)


TEXTURAS = {
    # nombre del archivo: (funcion, argumentos)
    "mat_madera": (madera, {}),
    "mat_madera_gris": (madera, {"dark": (54, 50, 44), "light": (138, 128, 112), "vertical": False}),
    "mat_piedra": (piedra, {}),
    "mat_yeso": (yeso, {}),
    "mat_tejas": (tejas, {}),
    "mat_paja": (paja, {}),
    "mat_lona": (lona, {}),
    "mat_metal": (metal, {}),
    "mat_oscuro": (oscuro, {}),
    "mat_agua": (agua, {}),
    "mat_puerta": (puerta, {}),
    "mat_ventana": (ventana, {}),
    "mat_ventana_luz": (ventana, {"lit": True}),
    # subterraneo (cripta y tuneles); van al final para no cambiar la semilla de las demas
    "mat_tierra": (tierra, {}),
    "mat_tierra_suelo": (tierra, {"dark": (30, 22, 16), "light": (96, 72, 50), "raices": False}),
    "mat_roca": (roca, {}),
    "mat_losa": (losas, {}),
    "mat_cripta": (piedra, {"dark": (40, 40, 46), "light": (112, 108, 104), "mortar": (24, 22, 24)}),
    "mat_calaveras": (calaveras, {}),
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(here, "..", "assets", "textures"))
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--preview", action="store_true", help="guarda tambien ampliaciones x6")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    for i, (name, (fn, kwargs)) in enumerate(TEXTURAS.items()):
        rng = np.random.default_rng(args.seed * 1000 + i)
        rgb = fn(rng, **kwargs)
        q = quantize_ordered(rgb, 32, 0.8)
        img = Image.fromarray((np.clip(q, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB")
        path = os.path.join(args.out, name + ".png")
        img.save(path)
        if args.preview:
            big = img.resize((img.width * 6, img.height * 6), Image.NEAREST)
            big.save(os.path.join(args.out, name + "_x6.png"))
        print(f"{path}  {img.width}x{img.height}")


if __name__ == "__main__":
    main()
