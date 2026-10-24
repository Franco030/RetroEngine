"""
Modelos de noche y de cementerio para RetroEngine: capilla, cripta, tumba, lapida, cruz,
verja de hierro, farol, banco y fuente. Varios materiales con textura propia: en la escena NO
se pone "texture".

  Base en el origen, frente hacia +Z del motor (-Y en Blender).
  farol: la luz va aparte, como "light" de la entidad, con offset [0, 2.75, 0].
  fuente: con agua en la taza grande y en la pequena.

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402

importlib.reload(rc)

# reutiliza tejados, hastiales y ventanas de las casas de la aldea
import aldea_props as ap  # noqa: E402

importlib.reload(ap)

from mathutils import Vector  # noqa: E402

FRENTE, ATRAS, IZQ, DER = ap.FRENTE, ap.ATRAS, ap.IZQ, ap.DER


def taza(m, centro, r_ext, r_int, alto, nivel_agua, lados=8):
    """Recipiente hueco de piedra (pila de fuente): pared exterior, borde, pared interior y el
    agua por debajo del borde. Sin caras superpuestas (evita el z-fighting)."""
    cx, cy, cz = centro
    m.prisma((cx, cy, cz), r_ext, r_ext, alto, lados, "mat_piedra", giro=180 / lados,
             tapas=False)
    m.anillos((cx, cy, cz), [(alto, r_ext, r_ext), (alto, r_int, r_int),
                             (nivel_agua, r_int, r_int)], lados, "mat_piedra",
              tapa_abajo=False, tapa_arriba=False)
    agua = [(cx + r_int * math.cos(math.radians(180 / lados + 360 * k / lados - 90)),
             cy + r_int * math.sin(math.radians(180 / lados + 360 * k / lados - 90)),
             cz + nivel_agua) for k in range(lados)]
    m.poligono(agua, "mat_agua")


# ---------------------------------------------------------------------------
def capilla(col):
    m = rc.Malla()
    W, D, H = 4.6, 7.0, 3.4
    m.caja((0, 0, 0.15), (W + 0.4, D + 0.4, 0.3), "mat_piedra")         # zocalo
    m.caja((0, 0, 0.3 + H / 2), (W, D, H), "mat_piedra")
    z_alero = 0.3 + H
    ap.hastiales(m, W, D, z_alero, 2.4, "mat_piedra", eje="y")
    cumbre = ap.tejado(m, W, D, z_alero, 2.4, "mat_tejas", vuelo=0.4, vuelo_lado=0.35,
                       eje="y")
    # contrafuertes en los laterales
    for y in (-2.2, 0.0, 2.2):
        for lado in (-1, 1):
            m.caja((lado * (W / 2 + 0.2), y, 1.4), (0.4, 0.5, 2.8), "mat_piedra")
            m.caja((lado * (W / 2 + 0.12), y, 2.95), (0.28, 0.5, 0.4), "mat_piedra",
                   rot=(0, lado * 25, 0))
    # espadana: muro con hueco y campana sobre el hastial del frente
    yf = -D / 2 - 0.05
    m.caja((0, yf, cumbre + 0.6), (1.3, 0.35, 1.4), "mat_piedra")
    m.decal((0, yf - 0.18, cumbre + 0.55), FRENTE, 0.7, 0.8, "mat_oscuro", salida=0.0)
    m.prisma((0, yf - 0.2, cumbre + 0.3), 0.28, 0.12, 0.45, 8, "mat_metal")
    m.caja((0, yf, cumbre + 1.38), (1.5, 0.45, 0.16), "mat_piedra")
    # puerta con arco de piedra, peldanos y ventanas encendidas
    m.decal((0, -D / 2, 0.3 + 1.15), FRENTE, 1.4, 2.3, "mat_puerta")
    m.caja((0, -D / 2 - 0.06, 0.3 + 2.45), (1.9, 0.25, 0.3), "mat_piedra")
    m.caja((0, -D / 2 - 0.45, 0.1), (2.2, 0.8, 0.2), "mat_piedra")
    m.caja((0, -D / 2 - 0.3, 0.25), (1.8, 0.5, 0.1), "mat_piedra")
    m.decal((0, -D / 2, z_alero + 0.9), FRENTE, 0.8, 0.8, "mat_ventana_luz")   # oculo
    for y in (-1.1, 1.1):
        for normal, x in ((DER, W / 2), (IZQ, -W / 2)):
            m.decal((x, y, 2.2), normal, 0.55, 1.4, "mat_ventana_luz")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 1.85), (W + 0.4, D + 0.4, 3.7))
    for y in (-2.2, 0.0, 2.2):
        for lado in (-1, 1):
            m.colision((lado * (W / 2 + 0.2), y, 1.4), (0.4, 0.5, 2.8))  # contrafuertes
    m.colision((0, -D / 2 - 0.45, 0.1), (2.2, 0.8, 0.2))  # peldano (se sube)
    return m.objeto("capilla", col)


def cripta(col):
    """Panteon pequeno con frontispicio y dos columnas; puerta de hierro."""
    m = rc.Malla()
    W, D, H = 2.8, 3.4, 2.4
    m.caja((0, 0, 0.12), (W + 0.5, D + 0.5, 0.24), "mat_piedra")
    m.caja((0, 0.2, 0.24 + H / 2), (W, D - 0.4, H), "mat_piedra")
    z = 0.24 + H
    ap.hastiales(m, W + 0.2, D, z, 0.9, "mat_piedra", eje="y")
    ap.tejado(m, W + 0.2, D, z, 0.9, "mat_piedra", grosor=0.18, vuelo=0.2, vuelo_lado=0.15,
              eje="y")
    for x in (-1.05, 1.05):
        m.prisma((x, -D / 2 + 0.2, 0.24), 0.17, 0.15, H, 8, "mat_piedra")
        m.caja((x, -D / 2 + 0.2, z - 0.08), (0.42, 0.42, 0.16), "mat_piedra")
    m.caja((0, -D / 2 + 0.2, z + 0.06), (W + 0.25, 0.45, 0.14), "mat_piedra")  # dintel
    m.decal((0, -D / 2 + 0.4, 0.24 + 1.0), FRENTE, 1.1, 2.0, "mat_metal")       # puerta
    m.caja((0, -D / 2 + 0.36, 0.24 + 1.05), (0.5, 0.06, 0.08), "mat_metal")     # cerrojo
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 0.12), (W + 0.5, D + 0.5, 0.24))   # base (se sube)
    m.colision((0, 0.2, 0.24 + H / 2), (W, D - 0.4, H))
    for x in (-1.05, 1.05):
        m.colision((x, -D / 2 + 0.2, 0.24 + H / 2), (0.36, 0.36, H))  # columnas
    return m.objeto("cripta", col)


def lapida(m, x=0.0, y=0.0, inclinacion=0.0):
    """Lapida de remate redondo (para reutilizar dentro de otros modelos)."""
    rot = (inclinacion, 0, 0)
    base = Vector((x, y, 0))
    m.caja(base + Vector((0, 0, 0.06)), (0.8, 0.35, 0.12), "mat_piedra")
    m.caja(base + Vector((0, 0, 0.12 + 0.35)), (0.6, 0.14, 0.7), "mat_piedra", rot=rot)
    m.prisma(base + Vector((0, 0.07, 0.12 + 0.7)), 0.3, 0.3, 0.14, 8, "mat_piedra",
             rot=(90 + inclinacion, 0, 0), giro=22.5)


def tumba(col):
    """Lapida con la losa de la sepultura delante (hacia +Z del motor)."""
    m = rc.Malla()
    lapida(m, 0, 0.9)
    m.caja((0, 0, 0.08), (0.9, 1.7, 0.16), "mat_piedra")
    m.caja((0, 0, 0.17), (0.75, 1.5, 0.04), "mat_piedra")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0.9, 0.5), (0.8, 0.35, 1.0))           # lapida
    m.colision((0, 0, 0.1), (0.9, 1.7, 0.2))              # losa (se sube)
    return m.objeto("tumba", col)


def lapida_sola(col):
    m = rc.Malla()
    lapida(m, inclinacion=-6.0)
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 0.5), (0.8, 0.35, 1.0))
    return m.objeto("lapida", col)


def cruz(col):
    m = rc.Malla()
    m.prisma((0, 0, 0), 0.4, 0.34, 0.25, 4, "mat_piedra", giro=45)
    m.caja((0, 0, 0.25 + 0.75), (0.17, 0.17, 1.5), "mat_piedra")
    m.caja((0, 0, 0.25 + 1.12), (0.8, 0.17, 0.17), "mat_piedra")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 0.12), (0.8, 0.8, 0.25))            # peana (se sube)
    m.colision((0, 0, 1.0), (0.2, 0.2, 1.5))
    m.colision((0, 0, 1.37), (0.8, 0.2, 0.2))             # brazos
    return m.objeto("cruz", col)


def verja(col):
    """Tramo de verja de 2.4 m a lo largo de X: dos pilares de piedra y barrotes con punta."""
    m = rc.Malla()
    for x in (-1.2, 1.2):
        m.caja((x, 0, 0.7), (0.34, 0.34, 1.4), "mat_piedra")
        m.prisma((x, 0, 1.4), 0.3, 0.0, 0.3, 4, "mat_piedra", giro=45)
    for z in (0.25, 1.15):
        m.caja((0, 0, z), (2.1, 0.05, 0.06), "mat_metal")
    n = 13
    for i in range(n):
        x = -0.98 + i * 1.96 / (n - 1)
        m.caja((x, 0, 0.75), (0.035, 0.035, 1.5), "mat_metal")
        m.prisma((x, 0, 1.5), 0.05, 0.0, 0.14, 4, "mat_metal", giro=45)
    # colision: solo lo que estorba a la altura del jugador
    for x in (-1.2, 1.2):
        m.colision((x, 0, 0.8), (0.34, 0.34, 1.6))         # pilares
    m.colision((0, 0, 0.8), (2.1, 0.1, 1.6))              # barrotes
    return m.objeto("verja", col)


def farol(col):
    """Farola de hierro con un farol de cristales encendidos arriba (luz a 2.75 m)."""
    m = rc.Malla()
    m.prisma((0, 0, 0), 0.22, 0.18, 0.35, 8, "mat_piedra", giro=22.5)
    m.prisma((0, 0, 0.35), 0.06, 0.045, 2.1, 8, "mat_metal", giro=22.5)
    m.prisma((0, 0, 2.45), 0.13, 0.09, 0.08, 4, "mat_metal", giro=45)
    # farol: cuatro cristales, marco y tejadillo
    z0, z1, r = 2.53, 2.95, 0.15
    for normal in (FRENTE, ATRAS, IZQ, DER):
        n = Vector(normal)
        m.decal(n * r + Vector((0, 0, (z0 + z1) / 2)), n, 2 * r - 0.04, z1 - z0,
                "mat_ventana_luz", salida=0.0)
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.caja((sx * r, sy * r, (z0 + z1) / 2), (0.035, 0.035, z1 - z0), "mat_metal")
    m.prisma((0, 0, z1), 0.25, 0.0, 0.22, 4, "mat_metal", giro=45)
    m.prisma((0, 0, z1 + 0.22), 0.03, 0.03, 0.08, 6, "mat_metal")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 0.18), (0.44, 0.44, 0.35))
    m.colision((0, 0, 1.3), (0.14, 0.14, 2.6))
    return m.objeto("farol", col)


def banco(col):
    m = rc.Malla()
    m.caja((0, 0, 0.45), (1.6, 0.45, 0.07), "mat_madera")                # asiento
    m.caja((0, 0.22, 0.8), (1.6, 0.06, 0.32), "mat_madera", rot=(-8, 0, 0))  # respaldo
    for x in (-0.65, 0.65):
        m.caja((x, 0, 0.21), (0.08, 0.4, 0.42), "mat_metal")
        m.caja((x, 0.22, 0.6), (0.08, 0.06, 0.5), "mat_metal")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0.05, 0.4), (1.6, 0.5, 0.8))
    return m.objeto("banco", col)


def fuente(col):
    m = rc.Malla()
    m.prisma((0, 0, 0), 2.3, 2.3, 0.12, 8, "mat_piedra", giro=22.5)       # escalon
    taza(m, (0, 0, 0.12), 2.0, 1.75, 0.6, 0.42)
    m.prisma((0, 0, 0.12), 0.32, 0.26, 1.5, 8, "mat_piedra", giro=22.5)    # columna
    taza(m, (0, 0, 1.62), 0.7, 0.55, 0.22, 0.14)
    m.prisma((0, 0, 1.84), 0.12, 0.05, 0.45, 8, "mat_piedra", giro=22.5)   # remate
    # colision: solo lo que estorba a la altura del jugador
    m.octogono((0, 0, 0), 2.3 * math.cos(math.radians(22.5)), 0.12)  # escalon (se sube)
    m.octogono((0, 0, 0), 2.0 * math.cos(math.radians(22.5)), 0.72)  # pila
    return m.objeto("fuente", col)


MODELOS = [
    ("capilla", capilla, "Capilla de piedra con espadana, campana y ventanas encendidas."),
    ("cripta", cripta, "Panteon con columnas y puerta de hierro."),
    ("tumba", tumba, "Lapida con la losa de la sepultura hacia +Z."),
    ("lapida", lapida_sola, "Lapida suelta, algo inclinada."),
    ("cruz", cruz, "Cruz de piedra sobre peana."),
    ("verja", verja, "Tramo de verja de 2.4 m a lo largo de X, centrado en el origen."),
    ("farol", farol, "Farola de hierro; luz de la entidad con offset [0, 2.75, 0]."),
    ("banco", banco, "Banco de madera y hierro."),
    ("fuente", fuente, "Fuente de piedra con dos tazas de agua."),
]


def construir_todos():
    sc = rc.escena()
    col = rc.coleccion("cementerio", sc)
    obs = []
    x = 0.0
    for _, fn, _ in MODELOS:
        ob = fn(col)
        ancho = ob.dimensions.x
        ob.location.x = x + ancho / 2
        x += ancho + 1.5
        obs.append(ob)
    return obs


def exportar_todos(obs):
    res = []
    for ob, (nombre, _, texto) in zip(obs, MODELOS):
        loc = tuple(ob.location)
        ob.location = (0, 0, 0)
        res.append(rc.exportar([ob], nombre, texto + " Base en el origen, frente hacia +Z."))
        ob.location = loc
    return res


if __name__ == "__main__":
    print(exportar_todos(construir_todos()))
