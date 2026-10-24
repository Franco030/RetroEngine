"""
Piezas para las salidas por el bosque (en vez de portales): el jugador sigue un sendero que se
mete entre los arboles y, al llegar a la espesura, la escena cambia con un fundido.

  sendero.obj  franja de tierra de 2.4 m de ancho y LARGO m, con bordes irregulares y hojarasca
               a los lados. Empieza en el origen y avanza hacia -Z del motor (con "rotation"
               se orienta hacia donde salga el camino). Va 2 cm por encima del suelo y no tiene
               colision.
  tronco.obj   tronco caido con una rama rota, para adornar la orilla del camino.

Arboles y arbustos 3D (low-poly, caras planas) para mezclar con los sprites:
  arbol_pino, arbol_pino_alto   conos de agujas apilados
  arbol_roble, arbol_roble_2    tronco con ramas y copa de bolas de hojas deformadas
  arbol_abedul                  tronco blanco fino y copa clara y pequena
  arbol_muerto                  ramas retorcidas sin hojas (cementerio)
  arbusto_hojas, arbusto_bayas  matas bajas (la segunda con bayas rojas)
Todos con la base en el origen y colision solo en el tronco (o la mata).

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

LARGO = 14.0
ANCHO = 2.4
ALTO = 0.02


def B(x, y, z):
    """Motor -> Blender."""
    return Vector((x, -z, y))


def sendero(col):
    m = rc.Malla()
    rnd = random.Random(3)
    pasos = int(LARGO)
    # bordes: medio ancho con ruido suave, y una franja de hojarasca por fuera
    borde = [ANCHO / 2 + 0.25 * math.sin(k * 1.7) + rnd.uniform(-0.12, 0.12) for k in range(pasos + 1)]
    deriva = [0.35 * math.sin(k * 0.45) for k in range(pasos + 1)]   # el camino serpentea un poco
    for k in range(pasos):
        z0, z1 = -k * LARGO / pasos, -(k + 1) * LARGO / pasos
        a0, a1 = deriva[k], deriva[k + 1]
        w0, w1 = borde[k], borde[k + 1]
        # tierra del centro
        m.poligono([B(a0 - w0, ALTO, z0), B(a0 + w0, ALTO, z0), B(a1 + w1, ALTO, z1),
                    B(a1 - w1, ALTO, z1)], "mat_tierra_suelo")
        # hojarasca a los lados (un poco mas baja, se mezcla con el suelo de la escena)
        for lado in (-1, 1):
            e0, e1 = a0 + lado * w0, a1 + lado * w1
            f0, f1 = e0 + lado * 0.7, e1 + lado * 0.7
            puntos = [B(e0, ALTO, z0), B(f0, ALTO * 0.5, z0), B(f1, ALTO * 0.5, z1), B(e1, ALTO, z1)]
            if lado < 0:
                puntos = list(reversed(puntos))
            m.poligono(puntos, "mat_hojarasca")
    for f in m.bm.faces:  # que todas miren hacia arriba
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
    return m.objeto("sendero", col)


def tronco(col):
    m = rc.Malla()
    m.segmento(B(-1.5, 0.22, 0), B(1.5, 0.2, 0.15), 0.24, 0.2, 7, "mat_madera_gris")
    m.segmento(B(0.4, 0.3, 0.05), B(0.9, 0.75, -0.35), 0.07, 0.04, 5, "mat_madera_gris")  # rama
    m.colision((0, -0.075, 0.22), (3.1, 0.5, 0.45))
    return m.objeto("tronco", col)


# ---------------------------------------------------------------------------
# Arboles y arbustos
# ---------------------------------------------------------------------------
def bola(m, centro, r, mat, rnd, aplastar=0.85):
    """Bola de hojas: icosfera deformada (cada vertice a una distancia distinta)."""
    res = bmesh.ops.create_icosphere(m.bm, subdivisions=1, radius=r)
    for v in res["verts"]:
        v.co *= rnd.uniform(0.82, 1.15)
        v.co.z *= aplastar
        v.co += B(*centro)
    for f in {f for v in res["verts"] for f in v.link_faces}:
        f.material_index = m._indice(mat)


def pino(nombre, alto, capas, r_base, semilla):
    def construir(col):
        rnd = random.Random(semilla)
        m = rc.Malla()
        m.segmento(B(0, 0, 0), B(0, alto * 0.35, 0), 0.16, 0.11, 6, "mat_corteza")
        paso = alto * 0.68 / capas
        for i in range(capas):
            y0 = alto * 0.2 + i * paso
            r = r_base * (1 - i / capas * 0.72)
            punta = 0.0 if i == capas - 1 else r * 0.2
            m.prisma(B(0, y0, 0), r, punta, paso * 1.45, 7, "mat_pino", giro=rnd.uniform(0, 50))
        m.colision((0, 0, 1.2), (0.4, 0.4, 2.4))
        return m.objeto(nombre, col)
    return construir


def roble(nombre, alto, semilla, mat="mat_hojas", corteza="mat_corteza", r_tronco=0.22, bolas=5,
          r_copa=(1.0, 1.35)):
    def construir(col):
        rnd = random.Random(semilla)
        m = rc.Malla()
        top = Vector((rnd.uniform(-0.2, 0.2), alto * 0.5, rnd.uniform(-0.2, 0.2)))
        m.segmento(B(0, 0, 0), B(*top), r_tronco, r_tronco * 0.7, 6, corteza)
        for _ in range(2):  # ramas gruesas hacia la copa (terminan dentro de ella)
            alcance = r_copa[0] * 0.6
            fin = top + Vector((rnd.uniform(-alcance, alcance), alto * 0.14,
                                rnd.uniform(-alcance, alcance)))
            m.segmento(B(*top), B(*fin), r_tronco * 0.45, r_tronco * 0.25, 5, corteza)
        for k in range(bolas):
            a = k / bolas * 2 * math.pi + rnd.uniform(-0.4, 0.4)
            d = rnd.uniform(0.5, 0.9) if k else 0.0
            c = (top.x + d * math.cos(a), alto * rnd.uniform(0.66, 0.8) + (0.35 if k == 0 else 0),
                 top.z + d * math.sin(a))
            bola(m, c, rnd.uniform(*r_copa), mat, rnd)
        m.colision((0, 0, 1.2), (r_tronco * 2 + 0.1, r_tronco * 2 + 0.1, 2.4))
        return m.objeto(nombre, col)
    return construir


def arbol_muerto(col):
    rnd = random.Random(8)
    m = rc.Malla()

    def rama(p, d, largo, r, nivel):
        fin = p + d * largo
        m.segmento(B(*p), B(*fin), r, r * 0.7, 5, "mat_corteza")
        if nivel == 0:
            return
        for _ in range(rnd.choice((2, 2, 3))):
            nd = (d + Vector((rnd.uniform(-0.9, 0.9), rnd.uniform(-0.1, 0.5), rnd.uniform(-0.9, 0.9))))
            nd.y = max(nd.y, 0.15)
            nd.normalize()
            rama(fin, nd, largo * rnd.uniform(0.55, 0.75), r * 0.62, nivel - 1)

    rama(Vector((0, 0, 0)), Vector((0.08, 1, 0.05)).normalized(), 2.4, 0.2, 3)
    m.colision((0, 0, 1.2), (0.45, 0.45, 2.4))
    return m.objeto("arbol_muerto", col)


def arbusto(nombre, bayas, semilla):
    def construir(col):
        rnd = random.Random(semilla)
        m = rc.Malla()
        centros = []
        for k in range(4):
            a = k * 2.1 + rnd.uniform(-0.3, 0.3)
            d = 0.0 if k == 0 else rnd.uniform(0.3, 0.45)
            c = (d * math.cos(a), rnd.uniform(0.32, 0.45), d * math.sin(a))
            r = rnd.uniform(0.38, 0.52)
            bola(m, c, r, "mat_hojas", rnd, aplastar=0.8)
            centros.append((c, r))
        if bayas:
            for _ in range(16):
                c, r = rnd.choice(centros)
                a, e = rnd.uniform(0, 2 * math.pi), rnd.uniform(-0.2, 0.9)
                p = (c[0] + r * math.cos(a) * math.cos(e), c[1] + r * 0.8 * math.sin(e),
                     c[2] + r * math.sin(a) * math.cos(e))
                bola(m, p, 0.045, "mat_sangre", rnd, aplastar=1.0)
        m.colision((0, 0, 0.4), (1.1, 1.1, 0.8))
        return m.objeto(nombre, col)
    return construir


PIEZAS = [
    (sendero, f"Sendero de tierra de {LARGO:.0f} m: empieza en el origen y avanza hacia -Z.\n"
              "Sin colision."),
    (tronco, "Tronco caido (3 m, a lo largo de X)."),
    (pino("arbol_pino", 4.6, 4, 1.5, 1), "Pino de 4.6 m."),
    (pino("arbol_pino_alto", 6.8, 5, 1.3, 2), "Pino alto y estrecho de 6.8 m."),
    (roble("arbol_roble", 5.0, 3), "Roble de 5 m."),
    (roble("arbol_roble_2", 4.2, 4, bolas=4, r_copa=(0.9, 1.25)), "Roble bajo de 4.2 m."),
    (roble("arbol_abedul", 5.6, 5, mat="mat_hojas_claras", corteza="mat_abedul", r_tronco=0.11,
           bolas=4, r_copa=(0.55, 0.8)), "Abedul de 5.6 m."),
    (arbol_muerto, "Arbol muerto sin hojas (cementerio)."),
    (arbusto("arbusto_hojas", False, 6), "Arbusto de 1 m."),
    (arbusto("arbusto_bayas", True, 7), "Arbusto con bayas rojas."),
]


def construir():
    sc = rc.escena()
    col = rc.coleccion("bosque", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col)
        ob.location.x = 30 + k * 7
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
