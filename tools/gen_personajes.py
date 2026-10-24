#!/usr/bin/env python3
"""
gen_personajes: pinta en pixel art el atlas completo (128x128) de cada personaje: la cara, el pelo
y la ropa. Misma distribucion que persona_m / persona_f y charatlas.py, asi que vale para todos
los cuerpos de tools/blender/personajes.py. Se elige en la escena con "texture".

Las caras son inventadas (no son de nadie): se componen con parametros (tono de piel, ojos,
cejas, nariz, boca, peinado, barba, arrugas, pecas, ojeras...) y el mismo acabado PS1 que el resto
de texturas (32 niveles por canal y trama Bayer).

Uso:
  python tools/gen_personajes.py                  # escribe assets/textures/pj_<nombre>.png
  python tools/gen_personajes.py --out carpeta --preview

Requiere:  pip install pillow numpy
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texconv import quantize_ordered  # noqa: E402

# Regiones del atlas (x0, y0, x1, y1), como en charatlas.py.
R = dict(FACE=(0, 0, 48, 56), HEADBACK=(48, 0, 96, 56), HAIR=(96, 0, 128, 32),
         SKIN=(96, 32, 128, 48), TORSO=(0, 56, 96, 96), SLEEVE=(96, 48, 128, 80),
         FOREARM=(96, 80, 128, 112), PANTS=(0, 96, 64, 128), SHOES=(64, 96, 96, 128))


def col(c):
    return np.array(c, np.float32) / 255.0


def mezcla(a, b, t):
    return a * (1 - t) + b * t


class Lienzo:
    """Una region del atlas: RGB en flotantes, con primitivas sencillas."""

    def __init__(self, w, h, color):
        self.w, self.h = w, h
        self.img = np.ones((h, w, 3), np.float32) * col(color)

    def px(self, x, y, c, t=1.0):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.img[y, x] = mezcla(self.img[y, x], c, t)

    def rect(self, x0, y0, x1, y1, c, t=1.0):
        for y in range(int(y0), int(y1)):
            for x in range(int(x0), int(x1)):
                self.px(x, y, c, t)

    def disco(self, cx, cy, r, c, t=1.0):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
                if d <= r:
                    self.px(x, y, c, t * min(1.0, (r - d + 0.5)))

    def ruido(self, rng, fuerza):
        n = rng.standard_normal((self.h, self.w, 1)).astype(np.float32) * fuerza
        self.img = np.clip(self.img * (1 + n), 0, 1)

    def vetas(self, rng, fuerza, vertical=True):
        """Mechones o tejido: ruido estirado en una direccion."""
        if vertical:
            n = rng.standard_normal((1, self.w, 1)).astype(np.float32)
            n = np.repeat(n, self.h, axis=0)
        else:
            n = rng.standard_normal((self.h, 1, 1)).astype(np.float32)
            n = np.repeat(n, self.w, axis=1)
        self.img = np.clip(self.img * (1 + n * fuerza), 0, 1)


# ---------------------------------------------------------------------------
# Cara (48x56): cuerpo de la cabeza visto de frente. Los ojos van al ~43 % desde arriba.
# ---------------------------------------------------------------------------
def cara(p, rng):
    piel = col(p["piel"])
    pelo = col(p["pelo"])
    L = Lienzo(48, 56, p["piel"])
    yy, xx = np.mgrid[0:56, 0:48].astype(np.float32)
    # volumen: los lados y la barbilla mas oscuros, el centro de la frente y la nariz mas claros
    lado = ((xx - 23.5) / 24.0) ** 2
    L.img *= (1.08 - 0.32 * lado)[..., None]
    L.img *= (1.0 - 0.12 * np.clip((yy - 46) / 10, 0, 1))[..., None]
    L.ruido(rng, 0.025)

    ey, exs = 24, (15, 32)
    nino = p.get("nino", False)
    viejo = p.get("viejo", False)

    if p.get("rubor"):
        for ex in exs:
            L.disco(ex - (2 if ex < 24 else -2), 33, 3.2, col((214, 120, 120)), 0.28)
    if p.get("pecas"):
        for _ in range(16):
            x = rng.uniform(9, 39)
            y = rng.uniform(28, 35)
            if abs(x - 23.5) > 3:
                L.px(x, y, piel * 0.72, 0.8)
    if p.get("ojeras"):
        for ex in exs:
            L.rect(ex - 3, ey + 2, ex + 4, ey + 4, col((92, 70, 96)), 0.35)

    # cejas
    ceja = pelo * 0.75 if not viejo else mezcla(pelo, col((240, 240, 240)), 0.2)
    grosor = p.get("cejas", 1)
    for ex in exs:
        for i in range(-3, 4):
            arco = -1 if abs(i) <= 1 else 0
            for g in range(grosor):
                L.px(ex + i, ey - 4 + arco + g, ceja, 0.95)
    # ojos
    iris = col(p.get("ojos", (70, 46, 30)))
    alto = 3 if nino else 2
    for ex in exs:
        L.rect(ex - 2, ey, ex + 3, ey + alto, col((228, 222, 210)))
        L.rect(ex, ey, ex + 2, ey + alto, iris)
        L.px(ex, ey, iris * 0.45)
        L.px(ex + 1, ey, col((250, 250, 250)), 0.6)       # brillo
        L.rect(ex - 3, ey - 1, ex + 4, ey, piel * 0.38)    # parpado
        if viejo:
            L.rect(ex - 2, ey + alto, ex + 3, ey + alto + 1, piel * 0.82)
            L.px(ex + 4, ey, piel * 0.75)                  # patas de gallo
            L.px(ex - 4, ey + 1, piel * 0.75)
    if p.get("gafas"):
        marco = col((40, 36, 34))
        for ex in exs:
            L.rect(ex - 4, ey - 2, ex + 5, ey - 1, marco)
            L.rect(ex - 4, ey + 3, ex + 5, ey + 4, marco)
            L.rect(ex - 4, ey - 2, ex - 3, ey + 4, marco)
            L.rect(ex + 4, ey - 2, ex + 5, ey + 4, marco)
        L.rect(19, ey - 1, 29, ey, marco)

    # nariz: sombra a un lado, punta clara y aletas
    for y in range(ey + 1, 33):
        L.px(22, y, piel * 0.8, 0.8)
    L.rect(23, 31, 26, 33, mezcla(piel, col((255, 240, 230)), 0.2))
    L.px(21, 33, piel * 0.55)
    L.px(26, 33, piel * 0.55)
    L.rect(22, 34, 26, 35, piel * 0.8)

    # boca
    my = 40
    labios = piel * col((210, 150, 150)) * 1.05
    ancho = p.get("boca", 4)
    L.rect(24 - ancho, my, 24 + ancho, my + 1, labios * 0.55)
    L.rect(25 - ancho, my + 1, 23 + ancho, my + 2, labios)
    if p.get("sonrisa"):
        L.px(24 - ancho - 1, my - 1, labios * 0.55)
        L.px(23 + ancho + 1, my - 1, labios * 0.55)
    if viejo:  # surcos de la nariz a la boca y frente
        for i in range(6):
            L.px(18 - i * 0.3, 35 + i, piel * 0.78)
            L.px(29 + i * 0.3, 35 + i, piel * 0.78)
        for y in (13, 16):
            L.rect(14, y, 34, y + 1, piel * 0.86)

    # barba, bigote y barba de pocos dias
    if p.get("sombra_barba"):
        # sombra continua (no motas): la zona de la barba algo mas oscura y verdosa
        for y in range(35, 56):
            margen = 6 + max(0, 37 - y) * 2
            for x in range(margen, 48 - margen):
                if not (19 <= x <= 28 and my - 1 <= y <= my + 2):
                    L.px(x, y, mezcla(piel, pelo, 0.5), 0.22 + 0.06 * rng.random())
    barba = p.get("barba")
    color_barba = col(p.get("color_barba", p["pelo"]))
    if barba in ("bigote", "completa"):
        L.rect(18, my - 3, 30, my - 1, color_barba)
        L.px(17, my - 1, color_barba)
        L.px(30, my - 1, color_barba)
    if barba == "completa":
        for y in range(35, 56):
            margen = 4 + max(0, 37 - y) * 2
            for x in range(margen, 48 - margen):
                if 19 <= x <= 28 and my - 1 <= y <= my + 2:
                    continue
                L.px(x, y, color_barba * rng.uniform(0.92, 1.04))

    # pelo: flequillo, entradas o melena enmarcando la cara
    peinado = p.get("peinado", "corto")
    mech = Lienzo(48, 56, p["pelo"])
    mech.vetas(rng, 0.18)
    mech.ruido(rng, 0.05)

    def pelo_en(x, y):
        L.img[y, x] = mech.img[y, x]

    for x in range(48):
        borde = {"corto": 10 + 1.5 * abs(x - 23.5) / 24,
                 "raya": 9 + (3 if x < 18 else 0) + abs(x - 23.5) / 18,
                 "despeinado": 12 + rng.integers(-2, 3),
                 "largo": 11 + abs(x - 23.5) / 12,
                 "calvo": -1,
                 "gorro": 13}[peinado]
        for y in range(0, int(borde)):
            pelo_en(x, y)
    if peinado in ("corto", "raya", "despeinado"):   # patillas
        for y in range(10, ey + 4):
            for x in list(range(0, 3)) + list(range(45, 48)):
                pelo_en(x, y)
    if peinado == "largo":                            # melena a los lados
        for y in range(8, 56):
            ancho_l = 5 + (2 if y > 40 else 0)
            for x in list(range(0, ancho_l)) + list(range(48 - ancho_l, 48)):
                pelo_en(x, y)
    if peinado == "calvo":                            # corona de pelo a los lados
        for y in range(14, 30):
            for x in list(range(0, 3)) + list(range(45, 48)):
                pelo_en(x, y)
        L.disco(23.5, 6, 8, col((255, 245, 230)), 0.12)  # brillo de la calva
    if peinado == "gorro":                            # gorra de uniforme con visera
        gorra = Lienzo(48, 56, p["gorro"])
        gorra.vetas(rng, 0.08, vertical=False)
        L.img[:12] = gorra.img[:12]
        L.rect(0, 11, 48, 13, col(p["gorro"]) * 0.55)
        L.rect(21, 4, 27, 8, col((200, 170, 70)))     # insignia
    return L


def nuca(p, rng):
    """Detras y a los lados de la cabeza (48x56, cilindrico: el centro es el frente)."""
    piel, pelo = p["piel"], p["pelo"]
    L = Lienzo(48, 56, piel)
    L.img *= 0.92
    mech = Lienzo(48, 56, pelo)
    mech.vetas(rng, 0.2)
    mech.ruido(rng, 0.05)
    peinado = p.get("peinado", "corto")
    for x in range(48):
        frente = abs(x - 23.5) < 9          # la parte que esta junto a la cara
        if peinado == "largo":
            hasta = 56 if not frente else 12
        elif peinado == "calvo":
            hasta = 0
        elif peinado == "gorro":
            hasta = 14 if frente else 34
        else:
            hasta = 12 if frente else 38
        L.img[:hasta, x] = mech.img[:hasta, x]
    if peinado == "calvo":
        for x in range(48):
            if abs(x - 23.5) >= 9:
                L.img[22:36, x] = mech.img[22:36, x]
    if peinado == "gorro":
        gorra = Lienzo(48, 56, p["gorro"])
        L.img[:12] = gorra.img[:12]
    return L


def coronilla(p, rng):
    L = Lienzo(32, 32, p["gorro"] if p.get("peinado") == "gorro" else
               (p["piel"] if p.get("peinado") == "calvo" else p["pelo"]))
    L.vetas(rng, 0.15)
    L.ruido(rng, 0.05)
    return L


# ---------------------------------------------------------------------------
# Ropa
# ---------------------------------------------------------------------------
def tela(w, h, c, rng, patron=None, c2=None):
    L = Lienzo(w, h, c)
    if patron == "cuadros":
        for y in range(h):
            for x in range(w):
                if (x // 6) % 2 == 0:
                    L.px(x, y, col(c2), 0.45)
                if (y // 6) % 2 == 0:
                    L.px(x, y, col(c2), 0.45)
                if x % 6 == 0 or y % 6 == 0:
                    L.px(x, y, col(c) * 0.6, 0.5)
    elif patron == "rayas":
        for y in range(h):
            if (y // 4) % 2 == 0:
                L.rect(0, y, w, y + 1, col(c2))
    L.vetas(rng, 0.05, vertical=True)
    L.vetas(rng, 0.04, vertical=False)
    L.ruido(rng, 0.04)
    return L


def torso(p, rng):
    r = p["ropa"]
    L = tela(96, 40, r["torso"], rng, r.get("patron"), r.get("color2"))
    piel = col(p["piel"])
    cx = 48  # el frente queda en el centro de la region
    for y in range(0, 6):                      # escote en V
        for x in range(cx - 4 + y // 2, cx + 4 - y // 2):
            L.px(x, y, piel * 0.9)
    if r.get("cuello"):
        for y in range(0, 4):
            L.px(cx - 6 + y, y, col(r["cuello"]))
            L.px(cx + 5 - y, y, col(r["cuello"]))
    if r.get("tirantes"):
        for x0 in (cx - 9, cx + 7):
            L.rect(x0, 0, x0 + 3, 40, col(r["tirantes"]))
            L.rect(x0, 30, x0 + 3, 32, col((200, 180, 90)))   # hebilla
    if r.get("delantal"):
        L.rect(cx - 13, 14, cx + 13, 40, col(r["delantal"]))
        L.rect(cx - 13, 14, cx + 13, 15, col(r["delantal"]) * 0.8)
    if r.get("solapas"):
        for y in range(0, 22):
            L.px(cx - 3 - y // 3, y, col(r["torso"]) * 0.5)
            L.px(cx + 2 + y // 3, y, col(r["torso"]) * 0.5)
    if r.get("botones"):
        for y in range(9, 38, 6):
            L.rect(cx - 1, y, cx + 1, y + 2, col(r["botones"]))
    if r.get("cinturon"):
        L.rect(0, 34, 96, 37, col(r["cinturon"]))
        L.rect(cx - 2, 34, cx + 2, 37, col((190, 170, 90)))
    for x in range(96):                        # espalda algo mas oscura (costura)
        if abs(x - cx) > 40:
            L.img[:, x] *= 0.9
    return L


def manga(p, rng):
    r = p["ropa"]
    L = tela(32, 32, r.get("mangas", r["torso"]), rng, r.get("patron_mangas", r.get("patron")),
             r.get("color2"))
    L.rect(0, 28, 32, 32, col(r.get("mangas", r["torso"])) * 0.75)
    return L


def antebrazo(p, rng):
    r = p["ropa"]
    if r.get("arremangado"):
        L = Lienzo(32, 32, p["piel"])
        L.ruido(rng, 0.03)
        L.rect(0, 0, 32, 4, col(r.get("mangas", r["torso"])) * 0.85)   # manga enrollada
        return L
    L = tela(32, 32, r.get("mangas", r["torso"]), rng, r.get("patron_mangas", r.get("patron")),
             r.get("color2"))
    L.rect(0, 26, 32, 32, col(r.get("mangas", r["torso"])) * 0.7)       # puno
    return L


def pantalon(p, rng):
    r = p["ropa"]
    L = tela(64, 32, r["pantalon"], rng)
    if r.get("falda"):
        L.vetas(rng, 0.12)                      # pliegues
        L.rect(0, 29, 64, 32, col(r["pantalon"]) * 0.7)
    else:
        for x in (16, 48):
            L.rect(x, 0, x + 1, 32, col(r["pantalon"]) * 0.6)
    return L


def zapatos(p, rng):
    L = Lienzo(32, 32, p["ropa"].get("zapatos", (52, 36, 26)))
    L.ruido(rng, 0.06)
    L.rect(0, 28, 32, 32, col((20, 16, 14)))   # suela
    L.rect(6, 4, 26, 7, col((255, 255, 255)) * 0.0 + col(p["ropa"].get("zapatos", (52, 36, 26))) * 1.3)
    return L


def piel(p, rng):
    L = Lienzo(32, 16, p["piel"])
    L.ruido(rng, 0.03)
    return L


def atlas(p, rng):
    img = np.zeros((128, 128, 3), np.float32)
    piezas = {"FACE": cara, "HEADBACK": nuca, "HAIR": coronilla, "SKIN": piel, "TORSO": torso,
              "SLEEVE": manga, "FOREARM": antebrazo, "PANTS": pantalon, "SHOES": zapatos}
    for region, fn in piezas.items():
        x0, y0, x1, y1 = R[region]
        img[y0:y1, x0:x1] = np.clip(fn(p, rng).img, 0, 1)
    q = quantize_ordered(img, 32, 0.6)
    return Image.fromarray((np.clip(q, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB")


# ---------------------------------------------------------------------------
# Los personajes
# ---------------------------------------------------------------------------
PERSONAJES = {
    "aldeana": dict(piel=(214, 168, 136), pelo=(92, 52, 30), ojos=(70, 90, 50), peinado="largo",
                    rubor=True, boca=3, sonrisa=True,
                    ropa=dict(torso=(52, 86, 60), mangas=(222, 214, 196), delantal=(214, 204, 180),
                              cuello=(222, 214, 196), pantalon=(64, 56, 46), falda=True,
                              zapatos=(48, 34, 26))),
    "granjero": dict(piel=(196, 140, 104), pelo=(52, 36, 26), ojos=(60, 40, 26), peinado="corto",
                     barba="bigote", sombra_barba=True, cejas=2,
                     ropa=dict(torso=(150, 44, 40), color2=(40, 30, 30), patron="cuadros",
                               tirantes=(52, 70, 104), pantalon=(56, 72, 108), arremangado=True,
                               zapatos=(60, 42, 28))),
    "anciano": dict(piel=(224, 186, 162), pelo=(216, 214, 210), ojos=(80, 100, 110), peinado="calvo",
                    barba="completa", color_barba=(226, 224, 220), viejo=True, cejas=2,
                    ropa=dict(torso=(98, 72, 52), solapas=True, botones=(52, 40, 30),
                              mangas=(98, 72, 52), pantalon=(86, 84, 80), zapatos=(40, 30, 24))),
    "nino": dict(piel=(236, 196, 168), pelo=(176, 84, 36), ojos=(70, 110, 140), peinado="despeinado",
                 rubor=True, pecas=True, nino=True, sonrisa=True, boca=3,
                 ropa=dict(torso=(214, 178, 72), color2=(120, 80, 50), patron="rayas",
                           mangas=(214, 178, 72), patron_mangas="rayas", arremangado=True,
                           pantalon=(104, 76, 52), zapatos=(70, 48, 34))),
    "sereno": dict(piel=(188, 146, 112), pelo=(30, 26, 24), ojos=(50, 36, 26), peinado="gorro",
                   gorro=(34, 40, 70), barba="bigote", ojeras=True, cejas=2,
                   ropa=dict(torso=(34, 40, 70), botones=(200, 170, 70), cinturon=(28, 22, 18),
                             mangas=(34, 40, 70), pantalon=(30, 34, 56), zapatos=(22, 20, 20))),
    "aldeano_noche": dict(piel=(122, 82, 58), pelo=(24, 20, 18), ojos=(40, 28, 20), peinado="corto",
                          barba="completa", color_barba=(30, 24, 20), boca=4, sonrisa=True,
                          ropa=dict(torso=(112, 40, 52), botones=(220, 210, 190),
                                    mangas=(112, 40, 52), pantalon=(60, 58, 62),
                                    zapatos=(40, 28, 22))),
    "sepulturero": dict(piel=(190, 182, 168), pelo=(120, 118, 112), ojos=(90, 90, 80),
                        peinado="raya", ojeras=True, sombra_barba=True, viejo=True, boca=3,
                        ropa=dict(torso=(42, 40, 40), solapas=True, botones=(30, 28, 28),
                                  mangas=(42, 40, 40), pantalon=(34, 32, 34),
                                  zapatos=(26, 22, 20))),
    # gabardina negra con capucha (region del pantalon) sobre una tunica gris oscura, que
    # asoma por delante; piel palida. El pelo, oscuro: lo que asome bajo la capucha es sombra
    "encapuchado": dict(piel=(192, 180, 172), pelo=(18, 17, 20), ojos=(150, 176, 168), peinado="corto",
                        barba="completa", color_barba=(120, 118, 116), ojeras=True, cejas=2,
                        ropa=dict(torso=(52, 48, 50), mangas=(24, 22, 26), cinturon=(80, 70, 56),
                                  pantalon=(20, 19, 23), falda=True, zapatos=(16, 14, 14))),
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(here, "..", "assets", "textures"))
    ap.add_argument("--seed", type=int, default=5)
    ap.add_argument("--preview", action="store_true", help="guarda tambien ampliaciones x4")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for i, (nombre, p) in enumerate(PERSONAJES.items()):
        rng = np.random.default_rng(args.seed * 100 + i)
        img = atlas(p, rng)
        ruta = os.path.join(args.out, f"pj_{nombre}.png")
        img.save(ruta)
        if args.preview:
            img.resize((512, 512), Image.NEAREST).save(os.path.join(args.out, f"pj_{nombre}_x4.png"))
        print(ruta)


if __name__ == "__main__":
    main()
