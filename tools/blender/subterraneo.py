"""
Modelos del subterraneo: el interior de la cripta del cementerio y los tuneles de tierra.

  cripta_interior.obj  sala cerrada de 8 x 11 m con boveda de canon, nichos y una escalera que
                       baja por un hueco en el suelo. El SUELO de la sala esta a 3 m (el motor
                       no deja bajar de Y=0): la escalera baja de 3 a 0 y sigue por un rellano
                       oscuro, donde la escena pone el trigger que lleva a los tuneles.
  tuneles.obj          toda la red de tuneles (mapa en tuneles_mapa.py): suelo, techo y paredes
                       por celdas de 3 m, con las paredes y el techo deformados con ruido para
                       que parezcan excavados. Incluye la escalera que sube a la cripta, el
                       fondo del pozo de la aldea (con la cuerda), la escalera de mano de la
                       salida a la plaza, el lago y el derrumbe.
  sarcofago, atril, puerta_cripta, estatua, reja: piezas sueltas para colocar en las escenas.

Todo se escribe en coordenadas del MOTOR (x derecha, y arriba, z hacia el jugador) y se pasa
a Blender con B(). Las caras de interiores miran hacia dentro: el motor no dibuja la cara de
atras.

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402
import tuneles_mapa as tm  # noqa: E402

importlib.reload(rc)
importlib.reload(tm)

import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

S = tm.CELDA
TIERRA = {"mat_tierra", "mat_tierra_suelo"}   # lo unico que se deforma con ruido


def B(x, y, z):
    """Motor -> Blender."""
    return Vector((x, -z, y))


def cara(m, puntos, mat, normal):
    """Cara con las esquinas en coordenadas del motor, orientada hacia 'normal' (motor)."""
    n = Vector((0.0, 0.0, 0.0))
    for a, b in zip(puntos, puntos[1:] + puntos[:1]):  # normal de Newell
        n.x += (a[1] - b[1]) * (a[2] + b[2])
        n.y += (a[2] - b[2]) * (a[0] + b[0])
        n.z += (a[0] - b[0]) * (a[1] + b[1])
    if n.dot(Vector(normal)) < 0:
        puntos = list(reversed(puntos))
    return m.poligono([B(*p) for p in puntos], mat)


def caja(m, lo, hi, mat, colision=False):
    """Caja alineada a los ejes entre lo y hi (motor)."""
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    t = [b - a for a, b in zip(lo, hi)]
    m.caja(B(*c), (t[0], t[2], t[1]), mat)
    if colision:
        col(m, lo, hi)


def col(m, lo, hi):
    """Caja de colision entre lo y hi (motor)."""
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    t = [b - a for a, b in zip(lo, hi)]
    m.colision(tuple(B(*c)), (t[0], t[2], t[1]))


def cortes(a, b, paso=1.0, extra=()):
    """Valores de a a b cada 'paso' (multiplos absolutos) mas los de 'extra' que caigan dentro:
    asi las piezas vecinas comparten vertices y no quedan grietas al deformar."""
    vs = {a, b}
    k = math.floor(a / paso) + 1
    while k * paso < b - 1e-6:
        vs.add(round(k * paso, 4))
        k += 1
    vs.update(e for e in extra if a + 1e-6 < e < b - 1e-6)
    return sorted(vs)


NIVELES = sorted(set(tm.ALTURA.values()) | {-0.8, 0.0})


def pared(m, p0, p1, y0, y1, mat, normal):
    """Pared vertical de p0 a p1 ((x, z) del motor), de y0 a y1, subdividida en una rejilla de
    1 m y en los niveles comunes (alturas de techo), mirando hacia 'normal'."""
    largo = math.dist(p0, p1)
    ts = [d / largo for d in cortes(0.0, largo)]
    ys = cortes(y0, y1, extra=NIVELES)
    for ta, tb in zip(ts, ts[1:]):
        xa, za = p0[0] + (p1[0] - p0[0]) * ta, p0[1] + (p1[1] - p0[1]) * ta
        xb, zb = p0[0] + (p1[0] - p0[0]) * tb, p0[1] + (p1[1] - p0[1]) * tb
        for ya, yb in zip(ys, ys[1:]):
            cara(m, [(xa, ya, za), (xb, ya, zb), (xb, yb, zb), (xa, yb, za)], mat, normal)


def plano(m, x0, x1, z0, z1, y, mat, arriba, hueco=None, xs=None, zs=None):
    """Suelo (arriba=True) o techo horizontal en rejilla de 1 m; 'hueco' = (x0, x1, z0, z1)
    que se deja abierto."""
    xs = xs or cortes(x0, x1)
    zs = zs or cortes(z0, z1)
    for xa, xb in zip(xs, xs[1:]):
        for za, zb in zip(zs, zs[1:]):
            if hueco and hueco[0] <= xa and xb <= hueco[1] and hueco[2] <= za and zb <= hueco[3]:
                continue
            cara(m, [(xa, y, za), (xb, y, za), (xb, y, zb), (xa, y, zb)], mat,
                 (0, 1 if arriba else -1, 0))


def pozo_vertical(m, x0, x1, z0, z1, y0, y1, mat):
    """Las cuatro paredes de un hueco vertical (mirando hacia dentro)."""
    pared(m, (x0, z0), (x1, z0), y0, y1, mat, (0, 0, 1))
    pared(m, (x0, z1), (x1, z1), y0, y1, mat, (0, 0, -1))
    pared(m, (x0, z0), (x0, z1), y0, y1, mat, (1, 0, 0))
    pared(m, (x1, z0), (x1, z1), y0, y1, mat, (-1, 0, 0))


# ---------------------------------------------------------------------------
# Ruido para deformar la tierra (determinista: depende solo de la posicion)
# ---------------------------------------------------------------------------
def _hash(ix, iy, iz, semilla):
    n = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791) ^ (semilla * 2654435761)
    n &= 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return (n ^ (n >> 16)) / 0xFFFFFFFF


def ruido(p, semilla, escala):
    """Ruido de valor 3D suave, -0.5..0.5."""
    x, y, z = p.x / escala, p.y / escala, p.z / escala
    ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    fx, fy, fz = (f * f * (3 - 2 * f) for f in (fx, fy, fz))
    total = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (fx if dx else 1 - fx) * (fy if dy else 1 - fy) * (fz if dz else 1 - fz)
                total += w * _hash(ix + dx, iy + dy, iz + dz, semilla)
    return total - 0.5


def deformar(m, horizontal=0.45, vertical=0.5):
    """Mueve los vertices que SOLO tocan caras de tierra. Los del suelo no suben ni bajan (el
    jugador camina en Y=0); los que comparte con piedra o madera no se mueven, asi encajan."""
    bm = m.bm
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bm.normal_update()
    idx_tierra = {i for i, nombre in enumerate(m.mats) if nombre in TIERRA}
    for v in bm.verts:
        caras = v.link_faces
        if not caras or any(f.material_index not in idx_tierra for f in caras):
            continue
        p = v.co.copy()
        def r(s):
            return ruido(p, s, 1.9) + 0.35 * ruido(p, s + 7, 0.8)
        d = Vector((r(1) * horizontal, r(2) * horizontal, 0.0))
        if not any(f.normal.z > 0.9 for f in caras):
            d.z = r(3) * vertical
        v.co += d


# ---------------------------------------------------------------------------
# Interior de la cripta
# ---------------------------------------------------------------------------
SUELO_CRIPTA = 3.0
X_SALA, Z_ATRAS, Z_FRENTE = 4.0, -6.0, 5.0
ALTO_PARED = 6.5
X_HUECO, Z_HUECO = 1.0, -1.5          # hueco de la escalera: |x| < 1, z < -1.5
HUELLA, TABICA = 0.375, 0.25           # escalon: fondo y alto
TOPE_POZO = 12.0                       # altura del tiro del pozo de la aldea (tuneles)
TOPE_TRAMPILLA = 5.4                   # altura de la trampilla de salida (tuneles)
Z_RELLANO = -9.5                       # fin del rellano de abajo


# Nichos de las paredes largas (las dos iguales): (z0, z1, y0, y1). Encima de los sarcofagos.
NICHOS = [(z0, z1, y0, y0 + 0.7) for z0, z1 in ((-3.0, -1.0), (0.0, 2.0)) for y0 in (4.0, 5.0)]
FONDO_NICHO = 0.55


def pared_con_nichos(m, x, nx, y0, nichos):
    """Pared larga de la cripta (plano x, mirando hacia nx) con huecos de verdad: la pared se
    trocea por los bordes de los nichos y cada nicho tiene fondo, techo, suelo y lados."""
    zs = cortes(Z_ATRAS, Z_FRENTE, extra=[v for n in nichos for v in n[:2]] + [Z_HUECO])
    ys = cortes(y0, ALTO_PARED, extra=[v for n in nichos for v in n[2:]])
    for za, zb in zip(zs, zs[1:]):
        for ya, yb in zip(ys, ys[1:]):
            if any(n[0] <= za and zb <= n[1] and n[2] <= ya and yb <= n[3] for n in nichos):
                continue
            cara(m, [(x, ya, za), (x, ya, zb), (x, yb, zb), (x, yb, za)], "mat_cripta", (nx, 0, 0))
    xf = x - nx * FONDO_NICHO     # el fondo, metido en el muro
    for za, zb, ya, yb in nichos:
        cara(m, [(xf, ya, za), (xf, ya, zb), (xf, yb, zb), (xf, yb, za)], "mat_oscuro", (nx, 0, 0))
        cara(m, [(x, ya, za), (xf, ya, za), (xf, ya, zb), (x, ya, zb)], "mat_cripta", (0, 1, 0))
        cara(m, [(x, yb, za), (xf, yb, za), (xf, yb, zb), (x, yb, zb)], "mat_cripta", (0, -1, 0))
        cara(m, [(x, ya, za), (xf, ya, za), (xf, yb, za), (x, yb, za)], "mat_cripta", (0, 0, 1))
        cara(m, [(x, ya, zb), (xf, ya, zb), (xf, yb, zb), (x, yb, zb)], "mat_cripta", (0, 0, -1))


def boveda_y(x):
    return ALTO_PARED + 1.5 * math.sqrt(max(0.0, 1.0 - (x / X_SALA) ** 2))


def cripta_interior(col_):
    m = rc.Malla()
    y0 = SUELO_CRIPTA
    zs = cortes(Z_ATRAS, Z_FRENTE, extra=(Z_HUECO,))
    xs = cortes(-X_SALA, X_SALA)

    # suelo de losas con el hueco de la escalera
    plano(m, -X_SALA, X_SALA, Z_ATRAS, Z_FRENTE, y0, "mat_losa", True,
          hueco=(-X_HUECO, X_HUECO, Z_ATRAS, Z_HUECO), xs=xs, zs=zs)
    # paredes
    for lado in (-1, 1):
        pared_con_nichos(m, lado * X_SALA, -lado, y0, NICHOS)
    pared(m, (-X_SALA, Z_ATRAS), (X_SALA, Z_ATRAS), y0, ALTO_PARED, "mat_cripta", (0, 0, 1))
    pared(m, (-X_SALA, Z_FRENTE), (X_SALA, Z_FRENTE), y0, ALTO_PARED, "mat_cripta", (0, 0, -1))

    # boveda de canon (eje en z) y sus dos lunetos
    arco = [(x, boveda_y(x)) for x in [-4, -3.4, -2.6, -1.4, 0, 1.4, 2.6, 3.4, 4]]
    for (xa, ya), (xb, yb) in zip(arco, arco[1:]):
        xm, ym = (xa + xb) / 2, (ya + yb) / 2
        n = (-xm, ALTO_PARED - ym - 0.01, 0)
        for za, zb in zip(cortes(Z_ATRAS, Z_FRENTE), cortes(Z_ATRAS, Z_FRENTE)[1:]):
            cara(m, [(xa, ya, za), (xb, yb, za), (xb, yb, zb), (xa, ya, zb)], "mat_cripta", n)
        for z, nz in ((Z_ATRAS, 1), (Z_FRENTE, -1)):
            cara(m, [(xa, ALTO_PARED, z), (xb, ALTO_PARED, z), (xb, yb, z), (xa, ya, z)],
                 "mat_cripta", (0, 0, nz))

    # pilastras y nervios de la boveda
    for z in (-3.5, -0.5, 2.5):
        for lado in (-1, 1):
            caja(m, (lado * X_SALA - 0.2, y0, z - 0.25), (lado * X_SALA + 0.2, ALTO_PARED, z + 0.25),
                 "mat_cripta", colision=True)
        for (xa, ya), (xb, yb) in zip(arco, arco[1:]):
            def dentro(x, y):
                d = Vector((-x, ALTO_PARED - 0.6 - y))
                d.normalize()
                return B(x + d.x * 0.12, y + d.y * 0.12, z)
            m.segmento(dentro(xa, ya), dentro(xb, yb), 0.13, 0.13, 4, "mat_cripta")

    # repisa de piedra bajo cada nicho
    for lado in (-1, 1):
        x = lado * X_SALA
        for za, zb, ya, _ in NICHOS:
            caja(m, (min(x, x - lado * 0.14), ya - 0.1, za - 0.1), (max(x, x - lado * 0.14), ya, zb + 0.1),
                 "mat_cripta")

    # hueco de la escalera: paredes hasta el suelo de abajo, rellano y escalones
    pared(m, (-X_HUECO, Z_RELLANO), (-X_HUECO, Z_HUECO), 0.0, y0, "mat_cripta", (1, 0, 0))
    pared(m, (X_HUECO, Z_RELLANO), (X_HUECO, Z_HUECO), 0.0, y0, "mat_cripta", (-1, 0, 0))
    pared(m, (-X_HUECO, Z_HUECO), (X_HUECO, Z_HUECO), 0.0, y0, "mat_cripta", (0, 0, -1))
    plano(m, -X_HUECO, X_HUECO, Z_RELLANO, Z_ATRAS, y0, "mat_cripta", False)    # techo del rellano
    plano(m, -X_HUECO, X_HUECO, Z_RELLANO, -5.5, 0.0, "mat_losa", True)          # suelo del rellano
    cara(m, [(-X_HUECO, 0, Z_RELLANO), (X_HUECO, 0, Z_RELLANO), (X_HUECO, y0, Z_RELLANO),
             (-X_HUECO, y0, Z_RELLANO)], "mat_oscuro", (0, 0, 1))                 # oscuridad
    k = 0
    while True:
        top = y0 - (k + 1) * TABICA
        if top <= 0.001:
            break
        z1 = Z_HUECO - k * HUELLA
        caja(m, (-X_HUECO, 0, z1 - HUELLA), (X_HUECO, top, z1), "mat_cripta", colision=True)
        k += 1

    # antepecho a los lados del hueco
    for lado in (-1, 1):
        x = lado * (X_HUECO + 0.15)
        caja(m, (x - 0.15, y0, Z_ATRAS), (x + 0.15, y0 + 0.85, Z_HUECO), "mat_cripta",
             colision=True)
        caja(m, (x - 0.2, y0 + 0.85, Z_ATRAS), (x + 0.2, y0 + 0.95, Z_HUECO), "mat_cripta")

    # colision: el suelo alto (tres bloques alrededor del hueco), las paredes y el rellano
    col(m, (-X_SALA, 0, Z_ATRAS), (-X_HUECO, y0, Z_FRENTE))
    col(m, (X_HUECO, 0, Z_ATRAS), (X_SALA, y0, Z_FRENTE))
    col(m, (-X_HUECO, 0, Z_HUECO), (X_HUECO, y0, Z_FRENTE))
    col(m, (-X_SALA - 1, 0, Z_RELLANO - 1), (-X_SALA, 9, Z_FRENTE + 1))
    col(m, (X_SALA, 0, Z_RELLANO - 1), (X_SALA + 1, 9, Z_FRENTE + 1))
    col(m, (-X_SALA, 0, Z_FRENTE), (X_SALA, 9, Z_FRENTE + 1))
    col(m, (-X_SALA, y0, Z_ATRAS - 1), (X_SALA, 9, Z_ATRAS))
    col(m, (-X_SALA, 0, Z_RELLANO - 1), (X_SALA, y0, Z_RELLANO))
    col(m, (-X_SALA, 0, Z_RELLANO), (-X_HUECO, y0, Z_ATRAS))
    col(m, (X_HUECO, 0, Z_RELLANO), (X_SALA, y0, Z_ATRAS))
    return m.objeto("cripta_interior", col_)


def sarcofago(col_):
    """Sarcofago de piedra, largo en z. Base en el suelo."""
    m = rc.Malla()
    caja(m, (-0.5, 0, -1.1), (0.5, 0.12, 1.1), "mat_marmol_viejo")
    caja(m, (-0.45, 0.12, -1.05), (0.45, 0.78, 1.05), "mat_marmol_viejo")
    caja(m, (-0.53, 0.78, -1.15), (0.53, 0.92, 1.15), "mat_marmol_viejo")      # tapa
    caja(m, (-0.08, 0.92, -0.8), (0.08, 1.0, 0.75), "mat_marmol_viejo")         # cruz en relieve
    caja(m, (-0.32, 0.92, 0.3), (0.32, 1.0, 0.44), "mat_marmol_viejo")
    caja(m, (-0.4, 0.2, 1.05), (0.4, 0.7, 1.08), "mat_oscuro")            # inscripcion
    col(m, (-0.53, 0, -1.15), (0.53, 1.0, 1.15))
    return m.objeto("sarcofago", col_)


def atril(col_):
    """Atril de madera con un libro abierto (la nota de la cripta)."""
    m = rc.Malla()
    caja(m, (-0.25, 0, -0.2), (0.25, 0.06, 0.2), "mat_madera")
    caja(m, (-0.05, 0.06, -0.05), (0.05, 1.0, 0.05), "mat_madera")
    m.caja(B(0, 1.05, 0), (0.6, 0.42, 0.05), "mat_madera", rot=(25, 0, 0))
    m.caja(B(0, 1.09, 0.02), (0.5, 0.34, 0.02), "mat_lona", rot=(25, 0, 0))
    col(m, (-0.3, 0, -0.25), (0.3, 1.2, 0.25))
    return m.objeto("atril", col_)


def puerta_cripta(col_):
    """Puerta de hierro vista desde dentro, con su marco de piedra. Frente hacia +Z."""
    m = rc.Malla()
    caja(m, (-0.65, 0, -0.06), (0.65, 2.2, 0.02), "mat_metal")
    for x in (-0.4, 0.0, 0.4):
        caja(m, (x - 0.04, 0.1, 0.02), (x + 0.04, 2.1, 0.06), "mat_metal")
    for y in (0.5, 1.6):
        caja(m, (-0.6, y - 0.04, 0.02), (0.6, y + 0.04, 0.06), "mat_metal")
    for lado in (-1, 1):
        caja(m, (lado * 0.65 - 0.15, 0, -0.1), (lado * 0.65 + 0.15, 2.45, 0.12), "mat_cripta")
    caja(m, (-0.85, 2.2, -0.1), (0.85, 2.5, 0.14), "mat_cripta")
    caja(m, (0.28, 1.0, 0.06), (0.38, 1.12, 0.12), "mat_metal")    # tirador
    col(m, (-0.8, 0, -0.1), (0.8, 2.5, 0.14))
    return m.objeto("puerta_cripta", col_)


def estatua(col_, nombre="estatua", M="mat_marmol"):
    """Figura encapuchada que reza, con una estola roja y lagrimas de sangre, sobre un pedestal
    de piedra con musgo: la del cementerio, de marmol blanco sospechosamente limpio (todo lo
    demas tiene musgo). La iglesia y los tuneles tienen las suyas (estatuas.py)."""
    m = rc.Malla()
    caja(m, (-0.7, 0, -0.7), (0.7, 0.2, 0.7), "mat_piedra_musgo")
    caja(m, (-0.55, 0.2, -0.55), (0.55, 0.75, 0.55), "mat_piedra_musgo")
    caja(m, (-0.62, 0.75, -0.62), (0.62, 0.85, 0.62), "mat_piedra_musgo")
    # tunica: anillos de revolucion desde el pedestal; cabeza inclinada bajo la capucha
    m.anillos(B(0, 0.85, 0), [(0.0, 0.42, 0.38), (0.6, 0.34, 0.3), (1.2, 0.3, 0.26),
                              (1.5, 0.28, 0.24), (1.62, 0.16, 0.16)], 8, M)
    m.anillos(B(0, 2.42, 0.06), [(0.0, 0.16, 0.17), (0.12, 0.2, 0.21), (0.26, 0.18, 0.19),
                                 (0.36, 0.0, 0.0)], 8, M, tapa_arriba=False)
    m.decal(B(0, 2.56, 0.26), (0, -1, 0), 0.2, 0.22, "mat_oscuro", salida=0.0)  # rostro en sombra
    for dx in (-0.045, 0.045):                                                 # lagrimas de sangre
        m.decal(B(dx, 2.5, 0.262), (0, -1, 0), 0.022, 0.14, "mat_sangre", salida=0.004)
    # manos juntas delante del pecho
    m.segmento(B(-0.22, 1.95, 0.18), B(0, 2.02, 0.36), 0.06, 0.05, 6, M)
    m.segmento(B(0.22, 1.95, 0.18), B(0, 2.02, 0.36), 0.06, 0.05, 6, M)
    m.caja(B(0, 2.14, 0.38), (0.08, 0.06, 0.24), M)
    # estola roja: cuello y dos bandas que caen por delante
    m.anillos(B(0, 2.3, 0.02), [(0.0, 0.2, 0.19), (0.08, 0.17, 0.16)], 8, "mat_tela_roja",
              tapa_abajo=False, tapa_arriba=False)
    for lado in (-1, 1):
        m.segmento(B(lado * 0.12, 2.32, 0.2), B(lado * 0.15, 1.3, 0.32), 0.06, 0.065, 4,
                   "mat_tela_roja", ovalo=(1.0, 0.25))
    col(m, (-0.7, 0, -0.7), (0.7, 2.8, 0.7))
    return m.objeto(nombre, col_)


def reja(col_):
    """Reja de hierro que cierra un tunel de 3 m (plano XY, frente +Z)."""
    m = rc.Malla()
    for x in (-1.45, 1.45):
        caja(m, (x - 0.08, 0, -0.08), (x + 0.08, 2.6, 0.08), "mat_metal")
    caja(m, (-1.5, 2.45, -0.08), (1.5, 2.6, 0.08), "mat_metal")
    for y in (0.35, 1.7):
        caja(m, (-1.4, y, -0.04), (1.4, y + 0.06, 0.04), "mat_metal")
    x = -1.2
    while x < 1.25:
        caja(m, (x - 0.025, 0, -0.025), (x + 0.025, 2.45, 0.025), "mat_metal")
        x += 0.24
    caja(m, (0.25, 1.0, -0.1), (0.5, 1.3, 0.1), "mat_metal")   # candado
    col(m, (-1.5, 0, -0.12), (1.5, 2.6, 0.12))
    return m.objeto("reja", col_)


def cuerda_pozo(col_):
    """Cuerda que baja por el pozo de la aldea y el cubo en el suelo. Origen en el suelo, bajo
    el centro del tiro; la cuerda sube hasta TOPE_POZO."""
    m = rc.Malla()
    caja(m, (-0.03, 0.45, -0.03), (0.03, TOPE_POZO, 0.03), "mat_paja")
    for y in (0.6, 1.3):  # nudos
        caja(m, (-0.05, y, -0.05), (0.05, y + 0.08, 0.05), "mat_paja")
    m.prisma(B(0.35, 0, 0.2), 0.2, 0.24, 0.32, 8, "mat_madera")
    m.prisma(B(0.35, 0.22, 0.2), 0.245, 0.245, 0.04, 8, "mat_metal")
    col(m, (-0.08, 0, -0.08), (0.08, 2.5, 0.08))
    return m.objeto("cuerda_pozo", col_)


def escalera_mano(col_):
    """Escalera de mano que sube a la trampilla (plano XY, frente +Z). Origen en el suelo."""
    m = rc.Malla()
    for dx in (-0.33, 0.33):
        caja(m, (dx - 0.04, 0, -0.04), (dx + 0.04, TOPE_TRAMPILLA - 0.1, 0.04), "mat_madera")
    y = 0.3
    while y < TOPE_TRAMPILLA - 0.3:
        caja(m, (-0.33, y, -0.03), (0.33, y + 0.05, 0.03), "mat_madera")
        y += 0.35
    col(m, (-0.4, 0, -0.12), (0.4, 3.0, 0.12))
    return m.objeto("escalera_mano", col_)


def craneo(m, c, e, ojos="mat_oscuro"):
    """Calavera en c (motor; c es la base) de tamano e (1 = 23 cm de alto), mirando a +Z."""
    cx, cy, cz = c
    m.anillos(B(cx, cy + 0.05 * e, cz),
              [(0.0, 0.07 * e, 0.08 * e), (0.06 * e, 0.095 * e, 0.11 * e),
               (0.13 * e, 0.1 * e, 0.115 * e), (0.19 * e, 0.085 * e, 0.1 * e),
               (0.23 * e, 0.0, 0.0)], 8, "mat_hueso", tapa_arriba=False)
    caja(m, (cx - 0.042 * e, cy + 0.01 * e, cz + 0.035 * e),
         (cx + 0.042 * e, cy + 0.065 * e, cz + 0.092 * e), "mat_hueso")      # mandibula
    frente = cz + 0.112 * e
    for dx in (-0.036, 0.036):
        m.decal(B(cx + dx * e, cy + 0.15 * e, frente), (0, -1, 0), 0.042 * e, 0.046 * e, ojos,
                salida=0.01 * e)
    m.decal(B(cx, cy + 0.105 * e, frente), (0, -1, 0), 0.02 * e, 0.03 * e, "mat_oscuro",
            salida=0.01 * e)                                                # nariz
    m.decal(B(cx, cy + 0.05 * e, cz + 0.092 * e), (0, -1, 0), 0.07 * e, 0.012 * e, "mat_oscuro",
            salida=0.004 * e)                                               # dientes


def hueso_largo(m, p0, p1, r=0.022, nudo=0.038):
    """Hueso largo (femur, tibia) de p0 a p1 (motor) con los extremos abultados."""
    m.segmento(B(*p0), B(*p1), r, r * 0.9, 6, "mat_hueso")
    for p in (p0, p1):
        res = bmesh.ops.create_icosphere(m.bm, subdivisions=1, radius=nudo)
        for v in res["verts"]:
            v.co += B(*p)
        for f in {f for v in res["verts"] for f in v.link_faces}:
            f.material_index = m._indice("mat_hueso")


def osamenta(col_):
    """Restos en un nicho: calavera, dos femures cruzados y huesos sueltos. Largo en X (1.1 m),
    la calavera mira a +Z."""
    m = rc.Malla()
    craneo(m, (-0.42, 0, 0.0), 1.0)
    hueso_largo(m, (-0.18, 0.035, -0.09), (0.5, 0.035, 0.07))
    hueso_largo(m, (-0.12, 0.07, 0.08), (0.55, 0.05, -0.06))
    hueso_largo(m, (0.15, 0.025, 0.12), (0.62, 0.025, 0.1), r=0.016, nudo=0.028)
    for k in range(4):   # costillas: arcos cortos
        z = -0.12 + k * 0.05
        m.segmento(B(0.05, 0.02, z), B(0.2, 0.06, z + 0.02), 0.009, 0.008, 4, "mat_hueso")
        m.segmento(B(0.2, 0.06, z + 0.02), B(0.32, 0.015, z + 0.01), 0.008, 0.007, 4, "mat_hueso")
    col(m, (-0.55, 0, -0.15), (0.65, 0.25, 0.15))
    return m.objeto("osamenta", col_)


def calavera_vela(col_):
    """Calavera grande con una vela que sale por la coronilla y los ojos encendidos por dentro
    (la llama es un sprite y la luz, de la entidad). Base en el origen, mira a +Z."""
    m = rc.Malla()
    e = 1.7
    craneo(m, (0, 0, 0), e, ojos="mat_brillo")
    alto = 0.05 * e + 0.19 * e                       # donde la vela asoma por arriba
    m.prisma(B(0, alto - 0.04, 0), 0.05, 0.045, 0.22, 8, "mat_cera")
    for x, z, largo in ((0.06, 0.05, 0.12), (-0.07, 0.02, 0.09), (0.0, -0.08, 0.14)):
        caja(m, (x - 0.012, alto - largo, z - 0.012), (x + 0.012, alto + 0.01, z + 0.012), "mat_cera")
    col(m, (-0.2, 0, -0.2), (0.2, 0.6, 0.2))
    return m.objeto("calavera_vela", col_)


# Llama de la antorcha de pared, relativa al origen del modelo (para el sprite de la escena)
LLAMA_ANTORCHA = (0.0, 1.78, 0.24)


def antorcha_pared(col_):
    """Antorcha clavada en la pared: la pared queda hacia -Z y la antorcha asoma hacia +Z.
    El brazo de hierro es largo (llega a z=-0.9) para que se meta en la pared aunque la tierra
    deformada quede mas cerca o mas lejos; la llama es un sprite (LLAMA_ANTORCHA)."""
    m = rc.Malla()
    caja(m, (-0.12, 1.0, -0.95), (0.12, 1.3, -0.85), "mat_metal")         # placa (dentro del muro)
    caja(m, (-0.025, 1.12, -0.9), (0.025, 1.17, 0.06), "mat_metal")        # brazo
    m.anillos(B(0, 1.1, 0.06), [(0.0, 0.055, 0.055), (0.08, 0.06, 0.06)], 6, "mat_metal",
              tapa_abajo=False, tapa_arriba=False)                          # argolla
    m.segmento(B(0, 0.85, 0.0), B(0, 1.6, 0.2), 0.03, 0.035, 6, "mat_madera")  # palo inclinado
    m.segmento(B(0, 1.5, 0.17), B(0, 1.66, 0.215), 0.06, 0.055, 6, "mat_corteza")  # trapo quemado
    caja(m, (-0.04, 1.62, 0.18), (0.04, 1.68, 0.25), "mat_oscuro")          # brasa
    return m.objeto("antorcha_pared", col_)


def trampilla(col_):
    """Trampilla de madera en el suelo de la plaza (se pisa: sin colision)."""
    m = rc.Malla()
    caja(m, (-0.65, 0, -0.65), (0.65, 0.04, 0.65), "mat_piedra")          # marco de piedra
    caja(m, (-0.55, 0.04, -0.55), (0.55, 0.07, 0.55), "mat_madera")
    for z in (-0.35, 0.35):
        caja(m, (-0.56, 0.07, z - 0.05), (0.56, 0.09, z + 0.05), "mat_metal")  # bisagras
    caja(m, (-0.1, 0.07, 0.38), (0.1, 0.1, 0.48), "mat_metal")              # argolla
    return m.objeto("trampilla", col_)


# ---------------------------------------------------------------------------
# Tuneles
# ---------------------------------------------------------------------------
LAGO = {(i, j) for i in range(18, 21) for j in range(2, 4)}
SUELO_LAGO = -0.8
POZO = (10, 1)       # celda con el hueco del pozo de la aldea en el techo
TRAMPILLA = (21, 7)  # celda con la trampilla de salida a la plaza
DERRUMBE = [(1, 12), (1, 13)]


def caja_celda(i, j):
    x0 = (i - tm.ORIGEN[0] - 0.5) * S
    z0 = (j - tm.ORIGEN[1] - 0.5) * S
    return x0, x0 + S, z0, z0 + S


def suelo(i, j):
    return SUELO_LAGO if (i, j) in LAGO else 0.0


def techo(i, j):
    return tm.ALTURA[tm.celda(i, j)]


def hueco_central(i, j):
    x0, _, z0, _ = caja_celda(i, j)
    return (x0 + 1, x0 + 2, z0 + 1, z0 + 2)


def tuneles(col_):
    m = rc.Malla()
    rnd = random.Random(5)
    lados = {(0, -1): ("z0", (0, 0, 1)), (0, 1): ("z1", (0, 0, -1)),
             (-1, 0): ("x0", (1, 0, 0)), (1, 0): ("x1", (-1, 0, 0))}

    for i, j in tm.celdas():
        x0, x1, z0, z1 = caja_celda(i, j)
        f, h = suelo(i, j), techo(i, j)
        hueco = None
        if (i, j) == POZO or (i, j) == TRAMPILLA:
            hueco = hueco_central(i, j)
        plano(m, x0, x1, z0, z1, f, "mat_roca" if (i, j) in LAGO else "mat_tierra_suelo", True)
        plano(m, x0, x1, z0, z1, h, "mat_tierra", False, hueco=hueco)

        for (di, dj), (borde, normal) in lados.items():
            if borde == "z0":
                p0, p1 = (x0, z0), (x1, z0)
            elif borde == "z1":
                p0, p1 = (x0, z1), (x1, z1)
            elif borde == "x0":
                p0, p1 = (x0, z0), (x0, z1)
            else:
                p0, p1 = (x1, z0), (x1, z1)
            vecina = (i + di, j + dj)
            if (i, j) == (11, 16) and borde == "z1":
                continue  # la boca de la escalera que sube a la cripta
            if not tm.abierta(*vecina):
                # el osario tiene las paredes hechas de calaveras (rectas: no se deforman)
                mat = "mat_calaveras" if tm.celda(i, j) == "C" else "mat_tierra"
                pared(m, p0, p1, f, h, mat, normal)
                continue
            h2, f2 = techo(*vecina), suelo(*vecina)
            if h2 < h:
                pared(m, p0, p1, h2, h, "mat_tierra", normal)      # escalon del techo
            if f2 > f:
                pared(m, p0, p1, f, f2, "mat_roca", normal)        # orilla del lago

        # entibado: postes y travesano en los tuneles rectos, una celda si y otra no
        if tm.celda(i, j) == "." and (i + j) % 2 == 0:
            n_s = tm.abierta(i, j - 1) and tm.abierta(i, j + 1)
            e_o = tm.abierta(i - 1, j) and tm.abierta(i + 1, j)
            cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
            if n_s and not tm.abierta(i - 1, j) and not tm.abierta(i + 1, j):
                for lado in (-1, 1):
                    caja(m, (cx + lado * 1.2 - 0.11, 0, cz - 0.11), (cx + lado * 1.2 + 0.11, 2.15, cz + 0.11),
                         "mat_madera")
                caja(m, (cx - 1.4, 2.15, cz - 0.13), (cx + 1.4, 2.4, cz + 0.13), "mat_madera")
            elif e_o and not tm.abierta(i, j - 1) and not tm.abierta(i, j + 1):
                for lado in (-1, 1):
                    caja(m, (cx - 0.11, 0, cz + lado * 1.2 - 0.11), (cx + 0.11, 2.15, cz + lado * 1.2 + 0.11),
                         "mat_madera")
                caja(m, (cx - 0.13, 2.15, cz - 1.4), (cx + 0.13, 2.4, cz + 1.4), "mat_madera")

    # --- escalera de piedra que sube a la cripta (sur de la llegada) ---
    zb, ze = 3.0, 7.5
    top0 = tm.ALTURA["A"]
    def techo_esc(z):
        return top0 + (z - zb) * (3.0 / (ze - zb))
    zs = cortes(zb, ze)
    for lado, n in ((-1.5, (1, 0, 0)), (1.5, (-1, 0, 0))):
        for za, zz in zip(zs, zs[1:]):
            ys = cortes(0.0, min(techo_esc(za), techo_esc(zz)), extra=NIVELES)
            for ya, yb in zip(ys, ys[1:]):
                cara(m, [(lado, ya, za), (lado, ya, zz), (lado, yb, zz), (lado, yb, za)], "mat_cripta", n)
            cara(m, [(lado, ys[-1], za), (lado, ys[-1], zz), (lado, techo_esc(zz), zz),
                     (lado, techo_esc(za), za)], "mat_cripta", n)
    xs = cortes(-1.5, 1.5)
    for za, zz in zip(zs, zs[1:]):
        for xa, xb in zip(xs, xs[1:]):
            cara(m, [(xa, techo_esc(za), za), (xb, techo_esc(za), za), (xb, techo_esc(zz), zz),
                     (xa, techo_esc(zz), zz)], "mat_cripta", (0, -1, 0))
    cara(m, [(-1.5, 3.0, ze), (1.5, 3.0, ze), (1.5, techo_esc(ze), ze), (-1.5, techo_esc(ze), ze)],
         "mat_oscuro", (0, 0, -1))
    for k in range(12):
        za = zb + k * HUELLA
        caja(m, (-1.5, 0, za), (1.5, (k + 1) * TABICA, za + HUELLA), "mat_cripta", colision=True)
    col(m, (-2.5, 0, 6.0), (-1.3, 10, 8.5))
    col(m, (1.3, 0, 6.0), (2.5, 10, 8.5))
    col(m, (-1.5, 0, ze), (1.5, 10, ze + 1))

    # --- fondo del pozo de la aldea: tiro de piedra, cuerda y cubo ---
    hx0, hx1, hz0, hz1 = hueco_central(*POZO)
    pozo_vertical(m, hx0, hx1, hz0, hz1, techo(*POZO), TOPE_POZO, "mat_piedra")
    plano(m, hx0, hx1, hz0, hz1, TOPE_POZO, "mat_yeso", False)
    # (la cuerda y el cubo son el modelo cuerda_pozo: se puede usar con E)

    # --- salida a la plaza: tiro corto, trampilla y escalera de mano ---
    tx0, tx1, tz0, tz1 = hueco_central(*TRAMPILLA)
    pozo_vertical(m, tx0, tx1, tz0, tz1, techo(*TRAMPILLA), TOPE_TRAMPILLA, "mat_madera_gris")
    plano(m, tx0, tx1, tz0, tz1, TOPE_TRAMPILLA, "mat_madera", False)
    # (la escalera de mano es el modelo escalera_mano: se puede usar con E)

    # --- lago: agua oscura en el hueco y piedras en la orilla ---
    lx0 = min(caja_celda(i, j)[0] for i, j in LAGO)
    lx1 = max(caja_celda(i, j)[1] for i, j in LAGO)
    lz0 = min(caja_celda(i, j)[2] for i, j in LAGO)
    lz1 = max(caja_celda(i, j)[3] for i, j in LAGO)
    plano(m, lx0, lx1, lz0, lz1, -0.22, "mat_agua", True)
    orilla = ([(x, lz1) for x in cortes(lx0, lx1, 0.7)] + [(lx0, z) for z in cortes(lz0, lz1, 0.7)]
              + [(lx1, z) for z in cortes(lz0, lz1, 0.7)])
    for x, z in orilla:
        r = rnd.uniform(0.25, 0.4)
        m.caja(B(x, r * 0.5, z), (r * 2, r * 2, r * rnd.uniform(0.8, 1.3)), "mat_roca",
               rot=(rnd.uniform(-10, 10), rnd.uniform(-10, 10), rnd.uniform(0, 90)))
    col(m, (lx0 - 0.2, -1, lz0), (lx1 + 0.2, 1.8, lz1 + 0.2))

    # --- derrumbe: rocas amontonadas y una viga partida ---
    dx0 = min(caja_celda(i, j)[0] for i, j in DERRUMBE)
    dx1 = max(caja_celda(i, j)[1] for i, j in DERRUMBE)
    dz0 = min(caja_celda(i, j)[2] for i, j in DERRUMBE)
    dz1 = max(caja_celda(i, j)[3] for i, j in DERRUMBE)
    for _ in range(14):
        r = rnd.uniform(0.5, 1.05)
        x = rnd.uniform(dx0 + 0.4, dx1 - 0.2)
        z = rnd.uniform(dz0 + 0.4, dz1 - 0.4)
        y = rnd.uniform(0.3, 1.6) * r
        res = bmesh.ops.create_icosphere(m.bm, subdivisions=1, radius=r)
        for v in res["verts"]:
            v.co *= rnd.uniform(0.75, 1.15)
            v.co.z *= 0.75
            v.co += B(x, y, z)
        for f in {f for v in res["verts"] for f in v.link_faces}:
            f.material_index = m._indice("mat_roca")
    m.caja(B(dx1 - 0.3, 1.2, (dz0 + dz1) / 2), (0.25, 2.6, 0.25), "mat_madera", rot=(0, 35, 10))
    col(m, (dx0, 0, dz0), (dx1 + 0.1, 3.5, dz1))

    deformar(m)

    # colision: bloques de roca alrededor de lo abierto (filas de celdas juntas, un poco
    # metidos en el tunel para que las paredes deformadas no se atraviesen con la camara)
    margen = 0.2
    for j in range(tm.ALTO):
        i = 0
        while i < tm.ANCHO:
            def borde(ii):
                if tm.abierta(ii, j) or (ii, j) == (11, 17):
                    return False
                return any(tm.abierta(ii + a, j + b) for a in (-1, 0, 1) for b in (-1, 0, 1))
            if not borde(i):
                i += 1
                continue
            k = i
            while k + 1 < tm.ANCHO and borde(k + 1):
                k += 1
            x0 = caja_celda(i, j)[0]
            x1 = caja_celda(k, j)[1]
            _, _, z0, z1 = caja_celda(i, j)
            col(m, (x0 - margen, -1, z0 - margen), (x1 + margen, 13, z1 + margen))
            i = k + 1

    m.bm.normal_update()
    return m.objeto("tuneles", col_)


# ---------------------------------------------------------------------------
PIEZAS = [
    (cripta_interior, "Interior de la cripta. Suelo de la sala a Y=3; la escalera baja por\n"
                      "z<-1.5 hasta un rellano en Y=0 (fin en z=-9.5)."),
    (sarcofago, "Sarcofago de piedra (largo en z)."),
    (atril, "Atril con libro abierto."),
    (puerta_cripta, "Puerta de hierro de la cripta vista desde dentro (frente +Z)."),
    (estatua, "Figura encapuchada sobre pedestal (marmol blanco, cementerio)."),
    (reja, "Reja de hierro para un tunel de 3 m (plano XY)."),
    (cuerda_pozo, "Cuerda del pozo de la aldea y cubo (origen bajo el centro del tiro)."),
    (escalera_mano, "Escalera de mano hasta la trampilla de la plaza."),
    (trampilla, "Trampilla de madera en el suelo (plaza de noche); sin colision."),
    (antorcha_pared, "Antorcha de pared: la pared hacia -Z; llama en " + str(LLAMA_ANTORCHA) + "."),
    (osamenta, "Calavera y huesos para los nichos (largo en X, la calavera mira a +Z)."),
    (calavera_vela, "Calavera con vela; la llama va encima, a 0.58 m (sprite spr_llama)."),
    (tuneles, "Red de tuneles (mapa en tools/blender/tuneles_mapa.py). Origen: centro de la\n"
              "llegada; la escalera a la cripta sube hacia +Z."),
]


def construir():
    sc = rc.escena()
    col_ = rc.coleccion("subterraneo", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col_)
        ob.location.x = 0 if fn is tuneles else -60 - k * 10
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
