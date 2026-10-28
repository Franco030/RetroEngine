"""
Modelos del capitulo del bosque (despues del bosque negro): cuentan la historia por si solos.

El campamento (una partida de busqueda que vino a por Clara y no volvio):
  tienda.obj         tienda de lona naranja, abierta por delante (+Z), con vientos y estacas
  tienda_caida.obj   la misma, hundida: un palo roto y la lona por el suelo
  fogata.obj         circulo de piedras, lenos quemados y una olla en un tripode
  mochila.obj        mochila naranja tirada
  farol_suelo.obj    farol de mano en el suelo (el cristal brilla: la luz la pone la entidad)
  radio.obj          radio portatil con la antena fuera
  botas.obj          un par de botas, juntas y derechas (se usan seis en fila)
  cartel_busca.obj   cartel de "SE BUSCA" clavado (va sobre un tronco; mira a +Z)
La aldea vieja (Valdemora, el primer pueblo del pacto):
  casa_ruina.obj     casa de piedra con el tejado hundido y una X roja en la puerta
  campanario.obj     campanario roto con la campana sin badajo
  pozo_tapado.obj    pozo lleno de piedras con tablas clavadas encima
  cruz_pequena.obj   cruz de madera pequena (tumbas de ninos)
  tumba_abierta.obj  fosa recien cavada, sin nombre, con la tierra al lado y una pala clavada
El final:
  arbol_hueco.obj    arbol enorme y seco con un hueco de verdad al frente (se ve lo de dentro),
                     raices, ramas y campanillas colgando
  zapatitos.obj      monton de zapatos de nino
  farol_mano.obj     farol que llevan los aldeanos (el cristal brilla)

Coordenadas del MOTOR (x derecha, y arriba, z hacia el jugador) pasadas a Blender con B().
Base en el origen, frente hacia +Z.

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


def quad(m, pts, mat, doble=False):
    """Cara de 4 esquinas (motor) con UV 0..1; 'doble' la duplica al reves (se ve por los dos
    lados: lonas, papel)."""
    m.quad([B(*p) for p in pts], mat)
    if doble:
        m.quad([B(*p) for p in reversed(pts)], mat, uvs=((1, 0), (0, 0), (0, 1), (1, 1)))


# ---------------------------------------------------------------------------
# Campamento
# ---------------------------------------------------------------------------
def _lona_tienda(m, caida=False):
    L, W, H = 1.1, 0.8, 1.15      # media longitud (z), media anchura (x), altura
    L_ = "mat_lona_naranja"
    if not caida:
        for s in (-1, 1):          # dos faldones del tejado
            quad(m, [(s * W, 0.02, L), (0, H, L), (0, H, -L), (s * W, 0.02, -L)], L_, doble=True)
        # trasera cerrada y delantera medio abierta (una solapa recogida)
        m.poligono([B(-W, 0.02, -L), B(W, 0.02, -L), B(0, H, -L)], L_)
        m.poligono([B(W, 0.02, -L), B(-W, 0.02, -L), B(0, H, -L)], L_)
        m.poligono([B(-W, 0.02, L), B(-0.05, 0.02, L), B(0, H, L)], L_)
        m.poligono([B(-0.05, 0.02, L), B(-W, 0.02, L), B(0, H, L)], L_)
        quad(m, [(0.05, 0.02, L - 0.05), (0.75, 0.02, L - 0.05), (0.05, H - 0.1, L - 0.05),
                 (0.0, H - 0.1, L - 0.05)], "mat_oscuro")     # el interior en sombra
        for z in (-L, L):          # palos
            m.segmento(B(0, 0, z), B(0, H + 0.08, z), 0.02, 0.02, 4, "mat_madera")
        m.segmento(B(0, H, -L), B(0, H, L), 0.015, 0.015, 4, "mat_madera")
        for z in (-L - 0.6, L + 0.6):                         # vientos y estacas
            m.segmento(B(0, H + 0.05, z * 0.92), B(0, 0, z), 0.006, 0.006, 3, "mat_lona")
            caja(m, (-0.02, 0, z - 0.02), (0.02, 0.12, z + 0.02), "mat_madera")
        col(m, (-W, 0, -L), (W, H, L))
    else:
        # un faldon por el suelo, el otro hundido a media altura y el palo de delante roto
        quad(m, [(-W - 0.2, 0.03, L), (0.1, 0.03, L), (0.1, 0.03, -L), (-W - 0.2, 0.03, -L)], L_, doble=True)
        quad(m, [(0.1, 0.03, L), (W, 0.02, L), (W, 0.02, -L), (0.1, 0.45, -L)], L_, doble=True)
        quad(m, [(0.1, 0.03, L), (0.1, 0.45, -L), (-0.2, 0.55, -L), (-0.3, 0.03, L)], L_, doble=True)
        m.segmento(B(0, 0, -L), B(0, 0.6, -L), 0.02, 0.02, 4, "mat_madera")
        m.segmento(B(0, 0, L), B(0.05, 0.42, L), 0.02, 0.02, 4, "mat_madera")
        m.segmento(B(0.05, 0.42, L), B(0.45, 0.3, L + 0.3), 0.02, 0.02, 4, "mat_madera")  # roto
        col(m, (-W, 0, -L), (W, 0.4, L))


def tienda(col_):
    m = rc.Malla()
    _lona_tienda(m)
    return m.objeto("tienda", col_)


def tienda_caida(col_):
    m = rc.Malla()
    _lona_tienda(m, caida=True)
    return m.objeto("tienda_caida", col_)


def fogata(col_):
    m = rc.Malla()
    rnd = random.Random(1)
    for k in range(9):
        a = 2 * math.pi * k / 9
        m.prisma(B(0.42 * math.cos(a), 0, 0.42 * math.sin(a)), 0.11, 0.07, 0.12, 5, "mat_roca",
                 giro=rnd.uniform(0, 70))
    for k in range(4):                                   # lenos quemados, en cruz
        a = math.pi * k / 4 + rnd.uniform(-0.2, 0.2)
        m.segmento(B(-0.32 * math.cos(a), 0.05, -0.32 * math.sin(a)),
                   B(0.32 * math.cos(a), 0.09, 0.32 * math.sin(a)), 0.045, 0.04, 5,
                   "mat_oscuro" if k % 2 else "mat_corteza")
    caja(m, (-0.25, 0.0, -0.25), (0.25, 0.02, 0.25), "mat_oscuro")   # ceniza
    for k in range(3):                                   # tripode y olla
        a = 2 * math.pi * k / 3 + 0.4
        m.segmento(B(0.5 * math.cos(a), 0, 0.5 * math.sin(a)), B(0, 0.95, 0), 0.015, 0.012, 4, "mat_madera")
    m.segmento(B(0, 0.95, 0), B(0, 0.62, 0), 0.004, 0.004, 3, "mat_metal")
    m.anillos(B(0, 0.38, 0), [(0.0, 0.1, 0.1), (0.04, 0.15, 0.15), (0.2, 0.15, 0.15), (0.22, 0.13, 0.13)],
              8, "mat_metal", tapa_arriba=False)
    col(m, (-0.5, 0, -0.5), (0.5, 0.15, 0.5))
    return m.objeto("fogata", col_)


def mochila(col_):
    m = rc.Malla()
    caja(m, (-0.18, 0, -0.12), (0.18, 0.45, 0.12), "mat_lona_naranja", rot=(0, 0, 0))
    m.segmento(B(-0.2, 0.47, 0), B(0.2, 0.47, 0), 0.07, 0.07, 6, "mat_lona", giro=0)   # saco enrollado
    caja(m, (-0.15, 0.08, 0.12), (0.15, 0.25, 0.18), "mat_lona_naranja")             # bolsillo
    for x in (-0.1, 0.1):                                                              # correas
        caja(m, (x - 0.02, 0.05, -0.135), (x + 0.02, 0.43, -0.12), "mat_oscuro")
    return m.objeto("mochila", col_)


def farol(m, alto=0.32, asa=True):
    caja(m, (-0.07, 0, -0.07), (0.07, 0.04, 0.07), "mat_metal")
    caja(m, (-0.055, 0.04, -0.055), (0.055, alto * 0.7, 0.055), "mat_ventana_luz")
    for x, z in ((-0.06, -0.06), (0.06, -0.06), (-0.06, 0.06), (0.06, 0.06)):
        m.segmento(B(x, 0.04, z), B(x, alto * 0.7, z), 0.007, 0.007, 3, "mat_metal")
    m.prisma(B(0, alto * 0.7, 0), 0.08, 0.02, alto * 0.18, 4, "mat_metal", giro=45)
    if asa:
        m.segmento(B(-0.04, alto * 0.88, 0), B(0, alto, 0), 0.006, 0.006, 3, "mat_metal")
        m.segmento(B(0, alto, 0), B(0.04, alto * 0.88, 0), 0.006, 0.006, 3, "mat_metal")


def farol_suelo(col_):
    m = rc.Malla()
    farol(m)
    return m.objeto("farol_suelo", col_)


def farol_mano(col_):
    m = rc.Malla()
    farol(m, alto=0.26)
    return m.objeto("farol_mano", col_)


def radio(col_):
    m = rc.Malla()
    caja(m, (-0.13, 0, -0.05), (0.13, 0.17, 0.05), "mat_oscuro")
    caja(m, (-0.11, 0.02, 0.05), (0.02, 0.13, 0.055), "mat_metal")         # rejilla
    m.decal(B(0.07, 0.1, 0.05), (0, -1, 0), 0.05, 0.03, "mat_ventana_luz", salida=0.004)  # dial
    m.segmento(B(0.1, 0.17, 0), B(0.16, 0.5, -0.04), 0.005, 0.003, 3, "mat_metal")   # antena
    m.segmento(B(-0.1, 0.17, 0), B(0.06, 0.17, 0), 0.008, 0.008, 3, "mat_metal")     # asa
    return m.objeto("radio", col_)


def botas(col_):
    m = rc.Malla()
    for x in (-0.09, 0.09):
        caja(m, (x - 0.055, 0, -0.06), (x + 0.055, 0.04, 0.2), "mat_oscuro")           # suela
        caja(m, (x - 0.05, 0.04, -0.06), (x + 0.05, 0.12, 0.17), "mat_madera")         # pie
        caja(m, (x - 0.05, 0.04, -0.06), (x + 0.05, 0.32, 0.05), "mat_madera")         # cana
        caja(m, (x - 0.052, 0.27, -0.062), (x + 0.052, 0.3, 0.052), "mat_oscuro")      # vuelta
    return m.objeto("botas", col_)


def cartel_busca(col_):
    m = rc.Malla()
    quad(m, [(-0.21, 0.0, 0.0), (0.21, 0.0, 0.0), (0.21, 0.56, 0.0), (-0.21, 0.56, 0.0)],
         "mat_cartel_busca")
    caja(m, (-0.015, 0.52, -0.01), (0.015, 0.55, 0.02), "mat_metal")                  # clavo
    return m.objeto("cartel_busca", col_)


# ---------------------------------------------------------------------------
# Aldea vieja
# ---------------------------------------------------------------------------
def casa_ruina(col_):
    m = rc.Malla()
    rnd = random.Random(7)
    P = "mat_sillar_viejo"
    ax, az, G = 2.6, 2.1, 0.3
    # muros con el borde de arriba roto (tramos de distinta altura)
    def muro(x0, x1, z0, z1, horizontal, hueco=None):
        n = 6
        for k in range(n):
            t0, t1 = k / n, (k + 1) / n
            alto = rnd.uniform(1.6, 2.9) if k not in (0, n - 1) else 2.9
            if horizontal:
                a, b = x0 + (x1 - x0) * t0, x0 + (x1 - x0) * t1
                if hueco and a < hueco[1] and b > hueco[0]:
                    caja(m, (a, 2.0, z0), (b, max(alto, 2.3), z1), P)            # dintel
                    continue
                caja(m, (a, 0, z0), (b, alto, z1), P)
            else:
                a, b = z0 + (z1 - z0) * t0, z0 + (z1 - z0) * t1
                caja(m, (x0, 0, a), (x1, alto, b), P)
    muro(-ax, ax, -az, -az + G, True)
    muro(-ax, ax, az - G, az, True, hueco=(-0.5, 0.45))
    muro(-ax, -ax + G, -az, az, False)
    muro(ax - G, ax, -az, az, False)
    # puerta atrancada con una X roja pintada
    caja(m, (-0.45, 0, az - 0.2), (0.45, 2.0, az - 0.14), "mat_puerta")
    for s in (-1, 1):
        m.segmento(B(-0.4, 0.15 if s > 0 else 1.85, az - 0.12), B(0.4, 1.85 if s > 0 else 0.15, az - 0.12),
                   0.045, 0.045, 4, "mat_sangre", ovalo=(1.0, 0.2))
    caja(m, (-0.55, 0.9, az - 0.13), (0.55, 1.05, az - 0.07), "mat_madera")          # tabla clavada
    # tejado hundido: unas vigas aun arriba, el resto caido dentro
    for k, x in enumerate((-1.8, -0.6, 0.6, 1.8)):
        if k == 2:
            m.segmento(B(x, 2.6, -az), B(x + 0.3, 0.3, az * 0.4), 0.09, 0.09, 4, "mat_madera_gris")
        else:
            m.segmento(B(x, 2.8, -az), B(x, 2.8, az), 0.09, 0.09, 4, "mat_madera_gris")
    for k in range(5):
        caja(m, (rnd.uniform(-2, 1.2), 0, rnd.uniform(-1.5, 0.8)),
             (rnd.uniform(1.3, 2.2), 0.1, rnd.uniform(0.9, 1.6)), "mat_madera_gris",
             rot=(rnd.uniform(-8, 8), 0, rnd.uniform(0, 180)))
    m.caja(B(-1.0, 3.1, -0.6), (2.6, 1.5, 0.08), "mat_tejas", rot=(0, -28, 0))       # un trozo
    col(m, (-ax, 0, -az), (ax, 2.9, -az + G))
    col(m, (-ax, 0, -az), (-ax + G, 2.9, az))
    col(m, (ax - G, 0, -az), (ax, 2.9, az))
    col(m, (-ax, 0, az - G), (ax, 2.9, az))
    return m.objeto("casa_ruina", col_)


def campanario(col_):
    m = rc.Malla()
    P = "mat_sillar_viejo"
    for x0, x1, z0, z1 in ((-1.1, -0.7, -1.1, 1.1), (0.7, 1.1, -1.1, 1.1)):
        caja(m, (x0, 0, z0), (x1, 5.0, z1), P)
    for z0, z1 in ((-1.1, -0.7), (0.7, 1.1)):
        caja(m, (-0.7, 0, z0), (0.7, 3.0, z1), P)
    caja(m, (-1.1, 4.6, -1.1), (1.1, 5.0, 1.1), P)                       # el arco de arriba
    caja(m, (-1.2, 5.0, -1.2), (0.3, 5.5, 1.2), P)                       # el remate, roto
    m.segmento(B(-0.7, 4.5, 0), B(0.7, 4.5, 0), 0.06, 0.06, 4, "mat_madera_gris")
    m.anillos(B(0, 3.75, 0), [(0.0, 0.45, 0.45), (0.12, 0.4, 0.4), (0.45, 0.28, 0.28),
                              (0.7, 0.2, 0.2), (0.75, 0.0, 0.0)], 10, "mat_metal", tapa_abajo=False)
    caja(m, (-0.04, 4.45, -0.04), (0.04, 4.55, 0.04), "mat_metal")
    col(m, (-1.1, 0, -1.1), (1.1, 3.0, 1.1))
    return m.objeto("campanario", col_)


def pozo_tapado(col_):
    m = rc.Malla()
    rnd = random.Random(3)
    m.anillos(B(0, 0, 0), [(0.0, 0.85, 0.85), (0.75, 0.85, 0.85), (0.75, 0.65, 0.65), (0.0, 0.65, 0.65)],
              10, "mat_piedra_musgo", tapa_abajo=False, tapa_arriba=False)
    for k in range(9):
        a, r = rnd.uniform(0, 2 * math.pi), rnd.uniform(0, 0.45)
        m.prisma(B(r * math.cos(a), 0.55, r * math.sin(a)), rnd.uniform(0.12, 0.2), 0.08, 0.2, 5,
                 "mat_roca", giro=rnd.uniform(0, 60))
    for k, ang in enumerate((10, -14)):
        m.caja(B(0, 0.8 + 0.04 * k, 0), (1.9, 0.22, 0.05), "mat_madera_gris", rot=(0, 0, ang))
    col(m, (-0.85, 0, -0.85), (0.85, 0.8, 0.85))
    return m.objeto("pozo_tapado", col_)


def cruz_pequena(col_):
    m = rc.Malla()
    caja(m, (-0.035, 0, -0.025), (0.035, 0.75, 0.025), "mat_madera")
    caja(m, (-0.22, 0.48, -0.025), (0.22, 0.55, 0.025), "mat_madera")
    m.decal(B(0, 0.515, 0.026), (0, -1, 0), 0.3, 0.04, "mat_oscuro", salida=0.002)    # el nombre
    return m.objeto("cruz_pequena", col_)


def tumba_abierta(col_):
    m = rc.Malla()
    quad(m, [(-0.5, 0.012, 1.0), (0.5, 0.012, 1.0), (0.5, 0.012, -1.0), (-0.5, 0.012, -1.0)], "mat_oscuro")
    for x0, x1, z0, z1 in ((-0.62, -0.5, -1.1, 1.1), (0.5, 0.62, -1.1, 1.1), (-0.5, 0.5, 1.0, 1.1),
                           (-0.5, 0.5, -1.1, -1.0)):
        caja(m, (x0, 0, z0), (x1, 0.06, z1), "mat_tierra_suelo")
    m.anillos(B(1.3, 0, 0), [(0.0, 0.6, 1.0), (0.25, 0.45, 0.8), (0.45, 0.2, 0.4), (0.5, 0.0, 0.0)],
              8, "mat_tierra_suelo")                                                # monton
    # pala clavada en el monton
    m.segmento(B(1.25, 0.35, 0.2), B(1.15, 1.4, 0.35), 0.022, 0.02, 5, "mat_madera")
    caja(m, (1.17, 0.12, 0.13), (1.37, 0.42, 0.17), "mat_metal", rot=(0, 0, 6))
    caja(m, (1.08, 1.38, 0.32), (1.24, 1.42, 0.38), "mat_madera")
    col(m, (-0.62, 0, -1.1), (0.62, 0.8, 1.1))
    col(m, (0.8, 0, -1.0), (1.9, 0.5, 1.0))
    return m.objeto("tumba_abierta", col_)


# ---------------------------------------------------------------------------
# El arbol hueco
# ---------------------------------------------------------------------------
def arbol_hueco(col_):
    m = rc.Malla()
    rnd = random.Random(13)
    C = "mat_corteza"
    sectores = 16
    hueco = 3.0                                   # alto del hueco, por delante (+Z)
    for k in range(sectores):
        a0 = 2 * math.pi * k / sectores
        a1 = 2 * math.pi * (k + 1) / sectores
        frente = abs(math.sin((a0 + a1) / 2) - 1.0) < 0.35 and math.cos((a0 + a1) / 2) > -0.6
        for (y0, y1, r0, r1) in ((0.0, 3.0, 2.4, 2.0), (3.0, 6.5, 2.0, 1.5), (6.5, 9.5, 1.5, 0.9)):
            if frente and y0 < hueco:
                continue
            p = []
            for (y, r) in ((y0, r0), (y1, r1)):
                for a in (a0, a1):
                    rr = r * (1 + 0.12 * math.sin(a * 5 + y))
                    p.append((rr * math.cos(a), y, rr * math.sin(a)))
            pts = [p[0], p[1], p[3], p[2]]
            f = m.poligono([B(*q) for q in pts], C)
            f.normal_update()
            centro = Vector((0, 0, (y0 + y1) / 2))
            if f.normal.dot(f.calc_center_median() - centro) < 0:
                f.normal_flip()
            # pared de dentro, oscura (se ve por el hueco)
            g = m.poligono([B(q[0] * 0.8, q[1], q[2] * 0.8) for q in pts], "mat_oscuro")
            g.normal_update()
            if g.normal.dot(g.calc_center_median() - centro) > 0:
                g.normal_flip()
    # suelo de dentro y el borde del hueco
    m.anillos(B(0, 0.02, 0), [(0.0, 1.8, 1.8), (0.01, 0.0, 0.0)], 12, "mat_tierra_suelo", tapa_abajo=False)
    # raices
    for k in range(9):
        a = 2 * math.pi * k / 9 + rnd.uniform(-0.2, 0.2)
        if math.sin(a) > 0.8:
            continue
        m.segmento(B(2.2 * math.cos(a), 0.6, 2.2 * math.sin(a)),
                   B(4.3 * math.cos(a), 0.0, 4.3 * math.sin(a)), 0.35, 0.08, 5, C)
    # ramas secas arriba; de las bajas cuelgan campanillas
    campanas = []
    for k in range(8):
        a = 2 * math.pi * k / 8 + rnd.uniform(-0.3, 0.3)
        y = rnd.uniform(6.5, 9.0)
        fin = (5.5 * math.cos(a), y + rnd.uniform(1.5, 3.5), 5.5 * math.sin(a))
        m.segmento(B(1.2 * math.cos(a), y, 1.2 * math.sin(a)), B(*fin), 0.35, 0.06, 5, C)
        if k % 2 == 0:
            mid = (3.5 * math.cos(a), y + 1.0, 3.5 * math.sin(a))
            campanas.append(mid)
    for x, y, z in campanas:
        m.segmento(B(x, y, z), B(x, y - 1.6, z), 0.008, 0.008, 3, "mat_lona")
        m.anillos(B(x, y - 1.82, z), [(0.0, 0.07, 0.07), (0.14, 0.04, 0.04), (0.18, 0.0, 0.0)], 6,
                  "mat_metal", tapa_abajo=False)
    col(m, (-2.3, 0, -2.3), (2.3, 3.0, 1.2))
    col(m, (-2.3, 0, 1.2), (-0.9, 3.0, 2.3))
    col(m, (0.9, 0, 1.2), (2.3, 3.0, 2.3))
    return m.objeto("arbol_hueco", col_)


def zapatitos(col_):
    m = rc.Malla()
    rnd = random.Random(5)
    mats = ["mat_tela_roja", "mat_madera", "mat_lona", "mat_oscuro", "mat_lona_naranja", "mat_flor_blanca"]
    for k in range(11):
        x, z = rnd.uniform(-0.45, 0.45), rnd.uniform(-0.35, 0.35)
        y = 0.0 if k < 7 else 0.07
        giro = rnd.uniform(0, 180)
        mat = mats[k % len(mats)]
        m.caja(B(x, y + 0.03, z), (0.07, 0.15, 0.06), mat, rot=(rnd.uniform(-15, 15), 0, giro))
        m.caja(B(x, y + 0.07, z), (0.06, 0.06, 0.06), mat, rot=(0, 0, giro))
    return m.objeto("zapatitos", col_)


PIEZAS = [
    (tienda, "Tienda de lona naranja, abierta por delante (+Z)."),
    (tienda_caida, "Tienda hundida, con un palo roto."),
    (fogata, "Fogata apagada con olla en un tripode."),
    (mochila, "Mochila naranja."),
    (farol_suelo, "Farol en el suelo; el cristal es mat_ventana_luz (luz en la entidad)."),
    (radio, "Radio portatil con antena."),
    (botas, "Un par de botas."),
    (cartel_busca, "Cartel de SE BUSCA (0.42 x 0.56), mira a +Z; ponerlo pegado a un tronco."),
    (casa_ruina, "Casa en ruinas 5.2 x 4.2 con una X roja en la puerta (+Z)."),
    (campanario, "Campanario roto con la campana."),
    (pozo_tapado, "Pozo lleno de piedras y tapado con tablas."),
    (cruz_pequena, "Cruz pequena de madera."),
    (tumba_abierta, "Fosa abierta con su monton de tierra y una pala clavada."),
    (arbol_hueco, "Arbol hueco enorme con la abertura al frente (+Z, 3 m de alto)."),
    (zapatitos, "Monton de zapatos de nino."),
    (farol_mano, "Farol de mano de los aldeanos."),
]


def construir():
    sc = rc.escena()
    col_ = rc.coleccion("bosque_historia", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col_)
        ob.location = (800 + (k % 6) * 12, 800 + (k // 6) * 12, 0)
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
