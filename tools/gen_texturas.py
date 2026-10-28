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


def hueso(rng, size=32, claro=(214, 202, 172), oscuro=(122, 106, 80)):
    """Hueso viejo: marfil con manchas de tierra y poros."""
    v = fbm(rng, (size, size), base=(4, 4), octaves=3)
    manchas = np.clip((fbm(rng, (size, size), base=(4, 4)) - 0.55) * 3.0, 0, 1)
    rgb = tint(0.55 + 0.45 * v - 0.5 * manchas, oscuro, claro)
    poros = rng.random((size, size)) < 0.05
    rgb[poros] *= 0.75
    return np.clip(rgb, 0, 1)


def cera(rng, size=32, base=(226, 212, 170), sombra=(170, 150, 108)):
    """Cera de vela: crema con chorretones verticales."""
    v = fbm(rng, (size, size), base=(16, 2), octaves=2)   # vetas verticales
    return tint(0.5 + 0.5 * v, sombra, base)


def brillo(rng, size=16):
    """Luz de vela vista a traves de un hueco: naranja que se aclara en el centro."""
    yy, xx = np.mgrid[0:size, 0:size]
    d = np.sqrt((xx - size / 2 + 0.5) ** 2 + (yy - size / 2 + 0.5) ** 2) / (size / 2)
    v = np.clip(1.0 - d, 0, 1) ** 0.7 + 0.1 * fbm(rng, (size, size), base=(4, 4), octaves=2)
    return tint(np.clip(v, 0, 1), (150, 50, 10), (255, 226, 140))


# ---------------------------------------------------------------------------
# Piedras del cementerio: cada objeto con la suya
# ---------------------------------------------------------------------------
def grietas(rng, size, ancho=0.018, base=(4, 4)):
    """Mascara de grietas finas (periodica): donde el ruido pasa cerca de 0.5."""
    return np.abs(fbm(rng, (size, size), base=base, octaves=3) - 0.5) < ancho


