"""
Detalles pequenos en 3D (sustituyen a los sprites billboard; las llamas animadas siguen siendo
sprites). Todos con la base en el origen salvo raices y telarana, que cuelgan desde el origen.

  velas_grupo       tres velas en un platillo (llamas: sprite spr_llama en LLAMAS_VELAS)
  cuervo            cuervo posado, mirando a +Z
  setas_grupo       tres setas de sombrero azul palido (con "emissive": true brillan)
  hierba_mata       mata de briznas (dos caras)
  flores_campo      briznas con flores de colores
  flores_marchitas  ramo seco con las cabezas caidas y una cinta
  huesos_pila       monton de huesos con dos calaveras
  ojos              dos ojos rojos (con "emissive": true; la escena les pone facePlayer)
  raices            raices que cuelgan del techo: el origen va pegado al techo
  telarana          media telarana tendida (textura con transparencia); el origen es el centro
                    del borde de arriba y cuelga hacia -Y; se coloca cruzando una esquina

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402
import subterraneo as st  # noqa: E402

importlib.reload(rc)
importlib.reload(st)

import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

B, caja = st.B, st.caja

# Velas del grupo: (x, z, alto); las llamas van encima (para la escena)
VELAS = [(-0.12, 0.02, 0.16), (0.0, -0.03, 0.25), (0.12, 0.03, 0.11)]
LLAMAS_VELAS = [(x, alto + 0.03, z) for x, z, alto in VELAS]


def bola(m, c, r, mat, escala=(1.0, 1.0, 1.0), rnd=None, sub=1):
    """Icosfera en c (motor) con radios distintos por eje (escala x, y, z del motor)."""
    res = bmesh.ops.create_icosphere(m.bm, subdivisions=sub, radius=r)
    for v in res["verts"]:
        if rnd:
            v.co *= rnd.uniform(0.9, 1.1)
        v.co.x *= escala[0]
        v.co.y *= escala[2]       # z del motor = -y de Blender
        v.co.z *= escala[1]
        v.co += B(*c)
    for f in {f for v in res["verts"] for f in v.link_faces}:
        f.material_index = m._indice(mat)


def dos_caras(m, puntos, mat):
    """Cara visible por los dos lados (el motor no dibuja la cara de atras). La de atras se
    separa 2 mm: si compartiera los vertices, al limpiar duplicados se fundiria con la otra."""
    a, b, c = (Vector(p) for p in puntos[:3])
    n = (b - a).cross(c - a).normalized() * 0.002
    m.poligono([B(*p) for p in puntos], mat)
    m.poligono([B(*(Vector(p) - n)) for p in reversed(puntos)], mat)


def brizna(m, x, z, alto, rnd, mat="mat_hierba"):
    ang = rnd.uniform(0, math.pi)
    dx, dz = math.cos(ang) * 0.025, math.sin(ang) * 0.025
    caida = (rnd.uniform(-0.12, 0.12), rnd.uniform(-0.12, 0.12))
    dos_caras(m, [(x - dx, 0, z - dz), (x + dx, 0, z + dz), (x + caida[0], alto, z + caida[1])], mat)


def velas_grupo(col):
    m = rc.Malla()
    m.prisma(B(0, 0, 0), 0.22, 0.2, 0.03, 10, "mat_metal")
    for x, z, alto in VELAS:
        m.prisma(B(x, 0.03, z), 0.032, 0.03, alto, 7, "mat_cera")
        caja(m, (x - 0.035, 0.03 + alto * 0.5, z + 0.025), (x - 0.02, 0.03 + alto - 0.01, z + 0.035),
             "mat_cera")                                                   # goteo
        caja(m, (x - 0.004, 0.03 + alto, z - 0.004), (x + 0.004, 0.03 + alto + 0.025, z + 0.004),
             "mat_oscuro")                                                 # mecha
    return m.objeto("velas_grupo", col)


def cuervo(col):
    rnd = random.Random(2)
    m = rc.Malla()
    bola(m, (0, 0.17, -0.02), 0.1, "mat_plumas", (0.9, 0.9, 1.6), rnd)    # cuerpo
    bola(m, (0, 0.27, 0.12), 0.055, "mat_plumas", (1, 1, 1.1), rnd)       # cabeza
    m.segmento(B(0, 0.27, 0.16), B(0, 0.255, 0.25), 0.022, 0.0, 4, "mat_oscuro")  # pico
    m.segmento(B(0, 0.16, -0.16), B(0, 0.12, -0.32), 0.05, 0.02, 4, "mat_plumas",
               ovalo=(1.0, 0.3))                                            # cola
    for lado in (-1, 1):
        bola(m, (lado * 0.075, 0.19, -0.03), 0.06, "mat_plumas", (0.4, 0.8, 2.0), rnd)  # alas
        m.segmento(B(lado * 0.03, 0.09, 0.0), B(lado * 0.03, 0.0, 0.02), 0.008, 0.008, 4, "mat_oscuro")
        m.decal(B(lado * 0.04, 0.29, 0.15), (lado, 0, 0), 0.015, 0.015, "mat_flor_amarilla", salida=0.01)
    return m.objeto("cuervo", col)


def setas_grupo(col):
    rnd = random.Random(3)
    m = rc.Malla()
    for x, z, alto, r in ((-0.1, 0.03, 0.2, 0.09), (0.06, -0.05, 0.3, 0.12), (0.15, 0.07, 0.12, 0.06)):
        m.prisma(B(x, 0, z), 0.02 + r * 0.15, 0.018 + r * 0.1, alto, 6, "mat_hueso")
        m.anillos(B(x, alto - 0.01, z), [(0.0, r, r), (r * 0.35, r * 0.9, r * 0.9), (r * 0.6, r * 0.5, r * 0.5),
                                          (r * 0.7, 0.0, 0.0)], 8, "mat_seta", tapa_arriba=False)
    return m.objeto("setas_grupo", col)


def hierba_mata(col):
    rnd = random.Random(4)
    m = rc.Malla()
    for _ in range(16):
        brizna(m, rnd.uniform(-0.25, 0.25), rnd.uniform(-0.25, 0.25), rnd.uniform(0.3, 0.6), rnd)
    return m.objeto("hierba_mata", col)


def flores_campo(col):
    rnd = random.Random(5)
    m = rc.Malla()
    for _ in range(12):
        brizna(m, rnd.uniform(-0.25, 0.25), rnd.uniform(-0.25, 0.25), rnd.uniform(0.25, 0.45), rnd)
    colores = ["mat_flor_roja", "mat_flor_amarilla", "mat_flor_blanca", "mat_flor_morada"]
    for k in range(6):
        x, z = rnd.uniform(-0.2, 0.2), rnd.uniform(-0.2, 0.2)
        alto = rnd.uniform(0.3, 0.5)
        top = (x + rnd.uniform(-0.05, 0.05), alto, z + rnd.uniform(-0.05, 0.05))
        m.segmento(B(x, 0, z), B(*top), 0.006, 0.005, 4, "mat_hierba")
        bola(m, top, 0.04, colores[k % len(colores)], (1, 0.6, 1), rnd)
    return m.objeto("flores_campo", col)


def flores_marchitas(col):
    rnd = random.Random(6)
    m = rc.Malla()
    for k in range(7):
        a = k / 7 * 2 * math.pi
        x0, z0 = 0.02 * math.cos(a), 0.02 * math.sin(a)
        alto = rnd.uniform(0.25, 0.4)
        medio = (x0 + 0.08 * math.cos(a), alto, z0 + 0.08 * math.sin(a))
        cabeza = (medio[0] + 0.07 * math.cos(a), alto - rnd.uniform(0.06, 0.12),
                  medio[2] + 0.07 * math.sin(a))               # el tallo se dobla y la flor cuelga
        m.segmento(B(x0, 0, z0), B(*medio), 0.008, 0.007, 4, "mat_corteza")
        m.segmento(B(*medio), B(*cabeza), 0.007, 0.006, 4, "mat_corteza")
        bola(m, cabeza, 0.035, "mat_flor_marchita", (1, 0.8, 1), rnd)
    caja(m, (-0.04, 0.08, -0.04), (0.04, 0.12, 0.04), "mat_lona")           # cinta
    for _ in range(4):                                                       # petalos caidos
        x, z = rnd.uniform(-0.25, 0.25), rnd.uniform(-0.25, 0.25)
        caja(m, (x - 0.015, 0, z - 0.01), (x + 0.015, 0.006, z + 0.01), "mat_flor_marchita")
    return m.objeto("flores_marchitas", col)


def huesos_pila(col):
    rnd = random.Random(7)
    m = rc.Malla()
    st.craneo(m, (-0.18, 0.04, 0.05), 1.0)
    st.craneo(m, (0.2, 0.0, -0.05), 0.9)
    for _ in range(7):
        x0, z0 = rnd.uniform(-0.4, 0.3), rnd.uniform(-0.3, 0.3)
        ang = rnd.uniform(0, math.pi)
        largo = rnd.uniform(0.3, 0.45)
        y = rnd.uniform(0.02, 0.09)
        st.hueso_largo(m, (x0, y, z0), (x0 + math.cos(ang) * largo, y + rnd.uniform(-0.02, 0.05),
                                         z0 + math.sin(ang) * largo), r=0.018, nudo=0.03)
    return m.objeto("huesos_pila", col)


def ojos(col):
    m = rc.Malla()
    for x in (-0.07, 0.07):
        bola(m, (x, 0, 0), 0.035, "mat_ojo", (1.3, 0.75, 0.5))
    return m.objeto("ojos", col)


def raices(col):
    rnd = random.Random(9)
    m = rc.Malla()
    for _ in range(7):
        p = Vector((rnd.uniform(-0.3, 0.3), 0.05, rnd.uniform(-0.3, 0.3)))
        r = rnd.uniform(0.025, 0.045)
        largo = rnd.uniform(0.8, 1.5)
        pasos = 6
        for k in range(pasos):
            q = p + Vector((rnd.uniform(-0.07, 0.07), -largo / pasos, rnd.uniform(-0.07, 0.07)))
            m.segmento(B(*p), B(*q), r, r * 0.8, 5, "mat_corteza")
            if rnd.random() < 0.3:                                 # raicilla
                fin = q + Vector((rnd.uniform(-0.15, 0.15), -0.12, rnd.uniform(-0.15, 0.15)))
                m.segmento(B(*q), B(*fin), r * 0.4, 0.004, 4, "mat_corteza")
            p, r = q, r * 0.8
    return m.objeto("raices", col)


def telarana(col):
    """Quad de 1.5 x 1.0 que cuelga del origen (centro del borde de arriba), a dos caras."""
    m = rc.Malla()
    esquinas = [(-0.75, -1.0, 0), (0.75, -1.0, 0), (0.75, 0, 0), (-0.75, 0, 0)]
    m.quad([B(*p) for p in esquinas], "spr_telarana")
    atras = [(x, y, -0.003) for x, y, _ in reversed(esquinas)]   # separada: ver dos_caras
    m.quad([B(*p) for p in atras], "spr_telarana", uvs=((0, 1), (1, 1), (1, 0), (0, 0)))
    return m.objeto("telarana", col)


PIEZAS = [
    (velas_grupo, "Tres velas en un platillo; llamas (sprite) en " + str(LLAMAS_VELAS) + "."),
    (cuervo, "Cuervo posado, mira a +Z."),
    (setas_grupo, "Tres setas (con emissive brillan)."),
    (hierba_mata, "Mata de hierba."),
    (flores_campo, "Hierba con flores."),
    (flores_marchitas, "Ramo marchito."),
    (huesos_pila, "Monton de huesos con dos calaveras."),
    (ojos, "Dos ojos rojos (emissive)."),
    (raices, "Raices colgantes: el origen va en el techo."),
    (telarana, "Media telarana de 1.5 x 1 m que cuelga del origen (centro del borde de arriba)."),
]


def construir():
    sc = rc.escena()
    col = rc.coleccion("detalles", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col)
        ob.location.x = k * 1.2
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
