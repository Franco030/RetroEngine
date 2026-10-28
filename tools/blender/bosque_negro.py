"""
Modelos del bosque negro (escena bosque_negro.json, al este del lago):

  hombre_alto.obj     figura de 2.7 m, delgadisima, con una tunica negra hecha jirones, brazos
                      que le llegan a las rodillas y una cara alargada y palida sin ojos (dos
                      huecos negros) ni mas boca que un corte. Mira a +Z.
  figura_ramas.obj    figura de ramas atadas con cuerda, de 0.9 m, que se cuelga de un arbol:
                      el ORIGEN es el punto del que cuelga (todo queda por debajo).
  cartel.obj          poste con una tabla torcida y pintura roja que gotea.
  cabana.obj          cabana de troncos de 4 x 3.4 m con la puerta y una ventana al frente (+Z),
                      porche de dos escalones. Colision en las paredes.
  circulo_piedras.obj nueve piedras de pie en un circulo de 2.6 m de radio y una losa en medio
                      con una mancha oscura.
  arbol_caido.obj     tronco enorme atravesado (9 m en X) con raices y ramas: cierra el sendero.

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

from mathutils import Vector  # noqa: E402


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


def hombre_alto(col_):
    m = rc.Malla()
    N = "mat_tela_negra"
    P = "mat_piel_palida"
    rnd = random.Random(5)
    # piernas: dos palos negros larguisimos bajo la tunica
    for lado in (-1, 1):
        m.segmento(B(lado * 0.1, 0, 0.02), B(lado * 0.09, 1.25, 0), 0.05, 0.06, 5, N)
    # tunica: estrecha, larga, el bajo hecho jirones (tiras que cuelgan a distinta altura)
    m.anillos(B(0, 0.75, 0), [(0.0, 0.24, 0.18), (0.6, 0.2, 0.15), (1.1, 0.22, 0.15),
                              (1.35, 0.26, 0.17), (1.5, 0.12, 0.1)], 7, N)
    for k in range(9):
        a = 2 * math.pi * k / 9
        x, z = 0.22 * math.cos(a), 0.16 * math.sin(a)
        largo = rnd.uniform(0.25, 0.6)
        m.segmento(B(x, 0.8, z), B(x * 1.15, 0.8 - largo, z * 1.15), 0.06, 0.02, 3, N, ovalo=(1.0, 0.3))
    # cuello largo y la cabeza alargada, un poco ladeada hacia delante
    m.segmento(B(0, 2.22, 0), B(0.02, 2.42, 0.05), 0.05, 0.045, 5, P)
    m.anillos(B(0.02, 2.4, 0.06), [(0.0, 0.08, 0.09), (0.1, 0.11, 0.12), (0.24, 0.1, 0.11),
                                   (0.34, 0.06, 0.07), (0.38, 0.0, 0.0)], 7, P)
    for dx in (-0.045, 0.045):                                     # ojos: huecos negros
        m.decal(B(0.02 + dx, 2.56, 0.181), (0, -1, 0), 0.05, 0.065, "mat_oscuro", salida=0.004)
    m.decal(B(0.02, 2.47, 0.178), (0, -1, 0), 0.11, 0.012, "mat_oscuro", salida=0.004)  # boca
    # hombros huesudos y brazos que llegan a las rodillas, con dedos larguisimos
    for lado in (-1, 1):
        hombro = (lado * 0.26, 2.12, 0.0)
        codo = (lado * 0.36, 1.45, 0.08)
        mano = (lado * 0.33, 0.85, 0.12)
        m.segmento(B(*hombro), B(*codo), 0.05, 0.04, 5, N)
        m.segmento(B(*codo), B(*mano), 0.04, 0.03, 5, P)
        for k in range(4):
            dx = (k - 1.5) * 0.018
            m.segmento(B(mano[0] + dx, mano[1], mano[2]),
                       B(mano[0] + dx * 1.6 + lado * 0.01, mano[1] - 0.24 - 0.02 * (k % 2), mano[2] + 0.03),
                       0.009, 0.004, 3, P)
    col(m, (-0.35, 0, -0.25), (0.35, 2.7, 0.25))
    return m.objeto("hombre_alto", col_)


def figura_ramas(col_):
    m = rc.Malla()
    R = "mat_corteza"
    C = "mat_lona"
    m.segmento(B(0, 0, 0), B(0, -0.12, 0), 0.008, 0.008, 3, C)         # cuerda de la que cuelga
    m.segmento(B(0, -0.1, 0), B(0, -0.9, 0), 0.02, 0.016, 4, R)        # tronco
    m.segmento(B(-0.3, -0.3, 0), B(0.3, -0.3, 0), 0.016, 0.016, 4, R)  # brazos
    m.segmento(B(-0.28, -0.32, 0), B(-0.4, -0.55, 0.02), 0.012, 0.008, 3, R)
    m.segmento(B(0.28, -0.32, 0), B(0.38, -0.58, -0.02), 0.012, 0.008, 3, R)
    m.segmento(B(0, -0.62, 0), B(-0.18, -0.95, 0.02), 0.014, 0.01, 3, R)  # piernas
    m.segmento(B(0, -0.62, 0), B(0.2, -0.97, -0.02), 0.014, 0.01, 3, R)
    m.anillos(B(0, -0.24, 0), [(0.0, 0.06, 0.06), (0.06, 0.08, 0.08), (0.14, 0.0, 0.0)], 5, R)  # cabeza
    for y in (-0.3, -0.62):                                             # ataduras
        m.anillos(B(0, y - 0.025, 0), [(0.0, 0.03, 0.03), (0.05, 0.03, 0.03)], 5, C)
    return m.objeto("figura_ramas", col_)


def cartel(col_):
    m = rc.Malla()
    m.prisma(B(0, 0, 0), 0.07, 0.06, 1.7, 5, "mat_madera")
    caja(m, (-0.55, 1.15, 0.05), (0.55, 1.6, 0.1), "mat_madera_gris", rot=(0, -6, 0))
    for x, largo in ((-0.35, 0.3), (-0.1, 0.42), (0.15, 0.22), (0.38, 0.35)):  # pintura que gotea
        m.decal(B(x, 1.45 - largo / 2, 0.1), (0, -1, 0), 0.04, largo, "mat_sangre", salida=0.008)
    m.decal(B(0, 1.47, 0.1), (0, -1, 0), 0.9, 0.08, "mat_sangre", salida=0.007)
    col(m, (-0.1, 0, -0.1), (0.1, 1.6, 0.1))
    return m.objeto("cartel", col_)


def cabana(col_):
    """Paredes macizas (cajas cerradas: se ven bien por fuera y por dentro), troncos en relieve,
    esquinas cruzadas, hastiales de troncos cada vez mas cortos y tejado de tablas."""
    m = rc.Malla()
    W = "mat_madera"
    G = 0.14                                 # grosor de pared
    ax, az = 2.0, 1.7                        # media anchura (x) y media profundidad (z)
    alto = 2.4
    # paredes: atras y lados enteros; delante con la puerta (x -0.45..0.45, hasta 2.0) y la
    # ventana (x 0.9..1.6, y 1.0..1.8)
    caja(m, (-ax, 0, -az), (ax, alto, -az + G), W)
    caja(m, (-ax, 0, -az), (-ax + G, alto, az), W)
    caja(m, (ax - G, 0, -az), (ax, alto, az), W)
    zf = (az - G, az)
    for lo, hi in (((-ax, 0), (-0.45, alto)), ((-0.45, 2.0), (0.45, alto)), ((0.45, 0), (ax, 1.0)),
                   ((0.45, 1.8), (ax, alto)), ((0.45, 1.0), (0.9, 1.8)), ((1.6, 1.0), (ax, 1.8))):
        caja(m, (lo[0], lo[1], zf[0]), (hi[0], hi[1], zf[1]), W)
    # troncos en relieve por fuera (no en los huecos) y esquinas que se cruzan
    y = 0.15
    while y < alto:
        caja(m, (-ax - 0.03, y, -az - 0.03), (ax + 0.03, y + 0.07, -az), "mat_corteza")
        for x0 in (-ax - 0.03, ax):
            caja(m, (x0, y, -az - 0.03), (x0 + 0.03, y + 0.07, az + 0.03), "mat_corteza")
        tramos = [(-ax - 0.03, -0.45)]
        if y >= 2.0:
            tramos = [(-ax - 0.03, ax + 0.03)]
        elif 1.0 <= y < 1.8:
            tramos += [(0.45, 0.9), (1.6, ax + 0.03)]
        else:
            tramos += [(0.45, ax + 0.03)]
        for x0, x1 in tramos:
            caja(m, (x0, y, az), (x1, y + 0.07, az + 0.03), "mat_corteza")
        for sx in (-1, 1):
            for sz in (-1, 1):
                caja(m, (sx * ax - 0.09, y - 0.05, sz * az - 0.09), (sx * ax + 0.09, y + 0.12, sz * az + 0.09),
                     "mat_corteza")
        y += 0.3
    # puerta (cerrada, algo hundida) y ventana con su cruz; la luz de dentro la pone la entidad
    caja(m, (-0.45, 0, az - 0.1), (0.45, 2.0, az - 0.06), "mat_puerta")
    caja(m, (0.9, 1.0, az - 0.1), (1.6, 1.8, az - 0.07), "mat_ventana_luz")
    caja(m, (1.22, 1.0, az - 0.07), (1.28, 1.8, az - 0.03), W)
    caja(m, (0.9, 1.37, az - 0.07), (1.6, 1.43, az - 0.03), W)
    # hastiales: troncos cada vez mas cortos hasta la cumbrera
    cumbre = 1.3
    y = alto
    while y < alto + cumbre - 0.05:
        half = ax * (1 - (y + 0.1 - alto) / cumbre)
        for z0, z1 in ((-az, -az + G), (az - G, az)):
            caja(m, (-half, y, z0), (half, y + 0.2, z1), W)
        y += 0.2
    # tejado: dos aguas de tablas (cajas) con alero
    ang = math.degrees(math.atan2(cumbre, ax))
    largo = math.hypot(ax, cumbre) + 0.35
    for lado in (-1, 1):
        cx = lado * (ax / 2 + 0.12)
        cy = alto + cumbre / 2 + 0.08
        m.caja(B(cx, cy, 0), (largo, 2 * az + 0.6, 0.08), "mat_madera_gris", rot=(0, lado * ang, 0))
    # porche: dos escalones
    caja(m, (-0.8, 0, az), (0.8, 0.12, az + 0.6), "mat_madera_gris")
    caja(m, (-0.8, 0, az + 0.6), (0.8, 0.06, az + 0.9), "mat_madera_gris")
    col(m, (-ax, 0, -az), (ax, alto, -az + G))
    col(m, (-ax, 0, -az), (-ax + G, alto, az))
    col(m, (ax - G, 0, -az), (ax, alto, az))
    col(m, (-ax, 0, az - G), (ax, alto, az))
    return m.objeto("cabana", col_)


def corazon(m, base):
    """Corazon tumbado sobre la losa: cuerpo en gota (dos lobulos arriba y la punta abajo, que
    apunta a +Z) y los vasos cortados que salen por arriba."""
    bx, by, bz = base
    # el cuerpo: anillos a lo largo de Z (tumbado), con la punta hacia delante
    for dx in (-0.045, 0.045):                       # dos lobulos
        m.anillos(B(bx + dx, by, bz - 0.06), [(0.0, 0.0, 0.0), (0.03, 0.07, 0.06), (0.09, 0.08, 0.075),
                                             (0.16, 0.05, 0.05), (0.2, 0.0, 0.0)], 7, "mat_tela_roja",
                  tapa_abajo=False, tapa_arriba=False)
    m.segmento(B(bx, by + 0.06, bz - 0.04), B(bx, by + 0.04, bz + 0.17), 0.1, 0.012, 7, "mat_tela_roja")
    for dx, dz, alto in ((-0.03, -0.08, 0.12), (0.04, -0.06, 0.09), (0.0, -0.12, 0.07)):   # vasos
        m.segmento(B(bx + dx, by + 0.1, bz + dz), B(bx + dx * 1.6, by + 0.1 + alto, bz + dz - 0.03),
                   0.022, 0.018, 5, "mat_sangre")


def circulo_piedras(col_):
    m = rc.Malla()
    rnd = random.Random(9)
    for k in range(9):
        a = 2 * math.pi * k / 9 + rnd.uniform(-0.08, 0.08)
        x, z = 2.6 * math.cos(a), 2.6 * math.sin(a)
        alto = rnd.uniform(1.0, 1.7)
        m.prisma(B(x, 0, z), 0.32, 0.2, alto, 5, "mat_granito", giro=math.degrees(a),
                 rot=(rnd.uniform(-6, 6), rnd.uniform(-6, 6), 0))
        col(m, (x - 0.3, 0, z - 0.3), (x + 0.3, alto, z + 0.3))
    caja(m, (-0.7, 0, -0.45), (0.7, 0.42, 0.45), "mat_granito")
    # charco de sangre sobre la losa (un poligono irregular mirando arriba)
    charco = [(0.42 * math.cos(2 * math.pi * k / 9) * rnd.uniform(0.7, 1.15),
               0.3 * math.sin(2 * math.pi * k / 9) * rnd.uniform(0.7, 1.15)) for k in range(9)]
    f = m.poligono([B(0.1 + x, 0.425, z) for x, z in charco], "mat_sangre")
    f.normal_update()
    if f.normal.z < 0:
        f.normal_flip()
    corazon(m, (0.12, 0.425, 0.0))
    col(m, (-0.7, 0, -0.45), (0.7, 0.42, 0.45))
    return m.objeto("circulo_piedras", col_)


def arbol_caido(col_):
    m = rc.Malla()
    m.segmento(B(-4.6, 0.55, 0.1), B(4.4, 0.75, -0.2), 0.55, 0.38, 8, "mat_corteza")
    rnd = random.Random(4)
    for k in range(7):                                                # raices al extremo izquierdo
        a = 2 * math.pi * k / 7
        m.segmento(B(-4.6, 0.55, 0.1), B(-5.3, 0.55 + 1.0 * math.sin(a), 0.1 + 1.0 * math.cos(a)),
                   0.14, 0.03, 4, "mat_corteza")
    for k in range(6):                                                # ramas secas al otro
        x = rnd.uniform(0.5, 4.2)
        m.segmento(B(x, 0.75, -0.1), B(x + rnd.uniform(0.3, 1.0), rnd.uniform(1.4, 2.6),
                                       rnd.uniform(-1.2, 1.2)), 0.1, 0.02, 4, "mat_corteza")
    caja(m, (-0.4, 0, -0.3), (0.4, 0.3, 0.4), "mat_tierra_suelo")      # tierra levantada
    col(m, (-4.8, 0, -0.6), (4.6, 1.6, 0.6))
    return m.objeto("arbol_caido", col_)


PIEZAS = [
    (hombre_alto, "Figura alta (2.7 m) de tunica negra y cara palida sin ojos. Mira a +Z."),
    (figura_ramas, "Figura de ramas de 0.9 m. El origen es el punto del que cuelga."),
    (cartel, "Poste con tabla y pintura roja. Frente a +Z."),
    (cabana, "Cabana de 4 x 3.4 m, puerta y ventana al frente (+Z); ventana en x 0.9..1.6, y 1..1.8."),
    (circulo_piedras, "Nueve piedras en circulo (r 2.6 m) y una losa con mancha en medio."),
    (arbol_caido, "Tronco caido de 9 m en X que cierra el sendero."),
]


def construir():
    sc = rc.escena()
    col_ = rc.coleccion("bosque_negro", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col_)
        ob.location = (600 + k * 8, 600, 0)
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
