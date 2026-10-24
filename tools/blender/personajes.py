"""
Personajes low-poly para RetroEngine: granjero, aldeana, anciano y nino.

Compatibles con los atlas de 128x128 de persona_m / persona_f (y con tools/charatlas.py): cada
parte del cuerpo cae en la misma region del atlas (cara, nuca, pelo, piel, torso, mangas,
antebrazos, pantalon, zapatos). La textura se elige en la escena con "texture"; los
complementos (sombrero de paja, horca, baston) traen su propio material y no cambian.

  Pies en el origen, mirando a +Z del motor (-Y en Blender). Sin esqueleto: el motor los anima
  con "idle" (respiracion) y "facePlayer".

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402

importlib.reload(rc)

import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

# Regiones del atlas (x0, y0, x1, y1) en pixeles, como en charatlas.py.
R = dict(FACE=(0, 0, 48, 56), HEADBACK=(48, 0, 96, 56), HAIR=(96, 0, 128, 32),
         SKIN=(96, 32, 128, 48), TORSO=(0, 56, 96, 96), SLEEVE=(96, 48, 128, 80),
         FOREARM=(96, 80, 128, 112), PANTS=(0, 96, 64, 128), SHOES=(64, 96, 96, 128))
ATLAS = 128.0
SLOT_ATLAS = "atlas"  # material sin textura propia: recibe la "texture" de la entidad


def en_region(region, ul, vl):
    """(ul, vl) en 0..1 dentro de la region -> UV de Blender (v hacia arriba). Medio pixel de
    margen para que el filtro no mezcle con la region vecina."""
    x0, y0, x1, y1 = R[region]
    ul = min(max(ul, 0.0), 1.0)
    vl = min(max(vl, 0.0), 1.0)
    px = x0 + 0.5 + ul * (x1 - x0 - 1)
    py = y1 - 0.5 - vl * (y1 - y0 - 1)
    return px / ATLAS, 1.0 - py / ATLAS


def angulo(co):
    """Angulo alrededor del eje Z: 0 = frente (-Y), +-pi = espalda."""
    return math.atan2(co.x, -co.y)


def cilindrico(region, z0, z1):
    """UV por cilindro: alrededor -> u (el frente al centro de la region), altura -> v. Los
    vertices de la costura (espalda) se colocan del lado de su cara."""
    def uv(co, cara):
        c = cara.calc_center_median()
        ac = angulo(c)
        a = angulo(co)
        if abs(co.x) < 1e-6 and abs(co.y) < 1e-6:
            a = ac  # el centro de una tapa
        if a - ac > math.pi:
            a -= 2 * math.pi
        elif ac - a > math.pi:
            a += 2 * math.pi
        return en_region(region, 0.5 + a / (2 * math.pi), (co.z - z0) / max(z1 - z0, 1e-6))
    return uv


def plano_frontal(region, x0, x1, z0, z1):
    def uv(co, cara):
        return en_region(region, (co.x - x0) / (x1 - x0), (co.z - z0) / (z1 - z0))
    return uv


def plano_superior(region, r):
    def uv(co, cara):
        return en_region(region, (co.x + r) / (2 * r), (co.y + r) / (2 * r))
    return uv


class Cuerpo:
    """Parametros de un personaje. Medidas en metros para una altura de 1.75."""

    def __init__(self, nombre, altura=1.75, hombros=0.21, cadera=0.15, cabeza=1.0, falda=False,
                 abrigo=False, joroba=0.0, pelo_largo=False, brazo_der="abajo",
                 brazo_izq="abajo", sombrero=False, capucha=False, gabardina=False, herramienta=None,
                 atlas="persona_m1_tex"):
        self.nombre = nombre
        self.k = altura / 1.75
        self.hombros = hombros
        self.cadera = cadera
        self.cabeza = cabeza
        self.falda = falda
        self.abrigo = abrigo
        self.joroba = joroba
        self.pelo_largo = pelo_largo
        self.brazo_der = brazo_der
        self.brazo_izq = brazo_izq
        self.sombrero = sombrero
        self.capucha = capucha  # capucha y esclavina del color del pantalon (la tunica)
        self.gabardina = gabardina  # abrigo largo hasta los tobillos, abierto por delante
        self.herramienta = herramienta
        self.atlas = atlas
        self.luz = None  # si lleva farol: su posicion (motor), para el "light" de la entidad


def construir(c, col):
    m = rc.Malla()
    k = c.k
    A = SLOT_ATLAS

    def P(x, y, z):
        return Vector((x * k, y * k, z * k))

    # --- piernas y pies (no se ven bajo la falda) ---
    for lado in (-1, 1):
        x = 0.095 * lado
        if not c.falda:
            m.segmento(P(x, 0, 0.47), P(x, 0, 0.86), 0.075 * k, 0.095 * k, 6, A,
                       cilindrico("PANTS", 0, 0.39 * k))
            m.segmento(P(x, 0, 0.07), P(x, 0, 0.48), 0.062 * k, 0.074 * k, 6, A,
                       cilindrico("PANTS", 0, 0.41 * k))
        # bota: caja algo alargada hacia delante
        caras = m.caja(P(x, -0.04, 0.045), (0.12 * k, 0.27 * k, 0.09 * k), A)
        for f in caras:
            m.uv_fijas[f] = [en_region("SHOES", 0.5 + l.vert.co.x * 3, 0.5 - l.vert.co.y * 1.8)
                             for l in f.loops]

    # --- cadera ---
    zc = 0.86
    m.anillos(P(0, 0, 0), [(0.82 * k, c.cadera * k, 0.10 * k), (0.97 * k, c.cadera * k, 0.105 * k)],
              8, A, cilindrico("PANTS", 0.82 * k, 0.97 * k))

    # --- falda o faldon del abrigo ---
    if c.falda:
        m.anillos(P(0, 0, 0), [(0.08 * k, 0.25 * k, 0.21 * k), (0.5 * k, 0.21 * k, 0.17 * k),
                               (0.98 * k, 0.16 * k, 0.11 * k)], 8, A,
                  cilindrico("PANTS", 0.08 * k, 0.98 * k), tapa_arriba=False)
    if c.abrigo:
        # el faldon usa solo el 60 % de abajo de la region del torso: arriba esta el escote, que
        # no debe repetirse en la cintura
        m.anillos(P(0, 0, 0), [(0.5 * k, 0.21 * k, 0.16 * k), (1.0 * k, 0.17 * k, 0.12 * k)], 8, A,
                  cilindrico("TORSO", 0.5 * k, 0.5 * k + 0.5 * k / 0.6), tapa_arriba=False)

    # --- torso (la joroba lo inclina hacia delante) ---
    j = c.joroba
    h = c.hombros
    torso = [(0.95, c.cadera - 0.005, 0.10), (1.12, c.cadera + 0.005, 0.11),
             (1.30, h - 0.02, 0.125), (1.42, h, 0.115), (1.49, h * 0.55, 0.085)]
    caras = m.anillos(P(0, 0, 0), [(z * k, rx * k, ry * k) for z, rx, ry in torso], 8, A,
                      cilindrico("TORSO", 0.95 * k, 1.49 * k))
    _inclinar(caras, 0.95 * k, j)

    if c.gabardina:
        # abrigo largo por encima del torso y del faldon, con cuello alto. Se le quita la cara
        # de delante (abierto: asoma la ropa de debajo). Region del pantalon, como la capucha.
        def abierta(caras, hasta):
            frente = [f for f in caras if f.calc_center_median().y < -0.05 * k
                      and abs(f.calc_center_median().x) < 0.07 * k
                      and f.calc_center_median().z < hasta]
            bmesh.ops.delete(m.bm, geom=frente, context="FACES")
        abrigo = [(0.1, 0.31, 0.26), (0.5, 0.26, 0.21), (0.95, c.cadera + 0.05, 0.145),
                  (1.3, h + 0.025, 0.15), (1.45, h + 0.03, 0.13)]
        caras = m.anillos(P(0, 0, 0), [(z * k, rx * k, ry * k) for z, rx, ry in abrigo], 8, A,
                          cilindrico("PANTS", 0.1 * k, 1.45 * k), tapa_abajo=False,
                          tapa_arriba=False)
        _inclinar(caras, 0.95 * k, j)
        abierta(caras, 1.25 * k)
        if not c.capucha:  # con capucha sobra: ella misma cae sobre los hombros
            cuello_alto = m.anillos(P(0, 0, 0), [(1.43 * k, (h * 0.75) * k, 0.12 * k),
                                                 (1.66 * k, 0.12 * k, 0.12 * k)], 8, A,
                                    cilindrico("PANTS", 1.43 * k, 1.66 * k), tapa_abajo=False,
                                    tapa_arriba=False)
            _inclinar(cuello_alto, 0.95 * k, j)
            abierta(cuello_alto, 2.0 * k)

    def inclinado(p):
        """Punto del torso desplazado por la joroba (hacia -Y al subir)."""
        dz = max(p.z - 0.95 * k, 0.0)
        return Vector((p.x, p.y - dz * math.tan(math.radians(j)), p.z - dz * (1 - math.cos(math.radians(j)))))

    # --- cuello y cabeza ---
    cuello = inclinado(P(0, 0, 1.47))
    m.segmento(cuello, cuello + Vector((0, 0, 0.1 * k)), 0.05 * k, 0.05 * k, 6, A,
               cilindrico("SKIN", 0, 0.1 * k))
    s = c.cabeza * k
    cab = [(0.0, 0.062, 0.068), (0.06, 0.084, 0.094), (0.13, 0.096, 0.106), (0.20, 0.096, 0.106),
           (0.26, 0.08, 0.09), (0.285, 0.045, 0.05)]
    zmax = cab[-1][0] * s
    rxm = 0.096 * s
    frente = plano_frontal("FACE", -rxm, rxm, 0.0, zmax)
    nuca = cilindrico("HEADBACK", 0.0, zmax)
    pelo = plano_superior("HAIR", 0.06 * s)

    def uv_cabeza(co, cara):
        n = cara.normal
        if n.z > 0.8:
            return pelo(co, cara)
        if n.y < -0.35:
            return frente(co, cara)
        return nuca(co, cara)

    base_cab = cuello + Vector((0, 0, 0.07 * k))
    m.anillos(base_cab, [(z * s, rx * s, ry * s) for z, rx, ry in cab], 8, A, uv_cabeza)

    if c.pelo_largo:
        # melena: bloque detras de la cabeza hasta los hombros
        m.anillos(base_cab + Vector((0, 0.05 * s, -0.18 * k)),
                  [(0.0, 0.11 * s, 0.05 * s), (0.36 * k, 0.1 * s, 0.07 * s)], 6, A,
                  cilindrico("HAIR", 0.0, 0.36 * k), giro=0)

    # --- brazos ---
    for lado, pose in ((1, c.brazo_der), (-1, c.brazo_izq)):
        hombro = inclinado(P(h * lado * 1.02, 0, 1.40))
        if pose == "sujeta":      # antebrazo hacia delante, como agarrando un palo
            codo = hombro + Vector((0.04 * lado * k, 0.02 * k, -0.27 * k))
            muneca = codo + Vector((0.0, -0.24 * k, 0.04 * k))
        elif pose == "baston":    # apoyado en un baston, algo adelantado
            codo = hombro + Vector((0.05 * lado * k, -0.06 * k, -0.26 * k))
            muneca = codo + Vector((0.02 * lado * k, -0.12 * k, -0.2 * k))
        else:                     # caido, un poco separado del cuerpo
            codo = hombro + Vector((0.06 * lado * k, 0.0, -0.28 * k))
            muneca = codo + Vector((0.025 * lado * k, -0.02 * k, -0.25 * k))
        m.segmento(hombro, codo, 0.06 * k, 0.052 * k, 6, A, cilindrico("SLEEVE", 0, 0.29 * k))
        m.segmento(codo, muneca, 0.05 * k, 0.042 * k, 6, A, cilindrico("FOREARM", 0, 0.26 * k))
        mano = muneca + (muneca - codo).normalized() * 0.05 * k
        m.segmento(muneca, mano + (muneca - codo).normalized() * 0.04 * k, 0.045 * k, 0.035 * k,
                   4, A, cilindrico("SKIN", 0, 0.09 * k), ovalo=(1.0, 0.6))
        if lado == 1:
            mano_der = mano

    # --- complementos con material propio ---
    if c.sombrero:
        tope = base_cab + Vector((0, 0, 0.2 * s))
        m.anillos(tope, [(0.0, 0.24 * s, 0.25 * s), (0.025 * s, 0.24 * s, 0.25 * s)], 10,
                  "mat_paja")
        m.anillos(tope, [(0.0, 0.11 * s, 0.12 * s), (0.12 * s, 0.1 * s, 0.11 * s),
                         (0.14 * s, 0.07 * s, 0.075 * s)], 8, "mat_paja")
    if c.capucha:
        # capucha: una cabeza mas grande y abierta por delante (se quitan las caras que tapan
        # la cara), y una esclavina sobre los hombros. Usan la region del pantalon, que en una
        # tunica se pinta del mismo color que el faldon.
        # capucha de sudadera: tela que cae desde la coronilla (redonda, sin punta) hasta los
        # hombros, holgada alrededor de la cabeza; solo se abre la cara (cerrada bajo la
        # barbilla, como una bufanda).
        caras = m.anillos(base_cab + Vector((0, 0.03 * s, 0)),
                          [(z * s, rx * s, ry * s) for z, rx, ry in
                           [(-0.3, 0.2, 0.17), (-0.16, 0.165, 0.152), (-0.02, 0.145, 0.148),
                            (0.1, 0.152, 0.158), (0.19, 0.152, 0.158), (0.27, 0.14, 0.148),
                            (0.34, 0.112, 0.12), (0.39, 0.065, 0.072), (0.415, 0.0, 0.0)]],
                          10, A, cilindrico("PANTS", -0.3 * s, 0.415 * s), tapa_abajo=False,
                          tapa_arriba=False)
        for v in {v for f in caras for v in f.verts}:
            dz = v.co.z - base_cab.z
            if 0.17 * s < dz < 0.3 * s:
                v.co.y -= 0.045 * s     # el borde de arriba cae sobre la frente...
            if 0.17 * s < dz < 0.21 * s:
                v.co.z -= 0.02 * s      # ...hasta las cejas
        fy = base_cab.y - 0.05 * s
        frente = []
        for f in caras:
            cm = f.calc_center_median()
            dz = cm.z - base_cab.z
            if cm.y >= fy:
                continue
            if -0.06 * s < dz < 0.19 * s:  # la cara, de la barbilla a las cejas
                frente.append(f)
        bmesh.ops.delete(m.bm, geom=frente, context="FACES")
        hombros = inclinado(P(0, 0, 1.28))
        m.anillos(Vector((hombros.x, hombros.y, 0)),
                  [(1.28 * k, (h + 0.05) * k, 0.16 * k), (1.47 * k, 0.12 * k, 0.12 * k)], 8, A,
                  cilindrico("PANTS", 1.28 * k, 1.47 * k), tapa_abajo=False, tapa_arriba=False)
    if c.herramienta == "horca":
        base = Vector((mano_der.x, mano_der.y, 0.02))
        m.segmento(base, base + Vector((0, 0, 1.75 * k)), 0.018, 0.018, 5, "mat_madera")
        cruz = base + Vector((0, 0, 1.75 * k))
        m.caja(cruz, (0.24 * k, 0.03, 0.03), "mat_metal")
        for dx in (-0.1, 0.0, 0.1):
            m.caja(cruz + Vector((dx * k, 0, 0.14 * k)), (0.018, 0.018, 0.28 * k), "mat_metal")
    if c.herramienta == "pala":
        # pala apoyada en el suelo, sujeta con la mano derecha
        base = Vector((mano_der.x, mano_der.y, 0.32))
        m.segmento(base, base + Vector((0, 0, 1.25 * k)), 0.02, 0.02, 5, "mat_madera")
        m.caja(base + Vector((0, 0, 1.25 * k)), (0.14 * k, 0.03, 0.03), "mat_madera")
        m.caja(base + Vector((0, 0, -0.16)), (0.24, 0.035, 0.32), "mat_metal")
    if c.herramienta == "farol":
        # farol de mano colgando bajo el puno: cuatro cristales encendidos y marco de hierro
        f = mano_der + Vector((0, 0, -0.2 * k))
        r = 0.07
        for n in ((0, -1, 0), (0, 1, 0), (-1, 0, 0), (1, 0, 0)):
            nv = Vector(n)
            m.decal(f + nv * r, nv, 2 * r - 0.02, 0.17, "mat_ventana_luz", salida=0.0)
        m.prisma(f + Vector((0, 0, 0.085)), 0.11, 0.0, 0.08, 4, "mat_metal", giro=45)
        m.prisma(f + Vector((0, 0, -0.105)), 0.1, 0.1, 0.03, 4, "mat_metal", giro=45)
        m.segmento(f + Vector((0, 0, 0.16)), mano_der, 0.008, 0.008, 4, "mat_metal")
        c.luz = (f.x, f.z, -f.y)  # posicion del farol en coordenadas del motor
    if c.herramienta == "baston":
        m.segmento(Vector((mano_der.x + 0.02, mano_der.y - 0.03, 0.0)), mano_der + Vector((0, 0, 0.06)),
                   0.017, 0.02, 5, "mat_madera")

    # colision: un bloque del tamano del cuerpo (los brazos y la herramienta no cuentan)
    m.colision((0, 0, 0.9 * k), (0.5 * k, 0.4 * k, 1.8 * k))
    ob = m.objeto(c.nombre, col)
    _material_de_atlas(ob, c.atlas)
    return ob


def _inclinar(caras, z0, grados):
    """Inclina hacia delante (-Y) lo que esta por encima de z0 (la joroba del anciano)."""
    if not grados:
        return
    t = math.tan(math.radians(grados))
    verts = {v for f in caras for v in f.verts}
    for v in verts:
        dz = max(v.co.z - z0, 0.0)
        v.co.y -= dz * t


def _material_de_atlas(ob, atlas):
    """En Blender, el hueco "atlas" muestra la textura indicada para poder ver el personaje.
    Al exportar se cambia por un material sin textura (ver exportar)."""
    vista = rc.material(atlas)
    for i, mat in enumerate(ob.data.materials):
        if mat is not None and mat.name == SLOT_ATLAS:
            ob.data.materials[i] = vista
    ob["atlas_vista"] = atlas


def exportar(ob, comentario):
    """Exporta con el hueco del atlas SIN textura (en el .mtl no lleva map_Kd), para que la
    entidad elija la textura con "texture"."""
    hueco = bpy.data.materials.get(SLOT_ATLAS) or bpy.data.materials.new(SLOT_ATLAS)
    hueco.use_nodes = False
    vista = ob.get("atlas_vista")
    indices = [i for i, mat in enumerate(ob.data.materials) if mat and mat.name == vista]
    for i in indices:
        ob.data.materials[i] = hueco
    try:
        return rc.exportar([ob], ob.name, comentario)
    finally:
        for i in indices:
            ob.data.materials[i] = rc.material(vista)


PERSONAJES = [
    Cuerpo("granjero", hombros=0.23, cadera=0.16, brazo_der="sujeta", sombrero=True,
           herramienta="horca", atlas="persona_m2_tex"),
    Cuerpo("aldeana", altura=1.66, hombros=0.18, cadera=0.15, falda=True, pelo_largo=True,
           atlas="persona_f1_tex"),
    Cuerpo("anciano", altura=1.66, hombros=0.2, abrigo=True, joroba=14.0, brazo_der="baston",
           herramienta="baston", atlas="persona_m1_tex"),
    Cuerpo("nino", altura=1.25, hombros=0.19, cadera=0.145, cabeza=1.25, atlas="persona_m2_tex"),
    # cuerpos genericos, sin complementos: para caras propias hechas con charatlas.py
    Cuerpo("hombre", hombros=0.22, cadera=0.155, atlas="persona_m1_tex"),
    Cuerpo("mujer", altura=1.68, hombros=0.185, cadera=0.155, pelo_largo=True,
           atlas="persona_f1_tex"),
    Cuerpo("sepulturero", altura=1.8, hombros=0.22, abrigo=True, joroba=8.0, brazo_der="sujeta",
           herramienta="pala", atlas="persona_m1_tex"),
    Cuerpo("sereno", hombros=0.21, abrigo=True, brazo_der="sujeta", herramienta="farol",
           atlas="persona_m2_tex"),
    # el ermitano de los tuneles: gabardina negra con capucha, encorvado y con farol
    Cuerpo("encapuchado", altura=1.7, hombros=0.2, falda=True, joroba=10.0, capucha=True,
           gabardina=True, brazo_der="sujeta", herramienta="farol", atlas="persona_m1_tex"),
]


def construir_todos():
    sc = rc.escena()
    col = rc.coleccion("personajes", sc)
    obs = []
    for i, c in enumerate(PERSONAJES):
        ob = construir(c, col)
        ob.location.x = (i - 1.5) * 1.1
        obs.append(ob)
    return obs


def exportar_todos(obs):
    res = []
    for ob in obs:
        loc = tuple(ob.location)
        ob.location = (0, 0, 0)
        res.append(exportar(ob, "Personaje '" + ob.name + "'. Pies en el origen, mira a +Z.\n"
                            "Atlas 128x128 como persona_m (charatlas.py); se elige con \"texture\"."))
        ob.location = loc
    return res


if __name__ == "__main__":
    print(exportar_todos(construir_todos()))
