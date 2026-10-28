#!/usr/bin/env python3
"""
gen_sprites: genera los sprites (billboards) de vegetacion y detalles: arbol seco, cuervo,
velas, hierba alta, flores y setas luminosas.

Pixel art procedural y determinista: mismo resultado con la misma semilla. Fondo transparente
con alfa duro (el motor recorta por alfa) y el mismo acabado que las texturas: 32 niveles por
canal y trama Bayer (quantize_ordered de texconv.py).

Los ANIMADOS (llama, velas) devuelven una lista de fotogramas y se guardan como una tira
horizontal: en la escena se usan con "frames": 4 (y "fps", por defecto 8). spr_luciernagas
es un enjambre de luciernagas (emissive) para el lago.

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


def llama_en(L, cx, base, alto, ancho, inclina, nucleo=0.45, chispa=None):
    """Llama de pixel art con la base en (cx, base): fila a fila se estrecha hacia arriba y la
    punta se va hacia un lado ('inclina', en pixeles). Rojo fuera, naranja y nucleo claro."""
    for k in range(int(alto) + 1):
        t = k / max(alto, 1)                       # 0 abajo .. 1 punta
        w = ancho * (1 - t) ** 0.7 * (0.75 + 0.6 * math.sin(math.pi * min(t * 1.6, 1.0)))
        x = cx + inclina * t * t
        y = base - k
        centro = int(math.floor(x + 0.5))       # columna entera: sin huecos por redondeo
        for px in range(centro - int(w + 0.5), centro + int(w + 0.5) + 1):
            borde = abs(px - x) / max(w, 0.5)
            if borde > 1 and px != centro:
                continue
            if t < nucleo and borde < 0.45:
                color = (255, 240, 170)
            elif borde < 0.75 and t < 0.8:
                color = (255, 160, 40)
            else:
                color = (226, 70, 20)
            L.punto(px, y, color)
    if chispa:
        L.punto(*chispa, (255, 200, 90))


# Variacion de cada fotograma: (escala de alto, inclinacion, tamano del nucleo)
FASES_LLAMA = [(1.0, 0.0, 0.45), (0.82, -1.4, 0.35), (1.12, 0.9, 0.5), (0.9, 1.6, 0.4)]


def velas(rng):
    """Grupo de tres velas con llama animada (4 fotogramas): con "emissive": true y una luz."""
    fotogramas = []
    desfase = [0, 2, 1]                            # cada vela va a su ritmo
    for f in range(len(FASES_LLAMA)):
        L = Lienzo(32, 32)
        L.elipse(16, 29, 12, 2.5, (40, 36, 34))   # platillo
        for v, (cx, alto) in enumerate(((9, 10), (16, 16), (23, 7))):
            cera = (222, 210, 168)
            L.rect(cx - 2, 28 - alto, cx + 3, 28, cera)
            L.rect(cx + 1, 28 - alto, cx + 3, 28, (186, 172, 132))   # sombra
            L.punto(cx - 2, 28 - alto + 2, cera)
            L.punto(cx - 3, 28 - alto + 3, cera)                       # goteo
            L.punto(cx, 27 - alto, (30, 26, 22))                        # mecha
            esc, inc, nuc = FASES_LLAMA[(f + desfase[v]) % len(FASES_LLAMA)]
            llama_en(L, cx, 26 - alto, 6 * esc, 1.6, inc * 0.6, nuc)
        fotogramas.append(L)
    return fotogramas


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
    """Media telarana (textura de un modelo tendido en una esquina): radios que salen del centro
    de arriba y una espiral que cuelga entre ellos, gris palido sobre transparente."""
    L = Lienzo(64, 32)
    color = (196, 196, 200)
    cx = 31.5
    angulos = [math.pi * i / 8 for i in range(9)]           # de 0 (derecha) a pi (izquierda)
    for a in angulos:
        L.linea(cx, 0, cx + 34 * math.cos(a), 34 * math.sin(a), 1, color)
    for r in range(5, 33, 5):
        prev = None
        for a in angulos:
            p = (cx + r * math.cos(a), r * math.sin(a))
            if prev:
                medio = ((prev[0] + p[0]) / 2, (prev[1] + p[1]) / 2 * 0.9 + r * 0.1)
                L.linea(*prev, *medio, 1, color)
                L.linea(*medio, *p, 1, color)
            prev = p
    for _ in range(3):                                      # hilos rotos que cuelgan
        x = rng.uniform(8, 56)
        L.linea(x, rng.uniform(4, 20), x + rng.uniform(-2, 2), 31, 1, color)
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


def llama(rng):
    """Llama sola (emissive), 4 fotogramas de 8x16: encima de una vela o una antorcha de modelo.
    Cambia de alto, se inclina a un lado y al otro, y a veces suelta una chispa."""
    fotogramas = []
    for f, (esc, inc, nuc) in enumerate(FASES_LLAMA):
        L = Lienzo(8, 16)
        chispa = (4 + int(rng.integers(-2, 3)), int(rng.integers(0, 3))) if f % 2 == 1 else None
        llama_en(L, 3.5, 15, 12 * esc, 2.6, inc, nuc, chispa)
        fotogramas.append(L)
    return fotogramas


def flores_marchitas(rng):
    """Ramo marchito sobre una tumba: tallos secos doblados, cabezas caidas de color apagado y
    petalos sueltos en el suelo, atado con una cinta descolorida."""
    L = Lienzo(32, 32)
    # tallos en abanico desde el atado; arriba se doblan y la flor cuelga hacia fuera
    for dx in (-10, -6, -2, 2, 6, 10, -4, 4):
        color = mezcla((58, 56, 36), (100, 86, 52), rng.uniform(0, 1))
        alto = rng.uniform(15, 25)
        xm, ym = 16 + dx * 0.9, 30 - alto
        lado = 1 if dx > 0 else -1
        xf, yf = xm + lado * rng.uniform(2, 4), ym + rng.uniform(3, 6)
        L.linea(16 + dx * 0.1, 30, xm, ym, 1, color)
        L.linea(xm, ym, xf, yf, 1, color)
        petalo = mezcla((96, 26, 32), (150, 100, 72), rng.uniform(0, 1))
        L.disco(xf, yf + 1.5, 2.3, petalo)
        L.punto(xf, yf + 3, mezcla(petalo, (40, 20, 20), 0.5))
        if rng.random() < 0.5:   # hoja seca
            L.linea(xm * 0.5 + 8, (ym + 30) / 2, xm * 0.5 + 8 + lado * 3, (ym + 30) / 2 + 2, 1, (88, 74, 40))
    for _ in range(5):  # petalos caidos
        L.punto(rng.uniform(6, 26), rng.uniform(29, 31), (110, 50, 46))
    L.rect(14, 26, 19, 29, (120, 104, 120))   # cinta
    L.contorno((24, 20, 18), solo_abajo_derecha=True)
    return L


def luciernagas(rng):
    """Enjambre de luciernagas (emissive), 4 fotogramas de 32x32: cinco o seis puntos de luz
    verde amarillenta que derivan despacio y se encienden y apagan cada uno a su ritmo."""
    bichos = [(rng.uniform(4, 28), rng.uniform(4, 28), rng.uniform(0, 2 * math.pi),
               rng.uniform(0.6, 1.6)) for _ in range(6)]
    fotogramas = []
    for f in range(4):
        L = Lienzo(32, 32)
        for x0, y0, fase, vel in bichos:
            a = fase + f * vel * math.pi / 2
            x, y = x0 + 1.5 * math.cos(a), y0 + 1.2 * math.sin(a * 1.3)
            brillo = 0.5 + 0.5 * math.sin(a)
            if brillo < 0.2:
                continue                                  # apagada en este fotograma
            L.punto(x, y, (int(200 + 55 * brillo), 255, int(110 * brillo)))
            if brillo > 0.6:                              # halo de las mas encendidas
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    L.punto(x + dx, y + dy, (120, 170, 40))
        fotogramas.append(L)
    return fotogramas


def sol(rng):
    """Sol de atardecer (emissive), 64x64: disco anaranjado, mas claro en el centro, y un halo
    tramado alrededor. Va en el panorama de la escena ("sun"), sin niebla."""
    L = Lienzo(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5)
            if d < 17:
                t = d / 17
                L.punto(x, y, (255, int(232 - 70 * t), int(150 - 90 * t)))
            elif d < 30 and (x + y) % 2 == 0 and rng.random() < 0.9 * (1 - (d - 17) / 13):
                L.punto(x, y, (255, 168, 110))                 # halo tramado
    return L


def luna(rng):
    """Luna llena (va en el panorama de las escenas de noche, sin niebla), 64x64: disco palido
    con mares grises y un halo azulado tramado."""
    L = Lienzo(64, 64)
    mares = [(rng.uniform(22, 42), rng.uniform(22, 42), rng.uniform(3, 6)) for _ in range(6)]
    for y in range(64):
        for x in range(64):
            d = math.hypot(x - 31.5, y - 31.5)
            if d < 15:
                c = (232, 232, 218)
                if any(math.hypot(x - mx, y - my) < mr for mx, my, mr in mares):
                    c = (178, 182, 176)
                L.punto(x, y, c)
            elif d < 26 and (x + y) % 2 == 0 and rng.random() < 0.8 * (1 - (d - 15) / 11):
                L.punto(x, y, (150, 170, 210))
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
    "spr_llama": llama,
    "spr_flores_marchitas": flores_marchitas,
    "spr_luciernagas": luciernagas,
    "spr_sol": sol,
    "spr_luna": luna,
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
        hecho = fn(rng)
        if isinstance(hecho, list):          # animado: fotogramas en una tira horizontal
            cuadros = [c.imagen() for c in hecho]
            img = Image.new("RGBA", (sum(c.width for c in cuadros), cuadros[0].height))
            for k, c in enumerate(cuadros):
                img.paste(c, (k * c.width, 0))
        else:
            img = hecho.imagen()
        ruta = os.path.join(args.out, nombre + ".png")
        img.save(ruta)
        if args.preview:
            img.resize((img.width * 6, img.height * 6), Image.NEAREST).save(
                os.path.join(args.out, nombre + "_x6.png"))
        print(f"{ruta}  {img.width}x{img.height}")


if __name__ == "__main__":
    main()
