"""
Modelos del lago al atardecer (escena lago.json, despues de la aldea):

  lago.obj        lago ovalado de 26 x 14 m (agua, orilla de barro y piedras) con las cajas de
                  colision que impiden meterse en el agua salvo por el pasillo del muelle
                  (x = 2.2 .. 3.8 desde la orilla de delante). Agua a 3 cm: en la escena va con
                  "layer": 1 sobre el suelo.
  muelle.obj      muelle de tablas de 6.5 m hacia -Z, con postes y un poste con farol al final
                  (la luz la pone la entidad). Se pisa: tablero a 25 cm.
  barca.obj       barca de remos con dos bancos y los remos dentro.
  sauce.obj       sauce lloron con un columpio colgado de una rama.
  montes.obj      montanas de fondo en un anillo de 48 m de radio (sin colision).
  rocas_orilla.obj  grupo de piedras para los bordes.

Coordenadas del MOTOR (x derecha, y arriba, z hacia el jugador) pasadas a Blender con B().

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

import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

RX, RZ = 13.0, 7.0          # semiejes del lago
PASILLO = (2.2, 3.8)        # x del muelle, que entra desde la orilla de delante (+Z)


def B(x, y, z):
    """Motor -> Blender."""
    return Vector((x, -z, y))


def caja(m, lo, hi, mat, rot=(0.0, 0.0, 0.0)):
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    t = [b - a for a, b in zip(lo, hi)]
    m.caja(B(*c), (t[0], t[2], t[1]), mat, rot=rot)


def col(m, lo, hi):
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    t = [b - a for a, b in zip(lo, hi)]
    m.colision(tuple(B(*c)), (t[0], t[2], t[1]))


def anillo_plano(m, rx0, rz0, rx1, rz1, y, lados, mat):
    """Banda horizontal entre dos ovalos (mirando arriba)."""
    for k in range(lados):
        a0 = 2 * math.pi * k / lados
        a1 = 2 * math.pi * (k + 1) / lados
        p = [(rx0 * math.cos(a0), y, rz0 * math.sin(a0)), (rx0 * math.cos(a1), y, rz0 * math.sin(a1)),
             (rx1 * math.cos(a1), y, rz1 * math.sin(a1)), (rx1 * math.cos(a0), y, rz1 * math.sin(a0))]
        # orden antihorario visto desde arriba: el quad debe mirar a +Y del motor (+Z Blender)
        f = m.poligono([B(*q) for q in p], mat)
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()


def lago(col_):
    m = rc.Malla()
    rnd = random.Random(7)
    lados = 28
    # agua: abanico desde el centro
    centro = m.bm.verts.new(B(0, 0.03, 0))
    borde = [m.bm.verts.new(B(RX * math.cos(2 * math.pi * k / lados), 0.03,
                                RZ * math.sin(2 * math.pi * k / lados))) for k in range(lados)]
    idx = m._indice("mat_agua_ocaso")  # refleja el cielo del atardecer
    for k in range(lados):
        f = m.bm.faces.new((centro, borde[k], borde[(k + 1) % lados]))
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
        f.material_index = idx
    # orilla de barro y piedras sueltas por el borde
    anillo_plano(m, RX, RZ, RX + 0.9, RZ + 0.9, 0.025, lados, "mat_tierra_suelo")
    for k in range(40):
        a = 2 * math.pi * k / 40 + rnd.uniform(-0.05, 0.05)
        r = rnd.uniform(0.15, 0.7)
        x, z = (RX + r) * math.cos(a), (RZ + r) * math.sin(a)
        if PASILLO[0] - 0.4 < x < PASILLO[1] + 0.4 and z > 0:
            continue                                   # la entrada del muelle, despejada
        t = rnd.uniform(0.18, 0.42)
        caja(m, (x - t, 0, z - t * 0.8), (x + t, t * 0.55, z + t * 0.8), "mat_roca",
             rot=(0, 0, rnd.uniform(0, 90)))
    # colision: franjas de 1 m que siguen el ovalo; las de delante dejan el pasillo del muelle
    z = -RZ
    while z < RZ:
        zc = z + 0.5
        half = RX * math.sqrt(max(0.0, 1 - (zc / RZ) ** 2)) - 0.35
        if half > 0.3:
            if zc > 0.5:
                if -half < PASILLO[0]:
                    col(m, (-half, 0, z), (min(PASILLO[0], half), 2.0, z + 1.0))
                if half > PASILLO[1]:
                    col(m, (PASILLO[1], 0, z), (half, 2.0, z + 1.0))
            else:
                col(m, (-half, 0, z), (half, 2.0, z + 1.0))
        z += 1.0
    return m.objeto("lago", col_)


def muelle(col_):
    m = rc.Malla()
    L = 6.5
    z = 0.0
    k = 0
    while z > -L + 0.01:                                   # tablas sueltas, alguna torcida
        caja(m, (-0.8, 0.17, z - 0.24), (0.8, 0.25, z), "mat_madera_gris",
             rot=(0, 0, 2.0 if k % 5 == 3 else 0.0))
        z -= 0.27
        k += 1
    for zz in (-0.4, -2.0, -3.6, -5.2, -L + 0.15):        # vigas y postes que entran al agua
        caja(m, (-0.85, 0.05, zz - 0.08), (0.85, 0.17, zz + 0.08), "mat_madera")
        for x in (-0.78, 0.78):
            caja(m, (x - 0.07, -0.4, zz - 0.07), (x + 0.07, 0.4, zz + 0.07), "mat_madera")
    for x in (-0.78, 0.78):                                # postes de amarre al final
        m.prisma(B(x, 0.25, -L + 0.15), 0.08, 0.07, 0.7, 6, "mat_madera")
    # farol al final, a la derecha
    m.prisma(B(0.78, 0.95, -L + 0.15), 0.05, 0.05, 0.8, 4, "mat_madera")
    caja(m, (0.6, 1.62, -L + 0.08), (0.98, 1.68, -L + 0.22), "mat_metal")
    caja(m, (0.7, 1.38, -L + 0.07), (0.86, 1.6, -L + 0.23), "mat_ventana_luz")
    caja(m, (0.68, 1.6, -L + 0.06), (0.88, 1.64, -L + 0.24), "mat_metal")
    col(m, (-0.85, 0, -L), (0.85, 0.25, 0.0))
    return m.objeto("muelle", col_)


def barca(col_):
    m = rc.Malla()
    perfil = [(0.0, 0.32, 1.0), (0.14, 0.46, 1.22), (0.34, 0.52, 1.32)]
    fuera = m.anillos(B(0, 0, 0), perfil, 10, "mat_madera", tapa_arriba=False)
    dentro = m.anillos(B(0, 0.06, 0), [(z, rx - 0.05, ry - 0.06) for z, rx, ry in perfil[1:]] +
                       [(0.29, 0.47, 1.26)], 10, "mat_madera_gris", tapa_abajo=False,
                       tapa_arriba=False)
    bmesh.ops.reverse_faces(m.bm, faces=list(dentro))      # se ve por dentro
    caja(m, (-0.38, 0.06, -1.0), (0.38, 0.1, 1.0), "mat_madera_gris")   # fondo
    for zz in (-0.45, 0.5):                                              # bancos
        caja(m, (-0.46, 0.24, zz - 0.12), (0.46, 0.29, zz + 0.12), "mat_madera")
    for lado in (-1, 1):                                                 # remos
        m.segmento(B(lado * 0.2, 0.32, -0.9), B(lado * 0.28, 0.33, 0.9), 0.025, 0.025, 4, "mat_madera")
        caja(m, (lado * 0.28 - 0.06, 0.31, 0.75), (lado * 0.28 + 0.06, 0.34, 1.05), "mat_madera")
    _ = fuera
    return m.objeto("barca", col_)


def sauce(col_):
    m = rc.Malla()
    rnd = random.Random(3)
    # tronco nudoso que se abre en tres ramas
    m.segmento(B(0, 0, 0), B(0.15, 1.6, 0.05), 0.32, 0.26, 7, "mat_corteza")
    ramas = [(B(0.15, 1.6, 0.05), B(-1.1, 3.0, -0.3)), (B(0.15, 1.6, 0.05), B(1.0, 3.2, 0.4)),
             (B(0.15, 1.6, 0.05), B(0.2, 3.6, -0.6))]
    for p0, p1 in ramas:
        m.segmento(p0, p1, 0.2, 0.12, 6, "mat_corteza")
    # copa: cupula baja
    m.anillos(B(0.05, 2.9, -0.1), [(0.0, 2.2, 2.0), (0.6, 2.4, 2.2), (1.2, 2.0, 1.8),
                                   (1.7, 1.1, 1.0), (1.95, 0.0, 0.0)], 10, "mat_hojas_claras")
    # cortina de ramas colgantes alrededor
    for k in range(34):
        a = 2 * math.pi * k / 34 + rnd.uniform(-0.06, 0.06)
        r = rnd.uniform(2.0, 2.45)
        x, z = 0.05 + r * math.cos(a), -0.1 + r * 0.92 * math.sin(a)
        if 0.6 < x < 2.2 and z > 0.4:
            continue                                  # hueco delante del columpio
        top = rnd.uniform(3.2, 3.6)
        bajo = rnd.uniform(0.5, 1.3)
        m.segmento(B(x, top, z), B(x * 1.04, bajo, z * 1.04), 0.13, 0.05, 4,
                   "mat_hojas" if k % 3 else "mat_hojas_claras", ovalo=(1.0, 0.45))
    # columpio: rama horizontal, dos cuerdas y el asiento
    m.segmento(B(0.4, 3.05, 0.9), B(2.3, 3.0, 1.1), 0.1, 0.07, 5, "mat_corteza")
    for x in (1.05, 1.65):
        m.segmento(B(x, 3.0, 1.0), B(x, 0.62, 1.0), 0.018, 0.018, 4, "mat_lona")
    caja(m, (0.95, 0.56, 0.85), (1.75, 0.62, 1.15), "mat_madera")
    col(m, (-0.35, 0, -0.35), (0.45, 2.5, 0.4))
    return m.objeto("sauce", col_)


def montes(col_):
    m = rc.Malla()
    rnd = random.Random(11)
    k = 0
    a = 0.0
    while a < 2 * math.pi:
        r = 48 + rnd.uniform(-4, 6)
        x, z = r * math.cos(a), r * math.sin(a)
        alto = rnd.uniform(8, 19)
        base = rnd.uniform(7, 13)
        lados = rnd.choice((5, 6, 7))
        m.prisma(B(x, -1, z), base, base * 0.08, alto, lados, "mat_roca", giro=rnd.uniform(0, 60))
        if alto > 13:                                  # nieve en las mas altas
            frac = 0.72
            m.prisma(B(x, -1 + alto * frac, z), base * (1 - frac * 0.92) + 0.25, base * 0.06,
                     alto * (1 - frac) + 0.15, lados, "mat_yeso", giro=0)
        a += rnd.uniform(0.16, 0.3)
        k += 1
    return m.objeto("montes", col_)


def rocas_orilla(col_):
    m = rc.Malla()
    for x, z, t, h, giro in ((0, 0, 0.55, 0.45, 10), (0.7, 0.3, 0.35, 0.3, 40), (-0.5, 0.4, 0.3, 0.22, 70),
                             (0.3, -0.5, 0.28, 0.2, 25)):
        m.prisma(B(x, 0, z), t, t * 0.6, h, 6, "mat_roca", giro=giro)
    col(m, (-0.6, 0, -0.5), (0.6, 0.45, 0.5))
    return m.objeto("rocas_orilla", col_)


PIEZAS = [
    (lago, "Lago ovalado 26 x 14 m con orilla; colision salvo el pasillo del muelle (x 2.2..3.8,\n"
           "desde la orilla de +Z). Agua a 3 cm: usar \"layer\": 1."),
    (muelle, "Muelle de 6.5 m hacia -Z, tablero a 25 cm, farol al final a la derecha (x 0.78, y 1.5)."),
    (barca, "Barca de remos de 2.6 m (larga en Z)."),
    (sauce, "Sauce llorón con columpio (asiento en x 1.35, z 1.0)."),
    (montes, "Montanas de fondo en un anillo de 48 m de radio, sin colision."),
    (rocas_orilla, "Grupo de piedras."),
]


def construir():
    sc = rc.escena()
    col_ = rc.coleccion("lago", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col_)
        ob.location = (400 + k * 30, 400, 0)
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
