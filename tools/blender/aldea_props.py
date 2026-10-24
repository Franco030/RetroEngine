"""
Edificios y objetos de aldea para RetroEngine: casa_madera, casa_piedra, pozo, valla, barril,
caja y carreta. Todos con varios materiales (madera, piedra, tejas, paja...) que traen su propia
textura: en la escena NO se pone "texture".

  Base en el origen, frente (puerta) hacia +Z del motor (-Y en Blender).

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

FRENTE = (0, -1, 0)
ATRAS = (0, 1, 0)
IZQ = (-1, 0, 0)
DER = (1, 0, 0)


def tejado(m, ancho, fondo, z_alero, subida, mat, grosor=0.14, vuelo=0.35, vuelo_lado=0.3,
           eje="x", centro=(0, 0)):
    """Tejado a dos aguas. eje "x": cumbrera a lo largo de X (aleros delante y detras);
    eje "y": cumbrera a lo largo de Y (hastial al frente, como una capilla). 'ancho' y 'fondo'
    son siempre las medidas en X e Y del edificio. Devuelve la altura de la cumbrera."""
    medio = (fondo if eje == "x" else ancho) / 2
    largo_cumbrera = ancho if eje == "x" else fondo
    pendiente = math.hypot(medio, subida)
    ang = math.degrees(math.atan2(subida, medio))
    largo = pendiente + vuelo
    cumbre = Vector((centro[0], centro[1], z_alero + subida))
    for lado in (-1, 1):
        if eje == "x":
            d = Vector((0, lado * medio, -subida)).normalized()  # de la cumbrera al alero
            n = Vector((0, lado * subida, medio)).normalized()   # normal hacia fuera
            tam = (largo_cumbrera + 2 * vuelo_lado, largo, grosor)
            rot = (lado * -ang, 0, 0)
        else:
            d = Vector((lado * medio, 0, -subida)).normalized()
            n = Vector((lado * subida, 0, medio)).normalized()
            tam = (largo, largo_cumbrera + 2 * vuelo_lado, grosor)
            rot = (0, lado * ang, 0)
        m.caja(cumbre + d * (largo / 2) + n * (grosor / 2), tam, mat, rot=rot)
    return z_alero + subida


def hastiales(m, ancho, fondo, z_alero, subida, mat, eje="x"):
    """Los dos triangulos de pared bajo el tejado: en los lados +-X (eje "x") o en el frente
    y la trasera (eje "y")."""
    x, y = ancho / 2, fondo / 2
    zc = z_alero + subida
    if eje == "x":
        m.poligono([(x, -y, z_alero), (x, y, z_alero), (x, 0, zc)], mat)
        m.poligono([(-x, y, z_alero), (-x, -y, z_alero), (-x, 0, zc)], mat)
    else:
        m.poligono([(-x, -y, z_alero), (x, -y, z_alero), (0, -y, zc)], mat)
        m.poligono([(x, y, z_alero), (-x, y, z_alero), (0, y, zc)], mat)


def ventana(m, centro, normal, ancho=0.85, alto=0.95, mat="mat_ventana", postigos=True):
    """Ventana con alfeizar y, si se quiere, contraventanas abiertas a los lados."""
    n = Vector(normal)
    der = Vector((0, 0, 1)).cross(n).normalized()
    c = Vector(centro)
    m.decal(c, n, ancho, alto, mat)
    giro = math.degrees(math.atan2(n.x, -n.y))
    m.caja(c + n * 0.08 - Vector((0, 0, alto / 2 + 0.04)), (ancho + 0.2, 0.16, 0.07),
           "mat_madera_gris", rot=(0, 0, giro))
    if postigos:
        for lado in (-1, 1):
            m.caja(c + n * 0.05 + der * lado * (ancho / 2 + 0.2), (0.38, 0.05, alto + 0.05),
                   "mat_madera", rot=(0, 0, giro))


# ---------------------------------------------------------------------------
def casa_madera(col, noche=False):
    """noche: todas las ventanas encendidas (variante casa_madera_noche)."""
    vent = "mat_ventana_luz" if noche else "mat_ventana"
    m = rc.Malla()
    W, D, H = 5.0, 4.0, 2.6
    base = 0.4
    m.caja((0, 0, base / 2), (W + 0.3, D + 0.3, base), "mat_piedra")
    m.caja((0, 0, base + H / 2), (W, D, H), "mat_madera")
    for sx in (-1, 1):
        for sy in (-1, 1):
            m.caja((sx * W / 2, sy * D / 2, base + H / 2), (0.22, 0.22, H), "mat_madera_gris")
    z_alero = base + H
    hastiales(m, W, D, z_alero, 1.8, "mat_madera")
    cumbre = tejado(m, W, D, z_alero, 1.8, "mat_tejas")
    m.caja((0, 0, cumbre + 0.08), (W + 0.7, 0.2, 0.2), "mat_madera_gris")
    # chimenea de piedra
    m.caja((1.7, 0.9, cumbre - 0.2), (0.6, 0.6, 2.2), "mat_piedra")
    m.caja((1.7, 0.9, cumbre + 0.92), (0.7, 0.7, 0.12), "mat_piedra")
    # puerta con porche: tejadillo sobre dos postes y peldano
    xp = -0.9
    m.decal((xp, -D / 2, base + 1.0), FRENTE, 1.0, 2.0, "mat_puerta")
    m.caja((xp, -D / 2 - 0.45, 0.12), (1.6, 0.9, 0.24), "mat_piedra")
    for dx in (-0.8, 0.8):
        m.caja((xp + dx, -D / 2 - 0.85, base / 2 + 1.25), (0.13, 0.13, 2.5 + base),
               "mat_madera_gris")
    m.caja((xp, -D / 2 - 0.5, 2.85), (2.0, 1.25, 0.1), "mat_tejas", rot=(-14, 0, 0))
    # ventanas
    ventana(m, (1.15, -D / 2, 1.85), FRENTE, mat=vent)
    ventana(m, (W / 2, -0.6, 1.85), DER, mat=vent)
    ventana(m, (-W / 2, 0.4, 1.85), IZQ, mat=vent)
    ventana(m, (0.5, D / 2, 1.85), ATRAS, mat=vent)
    # lenera junto a la pared derecha
    for i in range(3):
        m.caja((W / 2 + 0.35, 0.9 + i * 0.02, 0.25 + i * 0.22), (0.5, 1.2, 0.2), "mat_madera",
               rot=(0, 0, 3 * i))
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 1.5), (W + 0.3, D + 0.3, 3.0))      # cimientos y paredes
    m.colision((xp, -D / 2 - 0.45, 0.12), (1.6, 0.9, 0.24))  # peldano (se sube)
    for dx in (-0.8, 0.8):
        m.colision((xp + dx, -D / 2 - 0.85, 1.45), (0.15, 0.15, 2.9))  # postes del porche
    m.colision((W / 2 + 0.35, 0.93, 0.4), (0.55, 1.3, 0.8))  # lenera
    return m.objeto("casa_madera_noche" if noche else "casa_madera", col)


def casa_piedra(col, noche=False):
    """noche: las dos ventanas encendidas (variante casa_piedra_noche)."""
    m = rc.Malla()
    W, D, H = 4.2, 3.6, 2.3
    m.caja((0, 0, H / 2), (W, D, H), "mat_piedra")
    hastiales(m, W, D, H, 2.1, "mat_piedra")
    cumbre = tejado(m, W, D, H, 2.1, "mat_paja", grosor=0.3, vuelo=0.45, vuelo_lado=0.35)
    m.caja((0, 0, cumbre + 0.12), (W + 0.6, 0.45, 0.25), "mat_paja")
    m.caja((-W / 2 + 0.2, 0.0, cumbre - 0.6), (0.7, 0.8, 2.6), "mat_piedra")
    m.decal((0.5, -D / 2, 0.95), FRENTE, 0.95, 1.9, "mat_puerta")
    m.caja((0.5, -D / 2 - 0.08, 1.98), (1.25, 0.2, 0.18), "mat_madera_gris")  # dintel
    ventana(m, (-0.9, -D / 2, 1.35), FRENTE, 0.7, 0.75, "mat_ventana_luz", postigos=False)
    ventana(m, (W / 2, 0.3, 1.35), DER, 0.7, 0.75,
            "mat_ventana_luz" if noche else "mat_ventana", postigos=False)
    # banco de piedra junto a la puerta
    m.caja((1.55, -D / 2 - 0.3, 0.22), (0.9, 0.45, 0.44), "mat_piedra")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, H / 2), (W, D, H))
    m.colision((1.55, -D / 2 - 0.3, 0.22), (0.9, 0.45, 0.44))  # banco
    return m.objeto("casa_piedra_noche" if noche else "casa_piedra", col)


def pozo(col):
    m = rc.Malla()
    # Hueco de verdad: pared exterior, brocal (anillo plano), pared interior hacia abajo y el
    # fondo oscuro. Nada de un disco pegado encima de una tapa: dos caras casi en el mismo
    # sitio se pelean en el buffer de profundidad (z-fighting) y se ven a trozos.
    m.prisma((0, 0, 0), 0.8, 0.78, 0.85, 8, "mat_piedra", giro=22.5, tapas=False)
    m.anillos((0, 0, 0), [(0.85, 0.78, 0.78), (0.85, 0.6, 0.6), (0.3, 0.6, 0.6)], 8,
              "mat_piedra", tapa_abajo=False, tapa_arriba=False)
    fondo = [(0.6 * math.cos(math.radians(22.5 + 45 * k)),
              0.6 * math.sin(math.radians(22.5 + 45 * k)), 0.3) for k in range(8)]
    m.poligono(fondo, "mat_oscuro")
    for x in (-0.72, 0.72):
        m.caja((x, 0, 1.0 + 0.85), (0.14, 0.14, 2.0), "mat_madera_gris")
    m.caja((0, 0, 1.85), (1.6, 0.1, 0.1), "mat_madera")                 # eje del torno
    m.caja((0.86, -0.12, 1.85), (0.06, 0.3, 0.06), "mat_metal")         # manivela
    m.caja((0.86, -0.25, 1.75), (0.06, 0.06, 0.25), "mat_metal")
    tejado(m, 1.5, 1.3, 2.75, 0.55, "mat_tejas", grosor=0.08, vuelo=0.2, vuelo_lado=0.15)
    m.caja((0, 0, 1.55), (0.025, 0.025, 0.55), "mat_lona")              # cuerda
    m.prisma((0, 0, 1.05), 0.13, 0.15, 0.24, 8, "mat_madera")           # cubo
    m.prisma((0, 0, 1.2), 0.155, 0.155, 0.03, 8, "mat_metal", tapas=False)
    # colision: solo lo que estorba a la altura del jugador
    m.octogono((0, 0, 0), 0.8 * math.cos(math.radians(22.5)), 0.85)
    return m.objeto("pozo", col)


def valla(col):
    """Tramo de valla de 2 m centrado en el origen, a lo largo de X."""
    m = rc.Malla()
    for x, inc in ((-1.0, 2.0), (1.0, -3.0)):
        m.caja((x, 0, 0.55), (0.11, 0.11, 1.1), "mat_madera_gris", rot=(inc, 0, 0))
        m.caja((x, 0, 1.12), (0.13, 0.13, 0.05), "mat_madera_gris")
    m.caja((0, -0.07, 0.42), (2.15, 0.05, 0.13), "mat_madera_gris", rot=(0, 1.5, 0))
    m.caja((0, -0.07, 0.86), (2.15, 0.05, 0.13), "mat_madera_gris", rot=(0, -1.0, 0))
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, -0.03, 0.55), (2.15, 0.16, 1.1))
    return m.objeto("valla", col)


def barril(col):
    m = rc.Malla()
    perfil = [(0.0, 0.27), (0.18, 0.31), (0.45, 0.335), (0.72, 0.31), (0.9, 0.27)]
    m.anillos((0, 0, 0), [(z, r, r) for z, r in perfil], 10, "mat_madera")
    for z, r in ((0.12, 0.305), (0.76, 0.305)):
        m.anillos((0, 0, z), [(0.0, r + 0.012, r + 0.012), (0.05, r + 0.012, r + 0.012)], 10,
                  "mat_metal", tapa_abajo=False, tapa_arriba=False)
    # colision: solo lo que estorba a la altura del jugador
    m.octogono((0, 0, 0), 0.31, 0.9)
    return m.objeto("barril", col)


def caja(col):
    m = rc.Malla()
    s = 0.7
    m.caja((0, 0, s / 2), (s, s, s), "mat_madera")
    t = 0.07
    for a in (-1, 1):
        for b in (-1, 1):
            m.caja((a * s / 2, b * s / 2, s / 2), (t, t, s + 0.01), "mat_madera_gris")
            m.caja((a * s / 2, 0, s / 2 + b * s / 2), (t, s + 0.01, t), "mat_madera_gris")
            m.caja((0, a * s / 2, s / 2 + b * s / 2), (s + 0.01, t, t), "mat_madera_gris")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 0.35), (0.72, 0.72, 0.7))
    return m.objeto("caja", col)


def carreta(col):
    m = rc.Malla()
    z = 0.62
    m.caja((0, 0, z), (1.2, 2.0, 0.1), "mat_madera")
    for x in (-0.6, 0.6):
        m.caja((x, 0, z + 0.22), (0.07, 2.0, 0.38), "mat_madera_gris")
    m.caja((0, 1.0, z + 0.22), (1.25, 0.07, 0.38), "mat_madera_gris")
    m.caja((0, 0, 0.45), (1.6, 0.12, 0.12), "mat_madera_gris")          # eje
    for x in (-0.78, 0.7):
        m.prisma((x, 0, 0.45), 0.45, 0.45, 0.08, 10, "mat_madera", rot=(0, 90, 0))
        m.prisma((x + (0.08 if x > 0 else -0.02), 0, 0.45), 0.1, 0.1, 0.04, 8,
                 "mat_madera_gris", rot=(0, 90, 0))
    for x in (-0.45, 0.45):                                               # varas
        m.caja((x, -1.6, 0.42), (0.08, 1.6, 0.08), "mat_madera_gris", rot=(-9, 0, 0))
    m.caja((0, -1.0, 0.3), (0.08, 0.08, 0.6), "mat_madera_gris")          # pata
    for i, x in enumerate((-0.3, 0.2)):                                   # unos sacos
        m.anillos((x, 0.35 + i * 0.3, z + 0.05), [(0.0, 0.22, 0.17), (0.3, 0.2, 0.15),
                                                  (0.42, 0.08, 0.06)], 6, "mat_lona")
    # colision: solo lo que estorba a la altura del jugador
    m.colision((0, 0, 0.5), (1.65, 2.05, 1.0))            # caja y ruedas
    m.colision((0, -1.6, 0.25), (1.0, 1.6, 0.5))          # varas
    return m.objeto("carreta", col)


MODELOS = [
    ("casa_madera", casa_madera, "Casa de madera con porche, chimenea y lenera."),
    ("casa_piedra", casa_piedra, "Casa de piedra con tejado de paja."),
    ("casa_madera_noche", lambda col: casa_madera(col, noche=True),
     "Casa de madera con las ventanas encendidas."),
    ("casa_piedra_noche", lambda col: casa_piedra(col, noche=True),
     "Casa de piedra con las ventanas encendidas."),
    ("pozo", pozo, "Pozo con tejadillo, torno y cubo."),
    ("valla", valla, "Tramo de valla de 2 m a lo largo de X, centrado en el origen."),
    ("barril", barril, "Barril de madera con aros."),
    ("caja", caja, "Caja de madera de 0.7 m."),
    ("carreta", carreta, "Carreta de mano con sacos."),
]


def construir_todos():
    sc = rc.escena()
    col = rc.coleccion("aldea", sc)
    obs = []
    x = 0.0
    for nombre, fn, _ in MODELOS:
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
