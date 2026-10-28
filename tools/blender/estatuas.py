"""
Estatuas con forma propia (las del cementerio siguen usando estatua.obj de subterraneo.py):

  estatua_dolorosa.obj  iglesia: mujer de marmol con velo, de pie, la cabeza alzada, un halo de
                        metal detras, las manos sobre un corazon atravesado por un punal y
                        lagrimas rojas. Sobre un pedestal octogonal de marmol viejo. 2.9 m.
  estatua_orante.obj    tuneles: figura encapuchada ARRODILLADA sobre una roca, con las manos
                        juntas a la altura de la cara y un rosario de hueso que cuelga. Piedra
                        gastada con grietas. 1.6 m.

Coordenadas del MOTOR (x derecha, y arriba, z hacia el jugador) pasadas a Blender con B().
Base en el origen, frente hacia +Z.

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402

importlib.reload(rc)

from mathutils import Vector  # noqa: E402


def B(x, y, z):
    """Motor -> Blender."""
    return Vector((x, -z, y))


def col(m, lo, hi):
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    t = [b - a for a, b in zip(lo, hi)]
    m.colision(tuple(B(*c)), (t[0], t[2], t[1]))


def halo(m, centro, radio, grosor, piezas, mat):
    """Aro vertical (mirando a +Z) hecho de segmentos cortos."""
    cx, cy, cz = centro
    pts = [(cx + radio * math.cos(2 * math.pi * k / piezas),
            cy + radio * math.sin(2 * math.pi * k / piezas), cz) for k in range(piezas)]
    for a, b in zip(pts, pts[1:] + pts[:1]):
        m.segmento(B(*a), B(*b), grosor, grosor, 4, mat)
    # rayos cortos hacia fuera, cada dos piezas
    for k in range(0, piezas, 2):
        ang = 2 * math.pi * (k + 0.5) / piezas
        p0 = (cx + radio * math.cos(ang), cy + radio * math.sin(ang), cz)
        p1 = (cx + radio * 1.28 * math.cos(ang), cy + radio * 1.28 * math.sin(ang), cz)
        m.segmento(B(*p0), B(*p1), grosor * 0.8, 0.004, 4, mat)


def estatua_dolorosa(col_):
    m = rc.Malla()
    P = "mat_marmol_viejo"
    M = "mat_marmol"
    # pedestal octogonal en dos cuerpos
    m.prisma(B(0, 0, 0), 0.62, 0.6, 0.18, 8, P, giro=22.5)
    m.prisma(B(0, 0.18, 0), 0.48, 0.46, 0.5, 8, P, giro=22.5)
    m.prisma(B(0, 0.68, 0), 0.56, 0.56, 0.08, 8, P, giro=22.5)
    y0 = 0.76
    # manto: cae desde el velo hasta el pedestal, abierto en abanico abajo
    m.anillos(B(0, y0, -0.02), [(0.0, 0.44, 0.36), (0.35, 0.36, 0.3), (0.8, 0.28, 0.24),
                               (1.2, 0.26, 0.22), (1.45, 0.29, 0.24), (1.58, 0.24, 0.2),
                               (1.66, 0.12, 0.12)], 10, M)
    # borde rojo del manto: dos bandas que bajan por delante
    for lado in (-1, 1):
        m.segmento(B(lado * 0.2, y0 + 1.5, 0.17), B(lado * 0.36, y0 + 0.05, 0.3), 0.045, 0.05, 4,
                   "mat_tela_roja", ovalo=(1.0, 0.3))
    # cabeza alzada hacia el cielo y velo que la envuelve
    cab = (0, y0 + 1.7, 0.05)
    m.anillos(B(*cab), [(0.0, 0.1, 0.11), (0.08, 0.13, 0.14), (0.2, 0.12, 0.13),
                        (0.28, 0.0, 0.0)], 8, M, tapa_arriba=False)
    # velo: centrado detras de la cabeza para que la cara quede al descubierto
    m.anillos(B(cab[0], cab[1] - 0.06, cab[2] - 0.17),
              [(0.0, 0.21, 0.15), (0.18, 0.2, 0.16), (0.32, 0.16, 0.13), (0.4, 0.0, 0.0)],
              8, M, tapa_arriba=False)
    m.caja(B(0, cab[1] + 0.1, cab[2] + 0.14), (0.03, 0.05, 0.04), M)              # nariz
    # rostro: ojos en sombra y lagrimas rojas que bajan por las mejillas
    for dx in (-0.045, 0.045):
        m.decal(B(dx, cab[1] + 0.15, cab[2] + 0.145), (0, -1, 0), 0.04, 0.02, "mat_oscuro",
                salida=0.004)
        m.decal(B(dx, cab[1] + 0.07, cab[2] + 0.148), (0, -1, 0), 0.016, 0.13, "mat_sangre",
                salida=0.006)
    # halo de metal detras de la cabeza
    halo(m, (0, cab[1] + 0.16, cab[2] - 0.22), 0.3, 0.018, 16, "mat_metal")
    # corazon atravesado sobre el pecho y manos que lo sujetan
    pecho = (0, y0 + 1.22, 0.27)
    m.caja(B(*pecho), (0.13, 0.08, 0.14), "mat_sangre", rot=(0, 45, 0))
    m.segmento(B(-0.2, pecho[1] + 0.2, pecho[2] + 0.12), B(0.16, pecho[1] - 0.14, pecho[2] - 0.02),
               0.012, 0.004, 4, "mat_metal")                                       # punal
    m.caja(B(-0.19, pecho[1] + 0.19, pecho[2] + 0.12), (0.09, 0.02, 0.02), "mat_metal",
           rot=(0, 45, 0))                                                            # guarda
    for lado in (-1, 1):
        hombro = (lado * 0.24, y0 + 1.48, 0.02)
        codo = (lado * 0.3, y0 + 1.12, 0.18)
        mano = (lado * 0.09, y0 + 1.2, 0.32)
        m.segmento(B(*hombro), B(*codo), 0.07, 0.06, 6, M)
        m.segmento(B(*codo), B(*mano), 0.06, 0.045, 6, M)
        m.caja(B(*mano), (0.06, 0.05, 0.1), M)
    col(m, (-0.62, 0, -0.62), (0.62, 2.9, 0.62))
    return m.objeto("estatua_dolorosa", col_)


def estatua_orante(col_):
    m = rc.Malla()
    G = "mat_piedra_gastada"
    # roca de base, baja e irregular
    m.prisma(B(0, 0, 0), 0.7, 0.58, 0.22, 7, "mat_roca", giro=11)
    m.prisma(B(0.08, 0.22, -0.04), 0.5, 0.42, 0.08, 7, "mat_roca", giro=40)
    y0 = 0.3
    # piernas dobladas: muslo de la rodilla a la cadera y espinilla apoyada hacia atras
    for lado in (-1, 1):
        rodilla = (lado * 0.15, y0 + 0.08, 0.22)
        cadera = (lado * 0.13, y0 + 0.5, -0.08)
        pie = (lado * 0.13, y0 + 0.06, -0.38)
        m.segmento(B(*rodilla), B(*cadera), 0.1, 0.12, 6, G)
        m.segmento(B(*rodilla), B(*pie), 0.085, 0.06, 6, G)
    # capa: cubre desde la roca hasta los hombros, mas ancha atras (se arrastra)
    # (centrada atras: las rodillas asoman por delante y se lee que esta arrodillado)
    m.anillos(B(0, y0, -0.24), [(0.0, 0.42, 0.4), (0.25, 0.35, 0.32), (0.55, 0.3, 0.24),
                               (0.85, 0.27, 0.2), (1.02, 0.24, 0.18), (1.1, 0.14, 0.12)], 9, G)
    # capucha inclinada hacia delante (la cabeza agachada) con el hueco de la cara en sombra
    cap = (0, y0 + 1.04, -0.06)
    m.anillos(B(*cap), [(0.0, 0.17, 0.18), (0.1, 0.2, 0.21), (0.22, 0.18, 0.19),
                        (0.32, 0.1, 0.12), (0.38, 0.0, 0.0)], 8, G, giro=0)
    m.decal(B(0, cap[1] + 0.12, cap[2] + 0.2), (0, -1, 0), 0.16, 0.18, "mat_oscuro",
            salida=0.004)
    # brazos: manos juntas delante de la cara
    manos = (0, y0 + 1.06, 0.3)
    for lado in (-1, 1):
        hombro = (lado * 0.22, y0 + 0.98, -0.12)
        codo = (lado * 0.2, y0 + 0.72, 0.24)
        m.segmento(B(*hombro), B(*codo), 0.075, 0.065, 6, G)
        m.segmento(B(*codo), B(lado * 0.03, manos[1], manos[2]), 0.06, 0.045, 6, G)
    m.caja(B(*manos), (0.08, 0.14, 0.07), G)
    # rosario de hueso: cuentas que cuelgan de las manos
    for k in range(9):
        t = k / 8
        x = 0.06 * math.sin(t * math.pi)
        y = manos[1] - 0.06 - 0.42 * t
        z = manos[2] + 0.04 - 0.06 * math.sin(t * math.pi)
        m.caja(B(x, y, z), (0.03, 0.03, 0.03), "mat_hueso", rot=(0, 45 * k, 0))
    m.caja(B(0.06, manos[1] - 0.5, manos[2] - 0.02), (0.05, 0.012, 0.012), "mat_hueso")
    m.caja(B(0.06, manos[1] - 0.5, manos[2] - 0.02), (0.012, 0.012, 0.08), "mat_hueso")
    # grietas por la capa
    for x, y, ang in ((0.2, 0.85, -0.2), (0.05, 0.4, 0.05)):
        m.decal(B(x, y0 + y, 0.3 - 0.1 * y), (0, -1, 0), 0.015, 0.22, "mat_oscuro",
                salida=0.035)
    col(m, (-0.6, 0, -0.6), (0.6, 1.6, 0.6))
    return m.objeto("estatua_orante", col_)


PIEZAS = [
    (estatua_dolorosa, "Dolorosa de la iglesia: marmol, halo de metal, corazon atravesado y\n"
                       "lagrimas rojas. Base en el origen, mira a +Z."),
    (estatua_orante, "Orante arrodillado de los tuneles: piedra gastada, capucha y rosario de\n"
                     "hueso. Base en el origen, mira a +Z."),
]


def construir():
    sc = rc.escena()
    col_ = rc.coleccion("estatuas", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col_)
        ob.location.x = k * 2.0
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
    construir()
