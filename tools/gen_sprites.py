#!/usr/bin/env python3
"""
gen_sprites: genera los sprites (billboards) de vegetacion y detalles: arbol seco, cuervo,
velas, hierba alta, flores y setas luminosas.

Pixel art procedural y determinista: mismo resultado con la misma semilla. Fondo transparente
con alfa duro (el motor recorta por alfa) y el mismo acabado que las texturas: 32 niveles por
canal y trama Bayer (quantize_ordered de texconv.py).

Uso:
  python tools/gen_sprites.py                       # escribe en assets/textures/spr_*.png
  python tools/gen_sprites.py --out carpeta --preview

Requiere:  pip install pillow numpy
"""
import argparse
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texconv import quantize_ordered  # noqa: E402


class Lienzo:
    """Imagen RGBA en flotantes (0..1) con primitivas de pixel art."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.rgb = np.zeros((h, w, 3), np.float32)
        self.a = np.zeros((h, w), np.float32)

    def punto(self, x, y, color):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.rgb[y, x] = np.array(color, np.float32) / 255.0
            self.a[y, x] = 1.0

    def disco(self, cx, cy, r, color):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.punto(x, y, color)

    def elipse(self, cx, cy, rx, ry, color):
        for y in range(int(cy - ry - 1), int(cy + ry + 2)):
            for x in range(int(cx - rx - 1), int(cx + rx + 2)):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.punto(x, y, color)

    def linea(self, x0, y0, x1, y1, grosor, color):
        pasos = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(pasos + 1):
            t = i / pasos
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            g = grosor[0] + (grosor[1] - grosor[0]) * t if isinstance(grosor, tuple) else grosor
            if g <= 1.0:
                self.punto(x, y, color)
            else:
                self.disco(x, y, g / 2, color)

    def rect(self, x0, y0, x1, y1, color):
        for y in range(int(y0), int(y1)):
            for x in range(int(x0), int(x1)):
                self.punto(x, y, color)

    def contorno(self, color, solo_abajo_derecha=False):
        """Borde oscuro de 1 px alrededor de lo pintado (lectura clara a baja resolucion)."""
        lleno = self.a > 0.5
        borde = np.zeros_like(lleno)
        vecinos = [(1, 0), (0, 1)] if solo_abajo_derecha else [(1, 0), (-1, 0), (0, 1), (0, -1)]
        alto, ancho = lleno.shape
        for dy, dx in vecinos:
            # desplazamiento SIN dar la vuelta: lo que sale por un borde no entra por el otro
            mov = np.zeros_like(lleno)
            mov[max(dy, 0):alto + min(dy, 0), max(dx, 0):ancho + min(dx, 0)] =                 lleno[max(-dy, 0):alto + min(-dy, 0), max(-dx, 0):ancho + min(-dx, 0)]
            borde |= mov
        borde &= ~lleno
        self.rgb[borde] = np.array(color, np.float32) / 255.0
        self.a[borde] = 1.0

    def imagen(self, dither=0.6):
        q = quantize_ordered(self.rgb, 32, dither)
        rgba = np.zeros((self.h, self.w, 4), np.uint8)
        rgba[..., :3] = (np.clip(q, 0, 1) * 255 + 0.5).astype(np.uint8)
        rgba[..., 3] = np.where(self.a > 0.5, 255, 0)
        return Image.fromarray(rgba, "RGBA")


def mezcla(c0, c1, t):
    return tuple(int(a + (b - a) * t) for a, b in zip(c0, c1))


# ---------------------------------------------------------------------------
def arbol_seco(rng):
    """Arbol sin hojas, retorcido: tronco y ramas que se bifurcan."""
    L = Lienzo(64, 96)
    oscuro, claro = (34, 28, 26), (92, 80, 70)

    def rama(x, y, ang, largo, grosor, nivel):
        if nivel == 0 or largo < 3:
            return
        segs = 3
        for _ in range(segs):
            # sin bajar de la horizontal, y sin salirse del lienzo
            ang = max(-1.35, min(1.35, ang + rng.uniform(-0.25, 0.25)))
            x1 = x + math.sin(ang) * largo / segs
            y1 = y - math.cos(ang) * largo / segs
            if not (3 <= x1 <= L.w - 4 and y1 >= 3):
                return
            L.linea(x, y, x1, y1, (grosor, grosor * 0.8), mezcla(oscuro, claro, rng.uniform(0.2, 0.6)))
            x, y, grosor = x1, y1, grosor * 0.8
        hijos = 2 if nivel > 3 else 3
        for _ in range(hijos):
            rama(x, y, ang + rng.uniform(-0.8, 0.8), largo * rng.uniform(0.5, 0.68),
                 max(grosor * 0.7, 1.0), nivel - 1)

    # raices y tronco
    for dx in (-6, 6):
        L.linea(32, 92, 32 + dx, 95, (3, 1), oscuro)
    rama(32, 95, rng.uniform(-0.08, 0.08), 40, 7.0, 6)
    L.contorno((14, 12, 12))
    return L


def cuervo(rng):
    """Cuervo posado, de perfil (mira a la izquierda)."""
    arte = [
        "................",
        "................",
        "......####......",
        ".....#####......",
        "...o######......",
        "..>#######......",
        ".....######.....",
        ".....########...",
        "....##########..",
        "....###########.",
        ".....#########..",
        "......#######...",
        ".......#.#......",
        ".......#.#......",
        "......##.##.....",
        "................",
    ]
    L = Lienzo(32, 32)
    for y, fila in enumerate(arte):
        for x, c in enumerate(fila):
            color = {"#": (22, 22, 30), "o": (210, 200, 120), ">": (60, 56, 50)}.get(c)
            if color:
                for dy in (0, 1):
                    for dx in (0, 1):
                        L.punto(x * 2 + dx, y * 2 + dy, color)
    # brillo azulado en el lomo
    for x in range(12, 22):
        L.punto(x, 12 + (x - 12) // 4, (60, 66, 96))
    return L


def velas(rng):
    """Grupo de tres velas con llama: para usar con "emissive": true y una luz."""
    L = Lienzo(32, 32)
    L.elipse(16, 29, 12, 2.5, (40, 36, 34))  # platillo
    for cx, alto in ((9, 10), (16, 16), (23, 7)):
        cera = (222, 210, 168)
        L.rect(cx - 2, 28 - alto, cx + 3, 28, cera)
        L.rect(cx + 1, 28 - alto, cx + 3, 28, (186, 172, 132))   # sombra
        L.punto(cx - 2, 28 - alto + 2, cera)
        L.punto(cx - 3, 28 - alto + 3, cera)                       # goteo
        L.punto(cx, 27 - alto, (30, 26, 22))                        # mecha
        L.elipse(cx, 24 - alto, 1.6, 3.0, (255, 150, 40))
        L.elipse(cx, 25 - alto, 0.8, 1.6, (255, 236, 160))
    return L


def hierba_alta(rng):
    L = Lienzo(32, 32)
    verdes = [(42, 70, 30), (64, 98, 40), (94, 128, 52), (124, 150, 70)]
    for _ in range(26):
        x0 = rng.uniform(4, 28)
        alto = rng.uniform(10, 30)
        curva = rng.uniform(-6, 6)
        color = verdes[int(rng.integers(0, len(verdes)))]
        L.linea(x0, 31, x0 + curva, 31 - alto, (2.0, 1.0), color)
    return L


def flores(rng):
    L = hierba_alta(rng)
    for _ in range(6):
        x = rng.uniform(5, 27)
        y = rng.uniform(6, 18)
        color = [(214, 60, 60), (230, 200, 70), (226, 226, 236), (150, 90, 200)][int(rng.integers(0, 4))]
        L.linea(x, 31, x + rng.uniform(-2, 2), y, 1, (52, 84, 36))
        L.disco(x, y, 1.6, color)
        L.punto(x, y, (250, 230, 120))
    return L


def setas(rng):
    """Setas de sombrero azul palido: con "emissive": true brillan de noche."""
    L = Lienzo(32, 32)
    for cx, h, r in ((10, 12, 6), (21, 16, 7), (27, 7, 4)):
        L.rect(cx - 1.5, 31 - h, cx + 1.5, 31, (206, 200, 186))
        L.elipse(cx, 31 - h, r, r * 0.55, (110, 190, 230))
        L.elipse(cx - r * 0.25, 31 - h - r * 0.15, r * 0.55, r * 0.25, (170, 230, 250))
        for _ in range(3):
            L.punto(cx + rng.uniform(-r + 1.5, r - 1.5), 31 - h + rng.uniform(-r * 0.4, 0),
                    (235, 250, 255))
    L.contorno((26, 34, 44))
    return L


# ---------------------------------------------------------------------------
# Subterraneo (cripta y tuneles)
# ---------------------------------------------------------------------------
def cristales(rng):
    """Racimo de cristales que brillan (turquesa): con "emissive": true y una luz fria."""
    L = Lienzo(32, 48)
    L.elipse(16, 46, 13, 2.5, (36, 34, 40))  # roca de la base
    piezas = [(16, 0.0, 34, 4.5), (9, -0.45, 22, 3.5), (23, 0.4, 26, 3.8), (5, -0.8, 12, 2.6),
              (27, 0.75, 14, 2.6), (13, -0.15, 16, 2.8), (20, 0.2, 18, 3.0)]
    for x0, ang, largo, ancho in piezas:
        dx, dy = math.sin(ang), -math.cos(ang)
        px, py = -dy, dx  # perpendicular
        punta = (x0 + dx * largo, 45 + dy * largo)
        for t in np.linspace(0, 1, int(largo * 2)):
            cx, cy = x0 + dx * largo * t, 45 + dy * largo * t
            w = ancho * (1.0 if t < 0.75 else (1 - t) / 0.25)
            for s in np.linspace(-w, w, int(w * 4) + 1):
                lado = (s + w) / (2 * w + 1e-6)          # 0 izquierda .. 1 derecha
                color = mezcla((40, 150, 160), (150, 250, 240), 1 - lado * 0.8)
                if abs(s) < 0.6:
                    color = (210, 255, 250)              # arista brillante
                L.punto(cx + px * s, cy + py * s, color)
        L.punto(*punta, (240, 255, 255))
    L.contorno((14, 40, 48))
    return L


def telarana(rng):
    """Telarana en una esquina (arriba a la izquierda): radios y espiral, gris palido."""
    L = Lienzo(48, 48)
    color = (196, 196, 200)
    angulos = [i * (math.pi / 2) / 6 for i in range(7)]
    for a in angulos:
        L.linea(0, 0, 47 * math.cos(a), 47 * math.sin(a), 1, color)
    for r in range(8, 46, 6):
        prev = None
        for a in angulos:
            # la espiral cuelga un poco entre radio y radio
            p = (r * math.cos(a), r * math.sin(a))
            if prev:
                medio = ((prev[0] + p[0]) / 2 * 0.92, (prev[1] + p[1]) / 2 * 0.92)
                L.linea(*prev, *medio, 1, color)
                L.linea(*medio, *p, 1, color)
            prev = p
    return L


def huesos(rng):
    """Monton de huesos con dos calaveras."""
    L = Lienzo(48, 32)
    hueso, sombra = (196, 182, 148), (120, 106, 84)
    for _ in range(16):
        # extremos dentro del lienzo, amontonados hacia el centro y abajo
        x0, y0 = rng.uniform(4, 30), rng.uniform(17, 29)
        ang = rng.uniform(-0.5, 0.5)
        largo = rng.uniform(10, 16)
        x1 = min(x0 + math.cos(ang) * largo, 44)
        y1 = min(max(y0 + math.sin(ang) * largo * 0.5, 14), 29)
        L.linea(x0, y0, x1, y1, 3, mezcla(sombra, hueso, rng.uniform(0.4, 1.0)))
        L.disco(x0, y0, 2.0, hueso)
        L.disco(x1, y1, 2.0, hueso)
    for cx, cy in ((17, 18), (31, 21)):
        L.elipse(cx, cy, 6, 5.5, hueso)
        L.rect(cx - 3, cy + 3, cx + 4, cy + 7, hueso)
        L.elipse(cx + 2, cy - 2, 3, 2, (226, 214, 184))
        for ox in (-2.5, 2.5):
            L.elipse(cx + ox, cy + 0.5, 1.6, 1.8, (26, 20, 18))
        L.punto(cx, cy + 3, (26, 20, 18))
        for x in range(-2, 4, 2):
            L.punto(cx + x, cy + 5, sombra)
    L.contorno((30, 24, 20))
    return L


def raices(rng):
    """Raices que cuelgan del techo (la imagen se coloca con su borde de arriba en el techo)."""
    L = Lienzo(32, 48)
    for _ in range(9):
        x = rng.uniform(4, 28)
        largo = rng.uniform(16, 46)
        y, g = 0.0, rng.uniform(1.2, 2.4)
        color = mezcla((44, 32, 22), (104, 80, 54), rng.uniform(0, 1))
        while y < largo:
            x1 = x + rng.uniform(-1.5, 1.5)
            y1 = y + 3
            L.linea(x, y, x1, y1, (g, g * 0.9), color)
            x, y, g = x1, y1, max(g * 0.93, 1.0)
            if rng.random() < 0.12:  # raicilla
                L.linea(x, y, x + rng.uniform(-5, 5), y + rng.uniform(3, 7), 1, color)
    L.contorno((18, 12, 8))
    return L


def antorcha(rng):
    """Antorcha de pared: palo de madera, cabeza de trapo y llama (emissive + luz)."""
    L = Lienzo(16, 48)
    L.linea(8, 47, 8, 24, (3, 2.4), (86, 58, 34))
    L.rect(5, 20, 11, 26, (62, 46, 32))            # trapo
    L.rect(5, 22, 11, 23, (40, 30, 22))
    L.elipse(8, 13, 4.5, 9, (220, 80, 20))
    L.elipse(8, 15, 3.0, 6, (255, 160, 40))
    L.elipse(8, 17, 1.6, 3.5, (255, 240, 170))
    for _ in range(4):
        L.punto(8 + rng.uniform(-4, 4), rng.uniform(1, 6), (255, 190, 80))  # chispas
    return L


def ojos(rng):
    """Dos ojos rojizos en la oscuridad (emissive): algo que mira desde un rincon."""
    L = Lienzo(32, 16)
    for cx in (9, 23):
        L.elipse(cx, 8, 4.5, 2.6, (150, 20, 16))
        L.elipse(cx, 8, 2.8, 1.6, (240, 70, 40))
        L.punto(cx, 8, (255, 220, 160))
    return L


SPRITES = {
    "spr_arbol_seco": arbol_seco,
    "spr_cuervo": cuervo,
    "spr_velas": velas,
    "spr_hierba": hierba_alta,
    "spr_flores": flores,
    "spr_setas": setas,
    # nuevos al final: no cambian la semilla de los anteriores
    "spr_cristales": cristales,
    "spr_telarana": telarana,
    "spr_huesos": huesos,
    "spr_raices": raices,
    "spr_antorcha": antorcha,
    "spr_ojos": ojos,
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(here, "..", "assets", "textures"))
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--preview", action="store_true", help="guarda tambien ampliaciones x6")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for i, (nombre, fn) in enumerate(SPRITES.items()):
        rng = np.random.default_rng(args.seed * 1000 + i)
        img = fn(rng).imagen()
        ruta = os.path.join(args.out, nombre + ".png")
        img.save(ruta)
        if args.preview:
            img.resize((img.width * 6, img.height * 6), Image.NEAREST).save(
                os.path.join(args.out, nombre + "_x6.png"))
        print(f"{ruta}  {img.width}x{img.height}")


if __name__ == "__main__":
    main()
