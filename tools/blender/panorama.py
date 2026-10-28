"""
Panoramas: el horizonte lejano de las escenas de exterior. La escena los pone en "panorama" y el
motor los dibuja CENTRADOS EN LA CAMARA (nunca se llega a ellos ni cortan la escena) con una
bruma fija. Todo esta entre 90 y 210 m del centro, mas lejos que cualquier escena.

  panorama_montes.obj  cordillera de picos con nieve (150-210 m) y colinas delante (95-120 m).
  panorama_bosque.obj  linea de arboles (pinos y arboles secos, 90-115 m), colinas y montes
                       bajos detras.

El modelo baja hasta y = -12 (la escena lo pone en "y": -6 o asi): la base nunca se ve.

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402

importlib.reload(rc)

from mathutils import Vector  # noqa: E402

BAJO = -12.0


def B(x, y, z):
    """Motor -> Blender."""
    return Vector((x, -z, y))


def monte(m, x, z, alto, base, lados, giro, nieve):
    """Cono de roca terminado en punta; la nieve es la punta del MISMO cono (mismos lados y giro),
    un poco mas ancha, asi encaja sin escalones."""
    total = alto - BAJO
    m.prisma(B(x, BAJO, z), base, 0.0, total, lados, "mat_roca", giro=giro)
    if nieve:
        frac = 0.7
        r = base * (1 - frac) * 1.04
        m.prisma(B(x, BAJO + total * frac, z), r, 0.0, total * (1 - frac) + 0.05, lados, "mat_yeso",
                 giro=giro)


def colinas(m, rnd, radio, n, alto, mat):
    a = rnd.uniform(0, 1)
    for _ in range(n):
        r = radio + rnd.uniform(-6, 6)
        x, z = r * math.cos(a), r * math.sin(a)
        h = rnd.uniform(*alto)
        base = rnd.uniform(14, 24)
        m.prisma(B(x, BAJO, z), base, base * 0.35, h - BAJO, 8, mat, giro=rnd.uniform(0, 45))
        a += 2 * math.pi / n * rnd.uniform(0.8, 1.2)


def panorama_montes(col_):
    m = rc.Malla()
    rnd = random.Random(21)
    a = 0.0
    while a < 2 * math.pi:
        r = rnd.uniform(155, 205)
        alto = rnd.uniform(28, 72)
        monte(m, r * math.cos(a), r * math.sin(a), alto, alto * rnd.uniform(0.55, 0.8),
              rnd.choice((5, 6, 7)), rnd.uniform(0, 60), alto > 45)
        a += rnd.uniform(0.11, 0.2)
    colinas(m, rnd, 108, 34, (6, 13), "mat_hojas")
    return m.objeto("panorama_montes", col_)


def panorama_bosque(col_):
    m = rc.Malla()
    rnd = random.Random(22)
    a = 0.0                                   # montes bajos al fondo
    while a < 2 * math.pi:
        r = rnd.uniform(170, 205)
        alto = rnd.uniform(18, 34)
        monte(m, r * math.cos(a), r * math.sin(a), alto, alto * 0.9, 6, rnd.uniform(0, 60), False)
        a += rnd.uniform(0.15, 0.28)
    colinas(m, rnd, 135, 30, (5, 10), "mat_hojas")
    a = 0.0                                   # la linea de arboles
    while a < 2 * math.pi:
        r = rnd.uniform(90, 115)
        x, z = r * math.cos(a), r * math.sin(a)
        if rnd.random() < 0.75:               # pino: tronco y tres pisos de copa
            h = rnd.uniform(14, 24)
            m.prisma(B(x, BAJO, z), 0.6, 0.5, 4 - BAJO, 5, "mat_corteza")
            for k, (y0, frac) in enumerate(((0.25, 0.42), (0.45, 0.34), (0.65, 0.26))):
                m.prisma(B(x, h * y0, z), h * frac, 0.0, h * 0.42, 7, "mat_pino", giro=k * 20)
        else:                                 # arbol seco: tronco y unas ramas
            h = rnd.uniform(12, 20)
            m.prisma(B(x, BAJO, z), 0.9, 0.3, h - BAJO, 5, "mat_corteza")
            for k in range(4):
                ang = rnd.uniform(0, 2 * math.pi)
                y = h * rnd.uniform(0.45, 0.8)
                m.segmento(B(x, y, z), B(x + 4 * math.cos(ang), y + rnd.uniform(2, 5), z + 4 * math.sin(ang)),
                           0.35, 0.08, 4, "mat_corteza")
        a += rnd.uniform(0.035, 0.07)
    return m.objeto("panorama_bosque", col_)


PIEZAS = [
    (panorama_montes, "Panorama: cordillera con nieve (155-205 m) y colinas (100-115 m). Base en y -12."),
    (panorama_bosque, "Panorama: linea de arboles (90-115 m), colinas y montes bajos. Base en y -12."),
]


def construir():
    sc = rc.escena()
    col_ = rc.coleccion("panorama", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col_)
        ob.location = (2000 + k * 500, 2000, 0)
        obs.append(ob)
    return obs


def exportar(obs):
    res = []
    for ob, (_, comentario) in zip(obs, PIEZAS):
        loc = tuple(ob.location)
        ob.location = (0, 0, 0)
        res.append(rc.exportar([ob], ob.name, comentario))
        ob.location = loc
    return res


if __name__ == "__main__":
    print(exportar(construir()))