def piedra_musgo(rng, size=64):
    """Sillares viejos de la capilla: piedra gris verdosa, musgo que sale de las juntas y de
    abajo, grietas y algun trozo desconchado."""
    rgb = piedra(rng, size, row=16, dark=(62, 62, 56), light=(140, 136, 120), mortar=(38, 40, 32))
    yy = np.arange(size)[:, None] % 16 / 15.0          # 0 arriba de cada fila, 1 abajo
    musgo = fbm(rng, (size, size), base=(8, 8), octaves=3) + 0.35 * yy - 0.25
    m = np.clip((musgo - 0.45) * 4.0, 0, 1)[..., None]
    verde = tint(fbm(rng, (size, size), base=(16, 16), octaves=2), (40, 58, 26), (96, 120, 52))
    rgb = rgb * (1 - m) + verde * m
    rgb[grietas(rng, size, 0.007, (2, 2))] *= 0.45
    for _ in range(4):                                  # desconchones: manchas mas claras
        cy, cx = rng.integers(0, size, 2)
        yy2, xx2 = np.mgrid[0:size, 0:size]
        d = ((yy2 - cy + size // 2) % size - size // 2) ** 2 + ((xx2 - cx + size // 2) % size - size // 2) ** 2
        rgb[d < rng.uniform(4, 12)] *= 1.18
    return np.clip(rgb, 0, 1)


def sillar_viejo(rng, size=64):
    """Bloques grandes y descuidados del panteon: manchas de humedad y chorretones de lluvia."""
    rgb = piedra(rng, size, row=32, dark=(56, 52, 48), light=(126, 118, 104), mortar=(32, 30, 28))
    chorro = fbm(rng, (size, size), base=(1, 16), octaves=2)     # vertical: constante en y
    rgb *= (1.0 - 0.45 * np.clip((chorro - 0.55) * 4, 0, 1))[..., None]
    humedad = np.clip((fbm(rng, (size, size), base=(4, 4)) - 0.5) * 3, 0, 1)[..., None]
    rgb = rgb * (1 - 0.35 * humedad) + np.array([0.18, 0.2, 0.16]) * 0.35 * humedad
    rgb[grietas(rng, size, 0.006, (2, 2))] *= 0.5
    return np.clip(rgb, 0, 1)


def marmol(rng, size=64, claro=(238, 236, 230), oscuro=(196, 194, 190), vetas=(150, 148, 150),
           viejo=False):
    """Marmol blanco con vetas grises. 'viejo': amarillento, sucio y agrietado."""
    v = fbm(rng, (size, size), base=(4, 4), octaves=3)
    rgb = tint(0.6 + 0.4 * v, oscuro, claro)
    veta = np.abs(fbm(rng, (size, size), base=(3, 6), octaves=4) - 0.5) < 0.012
    rgb[veta] = rgb[veta] * 0.55 + np.array(vetas, np.float32) / 255.0 * 0.45
    if viejo:
        rgb *= np.array([0.9, 0.86, 0.76])                     # amarillento
        suciedad = np.clip((fbm(rng, (size, size), base=(4, 4)) - 0.5) * 2.5, 0, 1)[..., None]
        rgb *= 1 - 0.3 * suciedad
        rgb[grietas(rng, size, 0.007, (2, 2))] *= 0.55
    return np.clip(rgb, 0, 1)


def granito(rng, size=32, oscuro=(96, 96, 98), claro=(158, 156, 150), liquen=True, musgo=False):
    """Granito moteado de las lapidas, con liquenes (manchas amarillas y verde palido)."""
    rgb = tint(fbm(rng, (size, size), base=(8, 8), octaves=2) * 0.6 + 0.2, oscuro, claro)
    motas = rng.random((size, size))
    rgb[motas < 0.035] *= 0.7
    rgb[motas > 0.97] *= 1.18
    yy, xx = np.mgrid[0:size, 0:size]
    if liquen:
        for color in [(200, 170, 60)] * 3 + [(150, 160, 110)] * 2:
            cy, cx = rng.integers(0, size, 2)
            r = rng.uniform(1.5, 3.5)
            d = np.sqrt(((yy - cy + size // 2) % size - size // 2) ** 2 + ((xx - cx + size // 2) % size - size // 2) ** 2)
            mancha = (d < r) & (rng.random((size, size)) < 0.8)
            rgb[mancha] = np.array(color, np.float32) / 255.0 * rng.uniform(0.75, 1.0)
    if musgo:
        m = np.clip((fbm(rng, (size, size), base=(4, 4)) - 0.55) * 4, 0, 1)[..., None]
        rgb = rgb * (1 - m) + np.array([0.2, 0.28, 0.12]) * m
    return np.clip(rgb, 0, 1)


def piedra_gastada(rng, size=64, oscuro=(52, 48, 44), claro=(128, 120, 106)):
    """Piedra tallada y gastada por la humedad (la estatua de las catacumbas): grano grueso,
    picaduras, regueros oscuros que bajan y algo de verdin en los huecos."""
    v = 0.6 * fbm(rng, (size, size), base=(4, 4), octaves=4) + 0.4 * fbm(rng, (size, size), base=(16, 16), octaves=2)
    rgb = tint(np.clip((v - 0.15) * 1.3, 0, 1), oscuro, claro)
    reguero = fbm(rng, (size, size), base=(1, 12), octaves=2)          # vertical
    rgb *= (1.0 - 0.4 * np.clip((reguero - 0.55) * 4, 0, 1))[..., None]
    picaduras = rng.random((size, size)) < 0.04
    rgb[picaduras] *= 0.55
    verdin = np.clip((fbm(rng, (size, size), base=(4, 4)) - 0.62) * 4, 0, 1)[..., None]
    rgb = rgb * (1 - 0.6 * verdin) + np.array([0.22, 0.27, 0.16]) * 0.6 * verdin
    rgb[grietas(rng, size, 0.006, (2, 2))] *= 0.5
    return np.clip(rgb, 0, 1)


def hojarasca(rng, size=64, oscuro=(26, 30, 18), claro=(92, 84, 44)):
    """Suelo de bosque: tierra oscura cubierta de hojas secas y agujas de pino."""
    rgb = tint(fbm(rng, (size, size), base=(8, 8), octaves=3) * 0.7, oscuro, claro)
    yy, xx = np.mgrid[0:size, 0:size]
    for _ in range(70):   # hojas: elipses pequenas de tonos otonales, con vuelta en los bordes
        cy, cx = rng.uniform(0, size, 2)
        a = rng.uniform(0, np.pi)
        dx = (xx - cx + size / 2) % size - size / 2
        dy = (yy - cy + size / 2) % size - size / 2
        u = dx * np.cos(a) + dy * np.sin(a)
        v = -dx * np.sin(a) + dy * np.cos(a)
        hoja = (u / rng.uniform(1.5, 2.8)) ** 2 + (v / rng.uniform(0.8, 1.4)) ** 2 < 1
        color = [(110, 70, 30), (130, 96, 40), (80, 56, 30), (96, 92, 44)][int(rng.integers(0, 4))]
        rgb[hoja] = np.array(color, np.float32) / 255.0 * rng.uniform(0.6, 1.0)
    agujas = rng.random((size, size)) < 0.03
    rgb[agujas] = np.array([60, 52, 30], np.float32) / 255.0
    return np.clip(rgb, 0, 1)


def hojas(rng, size=32, oscuro=(18, 40, 16), claro=(92, 140, 52)):
    """Copa de arbol: racimos de hojas (manchas redondas con luz arriba) y huecos oscuros."""
    rgb = tint(fbm(rng, (size, size), base=(4, 4), octaves=2) * 0.35, oscuro, claro)
    yy, xx = np.mgrid[0:size, 0:size]
    for _ in range(26):
        cy, cx = rng.uniform(0, size, 2)
        r = rng.uniform(2.0, 4.0)
        dy = (yy - cy + size / 2) % size - size / 2
        dx = (xx - cx + size / 2) % size - size / 2
        d = np.sqrt(dx * dx + dy * dy)
        racimo = d < r
        luz = np.clip(0.55 - dy / r * 0.35 + rng.uniform(-0.1, 0.15), 0, 1)
        col = tint(luz, oscuro, claro)
        rgb[racimo] = col[racimo]
    return np.clip(rgb, 0, 1)


def agujas(rng, size=32, oscuro=(12, 30, 22), claro=(54, 96, 62)):
    """Ramas de pino: trazos cortos inclinados hacia abajo sobre verde azulado oscuro."""
    rgb = tint(fbm(rng, (size, size), base=(4, 4), octaves=2) * 0.4, oscuro, claro)
    for _ in range(140):
        y, x = rng.integers(0, size, 2)
        t = rng.uniform(0.4, 1.0)
        for k in range(4):
            rgb[(y + k // 2) % size, (x + k) % size] = tint(np.array(t), oscuro, claro)
    return np.clip(rgb, 0, 1)


def corteza(rng, size=32, oscuro=(30, 22, 16), claro=(98, 76, 56)):
    """Corteza: surcos verticales profundos."""
    surcos = fbm(rng, (size, size), base=(2, 8), octaves=3)          # estirado en vertical
    v = np.clip((surcos - 0.3) * 1.8, 0, 1)
    rgb = tint(v, oscuro, claro)
    rgb[np.abs(surcos - 0.5) < 0.03] *= 0.5
    return np.clip(rgb, 0, 1)


def abedul(rng, size=32):
    """Corteza de abedul: blanca con marcas negras horizontales."""
    rgb = tint(0.75 + 0.25 * fbm(rng, (size, size), base=(4, 2), octaves=2), (170, 166, 154), (232, 228, 216))
    for _ in range(9):
        y = int(rng.integers(0, size))
        x0, largo = int(rng.integers(0, size)), int(rng.integers(3, 10))
        for x in range(x0, x0 + largo):
            rgb[y % size, x % size] = (0.12, 0.11, 0.1)
            if rng.random() < 0.5:
                rgb[(y + 1) % size, x % size] = (0.2, 0.18, 0.16)
    return np.clip(rgb, 0, 1)


VIDRIOS = [(30, 50, 150), (150, 20, 34), (90, 30, 120), (200, 150, 40), (30, 110, 90), (60, 80, 180)]


def vidriera(rng, w=32, h=64):
    """Vidriera alargada con arco: rombos de vidrio de colores con emplomado negro, un marco y
    una figura de pie en el centro (un santo sin rostro)."""
    yy, xx = np.mgrid[0:h, 0:w]
    rgb = np.zeros((h, w, 3), np.float32)
    lado = 6
    u, v = (xx + yy) // lado, (xx - yy + 64) // lado          # celdas en rombo
    for a in np.unique(u):
        for b in np.unique(v):
            celda = (u == a) & (v == b)
            col = np.array(VIDRIOS[int(rng.integers(0, len(VIDRIOS)))], np.float32) / 255.0
            rgb[celda] = col * rng.uniform(0.75, 1.15)
    rgb[((xx + yy) % lado == 0) | ((xx - yy) % lado == 0)] = 0.05            # plomo
    # figura: tunica y cabeza claras con aureola dorada
    cx = w / 2
    cuerpo = (np.abs(xx - cx) < 3 + (yy - 26) * 0.12) & (yy > 26) & (yy < 56)
    rgb[cuerpo] = np.array([0.86, 0.84, 0.78]) * (0.85 + 0.15 * ((xx[cuerpo] // 2) % 2))[..., None]
    aureola = np.abs(np.sqrt((xx - cx) ** 2 + (yy - 21) ** 2) - 5) < 1
    rgb[aureola] = (0.95, 0.75, 0.2)
    cabeza = np.sqrt((xx - cx) ** 2 + (yy - 21) ** 2) < 3.5
    rgb[cabeza] = (0.15, 0.1, 0.12)                                         # sin rostro
    # marco de piedra y remate en arco
    arco = (yy < w / 2) & (np.sqrt((xx - cx + 0.5) ** 2 + (yy - w / 2) ** 2) > w / 2 - 1)
    marco = (xx < 2) | (xx >= w - 2) | (yy >= h - 2) | arco
    rgb[marco] = (0.22, 0.21, 0.2)
    return np.clip(rgb, 0, 1)


def roseton(rng, size=64):
    """Roseton: circulo de petalos de vidrio de colores alrededor de un centro rojo."""
    yy, xx = np.mgrid[0:size, 0:size]
    c = size / 2 - 0.5
    r = np.sqrt((xx - c) ** 2 + (yy - c) ** 2) / (size / 2)
    a = np.arctan2(yy - c, xx - c)
    rgb = np.full((size, size, 3), 0.22, np.float32)                         # piedra alrededor
    petalo = (np.cos(a * 8) * 0.18 + 0.72 > r) & (r < 0.92)
    anillo = (r < 0.92) & (r > 0.35)
    colores = np.array(VIDRIOS, np.float32) / 255.0
    sector = ((a + np.pi) / (2 * np.pi) * 16).astype(int) % 16
    rgb[anillo] = colores[sector[anillo] % len(colores)] * 0.9
    rgb[petalo & (r > 0.35)] = colores[(sector[petalo & (r > 0.35)] + 3) % len(colores)]
    rgb[r < 0.35] = (0.7, 0.08, 0.1)
    rgb[np.abs(r - 0.35) < 0.03] = 0.05
    rgb[np.abs(r - 0.92) < 0.04] = 0.05
    radios = (np.abs(np.sin(a * 8)) < 0.08) & (r < 0.92) & (r > 0.35)
    rgb[radios] = 0.05
    return np.clip(rgb, 0, 1)


def plumas(rng, size=32, oscuro=(10, 10, 16), claro=(54, 58, 82)):
    """Plumas negras con brillo azulado: escamas en hileras."""
    rgb = tint(fbm(rng, (size, size), base=(4, 4), octaves=2) * 0.4, oscuro, claro)
    yy, xx = np.mgrid[0:size, 0:size]
    escama = ((yy % 4) == 0) | (((xx + (yy // 4) * 2) % 4) == 0)
    rgb[escama] *= 0.55
    brillo = ((yy % 4) == 1) & (((xx + (yy // 4) * 2) % 4) == 2)
    rgb[brillo] = np.minimum(rgb[brillo] * 1.8, 1)
    return np.clip(rgb, 0, 1)


def liso(rng, size=16, oscuro=(0, 0, 0), claro=(255, 255, 255)):
    """Color casi liso con un poco de grano (petalos, setas, ojos)."""
    return tint(0.4 + 0.6 * fbm(rng, (size, size), base=(4, 4), octaves=2), oscuro, claro)


def hierba(rng, size=32, oscuro=(30, 60, 20), claro=(110, 160, 60)):
    """Briznas: rayas verticales de verdes."""
    v = fbm(rng, (size, size), base=(1, 16), octaves=2)
    return tint(v, oscuro, claro)


def tela_roja(rng, size=32):
    """Tela roja gastada (la estola de la estatua): pliegues verticales."""
    pliegues = 0.5 + 0.5 * np.sin(np.arange(size) / size * 2 * np.pi * 4)[None, :]
    v = 0.55 * pliegues + 0.45 * fbm(rng, (size, size), base=(4, 8), octaves=2)
    return tint(v, (70, 8, 12), (170, 28, 30))


def sangre(rng, size=16):
    """Rojo oscuro y brillante, para las lagrimas de la estatua."""
    return tint(fbm(rng, (size, size), base=(4, 4), octaves=2), (90, 4, 8), (170, 16, 20))


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
    "mat_hueso": (hueso, {}),
    "mat_cera": (cera, {}),
    "mat_brillo": (brillo, {}),
    "mat_piedra_musgo": (piedra_musgo, {}),
    "mat_sillar_viejo": (sillar_viejo, {}),
    "mat_marmol": (marmol, {}),
    "mat_marmol_viejo": (marmol, {"viejo": True}),
    "mat_lapida": (granito, {}),
    "mat_granito": (granito, {"oscuro": (52, 52, 56), "claro": (98, 96, 96), "liquen": False,
                              "musgo": True}),
    "mat_tela_roja": (tela_roja, {}),
    "mat_sangre": (sangre, {}),
    "mat_piedra_gastada": (piedra_gastada, {}),
    "mat_hojarasca": (hojarasca, {}),
    "mat_hojas": (hojas, {}),
    "mat_hojas_claras": (hojas, {"oscuro": (40, 60, 18), "claro": (150, 176, 70)}),
    "mat_pino": (agujas, {}),
    "mat_corteza": (corteza, {}),
    "mat_abedul": (abedul, {}),
    "mat_vidriera": (vidriera, {}),
    "mat_roseton": (roseton, {}),
    # detalles 3D que sustituyen a los sprites
    "mat_plumas": (plumas, {}),
    "mat_seta": (liso, {"oscuro": (70, 140, 180), "claro": (190, 240, 255)}),
    "mat_hierba": (hierba, {}),
    "mat_flor_roja": (liso, {"oscuro": (120, 20, 24), "claro": (230, 70, 60)}),
    "mat_flor_amarilla": (liso, {"oscuro": (170, 130, 30), "claro": (250, 220, 90)}),
    "mat_flor_blanca": (liso, {"oscuro": (170, 170, 180), "claro": (245, 245, 250)}),
    "mat_flor_morada": (liso, {"oscuro": (80, 40, 120), "claro": (170, 110, 210)}),
    "mat_flor_marchita": (liso, {"oscuro": (60, 24, 24), "claro": (130, 84, 64)}),
    "mat_ojo": (liso, {"oscuro": (170, 20, 16), "claro": (255, 120, 80)}),
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
