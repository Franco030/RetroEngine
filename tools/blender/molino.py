"""
Molino de torre: cuerpo (molino.obj) y aspas (molino_aspas.obj), por separado porque el motor no
tiene jerarquias: las aspas son otra entidad que gira con "spin".

  molino.obj        base en el origen, frente hacia +Z del motor (-Y en Blender).
  molino_aspas.obj  origen en el BUJE; el eje de giro es la Z del motor. En la escena:
                      "position": posicion del molino + BUJE (girado si el molino gira en Y),
                      "rotation": la misma del molino, "spin": [0, 0, -45], "collider": false.

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402

importlib.reload(rc)

from mathutils import Matrix, Vector  # noqa: E402

# Posicion del buje respecto a la base del molino, en coordenadas del MOTOR (x, y, z).
BUJE = (0.0, 6.9, 2.1)

ALTO_ZOCALO = 0.8
ALTO_TORRE = 6.4          # donde empieza la cubierta
R_ABAJO, R_ARRIBA = 2.1, 1.5
APOTEMA = math.cos(math.radians(22.5))  # distancia al centro de una cara / radio


def radio_torre(z):
    t = (z - ALTO_ZOCALO) / (ALTO_TORRE - ALTO_ZOCALO)
    return R_ABAJO + (R_ARRIBA - R_ABAJO) * t


def pieza_en_pared(m, angulo, z0, z1, ancho, mat, salida=0.03):
    """Quad (puerta o ventana) pegado a la cara de la torre que mira hacia 'angulo' (grados;
    -90 = frente). Sigue la inclinacion de la pared."""
    rot = Matrix.Rotation(math.radians(angulo + 90), 3, "Z")
    a0 = radio_torre(max(z0, ALTO_ZOCALO)) * APOTEMA + salida
    a1 = radio_torre(max(z1, ALTO_ZOCALO)) * APOTEMA + salida
    h = ancho / 2
    esquinas = [(-h, -a0, z0), (h, -a0, z0), (h, -a1, z1), (-h, -a1, z1)]
    return m.quad([rot @ Vector(p) for p in esquinas], mat)


def cuerpo(col):
    m = rc.Malla()
    # zocalo de piedra y torre de revoque, octogonales (giro 22.5: una cara mira al frente)
    m.prisma((0, 0, 0), 2.25, 2.2, ALTO_ZOCALO, 8, "mat_piedra", giro=22.5)
    m.prisma((0, 0, ALTO_ZOCALO), R_ABAJO, R_ARRIBA, ALTO_TORRE - ALTO_ZOCALO, 8, "mat_yeso",
             giro=22.5, tapas=False)

    # balconada de madera a media altura: suelo, 8 postes y barandilla
    zb = 3.3
    m.prisma((0, 0, zb - 0.12), 2.75, 2.75, 0.12, 8, "mat_madera_gris", giro=22.5)
    for k in range(8):
        a = math.radians(45 * k)
        m.caja((2.6 * math.cos(a), 2.6 * math.sin(a), zb + 0.45), (0.1, 0.1, 0.9), "mat_madera")
        b = math.radians(45 * k + 22.5)
        largo = 2 * 2.6 * math.sin(math.radians(22.5))
        r = 2.6 * APOTEMA
        m.caja((r * math.cos(b), r * math.sin(b), zb + 0.88), (0.08, largo + 0.08, 0.08),
               "mat_madera", rot=(0, 0, 45 * k + 22.5))
    # vigas que sostienen la balconada
    for k in range(4):
        a = 45 * k * 2 + 22.5
        m.caja((0, 0, zb - 0.32), (0.12, 5.4, 0.16), "mat_madera_gris", rot=(0, 0, a))

    # cubierta: aro de madera y tejado conico de tejas
    m.prisma((0, 0, ALTO_TORRE), 1.75, 1.75, 0.35, 8, "mat_madera_gris", giro=22.5)
    m.prisma((0, 0, ALTO_TORRE + 0.35), 1.85, 0.0, 1.9, 8, "mat_tejas", giro=22.5)

    # caja del eje: sale de la cubierta hacia el frente, hasta el buje
    bx, by, bz = BUJE[0], -BUJE[2], BUJE[1]   # motor -> Blender
    m.caja((bx, by + 0.75, bz), (0.75, 1.5, 0.75), "mat_madera_gris")

    # palo de cola: de la parte trasera de la cubierta al suelo
    arriba = Vector((0, 1.55, ALTO_TORRE + 0.15))
    abajo = Vector((0, 4.6, 0.35))
    d = arriba - abajo
    ang = math.degrees(math.atan2(d.y, d.z))
    m.caja((abajo + arriba) / 2, (0.2, 0.2, d.length), "mat_madera", rot=(-ang, 0, 0))
    m.caja((0, 4.6, 0.3), (0.9, 0.25, 0.6), "mat_madera_gris")  # tope en el suelo

    # puerta, peldano y ventanas
    pieza_en_pared(m, -90, 0.0, 2.1, 1.1, "mat_puerta", salida=0.06)
    m.caja((0, -2.35, 0.08), (1.5, 0.6, 0.16), "mat_piedra")
    pieza_en_pared(m, -90, 4.5, 5.3, 0.7, "mat_ventana")
    pieza_en_pared(m, 0, 2.0, 2.8, 0.7, "mat_ventana")
    pieza_en_pared(m, 180, 4.6, 5.4, 0.7, "mat_ventana")
    pieza_en_pared(m, 90, 1.9, 2.7, 0.7, "mat_ventana")
    # colision: solo lo que estorba a la altura del jugador
    m.octogono((0, 0, 0), 2.25 * APOTEMA, 8.5)            # zocalo y torre
    m.colision((0, 4.2, 1.0), (0.3, 1.0, 2.0))            # parte baja del palo de cola
    m.colision((0, 4.6, 0.3), (0.9, 0.3, 0.6))            # tope del palo
    return m.objeto("molino", col)


def aspas(col):
    """Cuatro aspas en el plano XZ de Blender (eje de giro = Y de Blender = Z del motor)."""
    m = rc.Malla()
    # buje: cilindro corto hacia el frente (-Y de Blender)
    m.prisma((0, 0.15, 0), 0.32, 0.28, 0.5, 8, "mat_madera_gris", rot=(90, 0, 0))
    for k in range(4):
        a = 45 + 90 * k
        giro = Matrix.Rotation(math.radians(a), 4, "Y")

        def en(p):
            return giro @ Vector(p)

        # vara principal, del buje a la punta
        m.caja(en((0, -0.25, 2.3)), (0.2, 0.16, 4.6), "mat_madera", rot=(0, a, 0))
        # armazon: larguero exterior y travesanos
        m.caja(en((1.05, -0.2, 2.75)), (0.08, 0.08, 3.5), "mat_madera_gris", rot=(0, a, 0))
        for i in range(7):
            z = 1.0 + i * 3.5 / 6
            m.caja(en((0.55, -0.2, z)), (1.1, 0.06, 0.06), "mat_madera_gris", rot=(0, a, 0))
        # lona, un poco por detras del armazon
        m.caja(en((0.55, -0.12, 2.75)), (1.05, 0.03, 3.5), "mat_lona", rot=(0, a, 0))
    return m.objeto("molino_aspas", col)


def construir():
    sc = rc.escena()
    col = rc.coleccion("molino", sc)
    c = cuerpo(col)
    a = aspas(col)
    # para verlo junto en Blender: aspas en el buje
    a.location = (BUJE[0], -BUJE[2], BUJE[1])
    return c, a


def exportar(c, a):
    loc = tuple(a.location)
    a.location = (0, 0, 0)
    r1 = rc.exportar([c], "molino", "Molino de torre. Base en el origen, frente hacia +Z.\n"
                     "Las aspas van aparte (molino_aspas.obj), buje en " + str(BUJE))
    r2 = rc.exportar([a], "molino_aspas", "Aspas del molino. Origen en el buje, giran en Z:\n"
                     '"spin": [0, 0, -45], "collider": false')
    a.location = loc
    return r1, r2


if __name__ == "__main__":
    cuerpo_ob, aspas_ob = construir()
    print(exportar(cuerpo_ob, aspas_ob))
