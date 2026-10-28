"""
retro_comun: utilidades para construir en Blender los modelos de RetroEngine.

Se ejecuta DENTRO de Blender (por script o desde el MCP). Cada modelo es un script en esta
carpeta que usa estas piezas:

  Malla       construye la geometria con bmesh: cajas, prismas, conos y quads, cada cara con
              su material. Las UV se calculan solas proyectando cada cara sobre su propio plano
              con la escala del material (metros por repeticion), asi la textura no se estira
              y las tablas o tejas quedan "derechas" en paredes y tejados.
  material()  material de Blender con la textura de assets/textures (filtro de punto).
  exportar()  escribe assets/models/<nombre>.obj y .mtl en el formato que espera el motor.

Convenciones (las del motor):
  - Metros. Pies o base del modelo en el origen.
  - El frente del modelo mira a -Y en Blender, que al exportar es +Z en el motor
    (forward -Z, up Y). Es la direccion a la que mira facePlayer.
  - Un modelo puede tener varios materiales: el .mtl apunta a ../textures/<mat>.png.
    En el JSON de la escena NO se pone "texture" (reemplazaria todas las texturas).
"""
import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

RAIZ = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TEXTURAS = os.path.join(RAIZ, "assets", "textures")
MODELOS = os.path.join(RAIZ, "assets", "models")

# Metros que ocupa una repeticion de cada textura (densidad de texeles parecida en todo).
ESCALA_UV = {
    "mat_madera": 1.6,
    "mat_madera_gris": 1.6,
    "mat_piedra": 2.0,
    "mat_yeso": 2.4,
    "mat_tejas": 1.6,
    "mat_paja": 2.0,
    "mat_lona": 1.4,
    "mat_metal": 0.6,
    "mat_tierra": 2.2,
    "mat_tierra_suelo": 2.2,
    "mat_roca": 1.6,
    "mat_losa": 2.4,
    "mat_cripta": 2.0,
    "mat_calaveras": 1.4,
    "mat_hueso": 0.3,
    "mat_cera": 0.3,
    "mat_brillo": 0.1,
    "mat_piedra_musgo": 2.0,
    "mat_sillar_viejo": 2.6,
    "mat_marmol": 1.2,
    "mat_marmol_viejo": 1.6,
    "mat_lapida": 0.8,
    "mat_granito": 0.8,
    "mat_tela_roja": 0.6,
    "mat_sangre": 0.2,
    "mat_piedra_gastada": 1.4,
    "mat_hojarasca": 1.6,
    "mat_hojas": 1.0,
    "mat_hojas_claras": 1.0,
    "mat_pino": 0.9,
    "mat_corteza": 0.9,
    "mat_abedul": 0.9,
    "mat_plumas": 0.25,
    "mat_seta": 0.3,
    "mat_hierba": 0.3,
    "mat_flor_roja": 0.1,
    "mat_flor_amarilla": 0.1,
    "mat_flor_blanca": 0.1,
    "mat_flor_morada": 0.1,
    "mat_flor_marchita": 0.1,
    "mat_ojo": 0.1,
}


# ---------------------------------------------------------------------------
# Escena de trabajo y materiales
# ---------------------------------------------------------------------------
def escena(nombre="RetroEngine"):
    """Escena propia para no tocar la del usuario. La deja activa en la ventana."""
    sc = bpy.data.scenes.get(nombre) or bpy.data.scenes.new(nombre)
    if bpy.context.window:
        bpy.context.window.scene = sc
    sc.unit_settings.system = "METRIC"
    return sc


def coleccion(nombre, sc=None):
    """Coleccion del modelo dentro de la escena de trabajo; si ya existia, se vacia."""
    sc = sc or escena()
    col = bpy.data.collections.get(nombre)
    if col is None:
        col = bpy.data.collections.new(nombre)
    if col.name not in sc.collection.children:
        sc.collection.children.link(col)
    for ob in list(col.objects):
        datos = ob.data
        bpy.data.objects.remove(ob, do_unlink=True)
        if datos is not None and datos.users == 0 and isinstance(datos, bpy.types.Mesh):
            bpy.data.meshes.remove(datos)
    return col


def material(nombre):
    """Material con la imagen assets/textures/<nombre>.png, sin filtrado (estilo PS1)."""
    mat = bpy.data.materials.get(nombre) or bpy.data.materials.new(nombre)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    tex = next((n for n in nodes if n.type == "TEX_IMAGE"), None)
    if tex is None:
        tex = nodes.new("ShaderNodeTexImage")
        tex.location = (bsdf.location.x - 320, bsdf.location.y)
    ruta = os.path.join(TEXTURAS, nombre + ".png")
    if os.path.exists(ruta):
        img = bpy.data.images.load(ruta, check_existing=True)
        img.reload()
        tex.image = img
    tex.interpolation = "Closest"
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    rough = bsdf.inputs.get("Roughness")
    if rough is not None:
        rough.default_value = 1.0
    return mat


# ---------------------------------------------------------------------------
# Construccion de mallas
# ---------------------------------------------------------------------------
class Malla:
    """Acumula primitivas en un bmesh; cada cara recuerda su material y, si se da, sus UV."""

    def __init__(self):
        self.bm = bmesh.new()
        self.mats = []        # nombres de material, en orden de indice
        self.uv_fijas = {}    # cara -> lista de (u, v) por esquina
        self.colisiones = []  # cajas de colision: (centro, tamano, giro), coordenadas de Blender

    def _indice(self, mat):
        if mat not in self.mats:
            self.mats.append(mat)
        return self.mats.index(mat)

    def _caras_de(self, verts):
        caras = set()
        for v in verts:
            caras.update(v.link_faces)
        return caras

    def _poner(self, verts, mat, matriz):
        if matriz is not None:
            bmesh.ops.transform(self.bm, matrix=matriz, verts=verts)
        idx = self._indice(mat)
        caras = self._caras_de(verts)
        for f in caras:
            f.material_index = idx
        return caras

    def caja(self, centro, tam, mat, rot=(0.0, 0.0, 0.0)):
        """Caja de tamano tam=(x, y, z) centrada en 'centro'; rot en grados (XYZ)."""
        r = bmesh.ops.create_cube(self.bm, size=1.0)
        m = (Matrix.Translation(Vector(centro))
             @ _rot(rot)
             @ Matrix.Diagonal((tam[0], tam[1], tam[2], 1.0)))
        return self._poner(r["verts"], mat, m)

    def prisma(self, base, radio_abajo, radio_arriba, alto, lados, mat, rot=(0.0, 0.0, 0.0),
               tapas=True, giro=0.0):
        """Prisma o tronco de cono de 'lados' lados, con la base en 'base'. radio_arriba=0 da
        una piramide o cono. 'giro' rota el poligono sobre su eje (grados)."""
        r = bmesh.ops.create_cone(self.bm, cap_ends=tapas, cap_tris=False, segments=lados,
                                  radius1=radio_abajo, radius2=radio_arriba, depth=alto)
        m = (Matrix.Translation(Vector(base)) @ _rot(rot)
             @ Matrix.Rotation(math.radians(giro), 4, "Z")
             @ Matrix.Translation((0.0, 0.0, alto / 2.0)))
        return self._poner(r["verts"], mat, m)

    def colision(self, centro, tam, giro=0.0):
        """Caja de colision del modelo (no se dibuja): centro y tamano en metros, coordenadas de
        Blender, y giro sobre el eje vertical en grados. Se exporta dentro del .obj y el motor la
        usa en vez de la caja de todo el modelo. Solo hace falta cubrir lo que estorba a la
        altura del jugador (paredes, postes...), no los tejados."""
        self.colisiones.append((tuple(centro), tuple(tam), float(giro)))

    def octogono(self, base, apotema, alto):
        """Colision octogonal exacta (pila, pozo, torre) con un lado mirando a cada eje: la union
        de cuatro franjas giradas 0, 45, 90 y 135 grados."""
        lado = 2 * apotema * math.tan(math.radians(22.5))
        centro = (base[0], base[1], base[2] + alto / 2)
        for giro in (0, 45, 90, 135):
            self.colision(centro, (2 * apotema, lado, alto), giro)

    def _uv_locales(self, verts, uv):
        """Si se da 'uv', calcula las UV de las caras nuevas AHORA, en coordenadas locales de
        la pieza (antes de moverla): uv(co, cara) -> (u, v)."""
        caras = self._caras_de(verts)
        if uv is not None:
            for f in caras:
                f.normal_update()  # una cara recien creada aun no tiene la normal calculada
                self.uv_fijas[f] = [uv(l.vert.co.copy(), f) for l in f.loops]
        return caras

    def segmento(self, p0, p1, r0, r1, lados, mat, uv=None, tapas=True, ovalo=(1.0, 1.0),
                 giro=0.0):
        """Prisma de p0 a p1 (brazos, piernas, palos). En coordenadas locales el eje va de
        z=0 a z=largo y el frente es -Y; la pieza se orienta manteniendo el frente hacia -Y
        del mundo. 'ovalo' escala la seccion en x e y."""
        p0, p1 = Vector(p0), Vector(p1)
        largo = (p1 - p0).length
        r = bmesh.ops.create_cone(self.bm, cap_ends=tapas, cap_tris=False, segments=lados,
                                  radius1=r0, radius2=r1, depth=largo)
        local = (Matrix.Translation((0, 0, largo / 2))
                 @ Matrix.Rotation(math.radians(giro), 4, "Z")
                 @ Matrix.Diagonal((ovalo[0], ovalo[1], 1.0, 1.0)))
        bmesh.ops.transform(self.bm, matrix=local, verts=r["verts"])
        self._uv_locales(r["verts"], uv)
        return self._poner(r["verts"], mat, Matrix.Translation(p0) @ _alinear(p1 - p0))

    def anillos(self, centro, anillos, lados, mat, uv=None, tapa_abajo=True, tapa_arriba=True,
                giro=None):
        """Forma de revolucion a trozos: anillos [(z, radio_x, radio_y), ...] de abajo arriba
        alrededor de 'centro' (torso, cabeza, falda). giro por defecto: una cara mirando al
        frente. Radio 0 en el ultimo anillo = punta."""
        giro = (180.0 / lados) if giro is None else giro
        filas = []
        for z, rx, ry in anillos:
            fila = []
            for k in range(lados):
                a = math.radians(giro + 360.0 * k / lados - 90.0)
                fila.append(self.bm.verts.new((rx * math.cos(a), ry * math.sin(a), z)))
            filas.append(fila)
        nuevas = [v for fila in filas for v in fila]
        for f0, f1 in zip(filas, filas[1:]):
            for k in range(lados):
                k1 = (k + 1) % lados
                self.bm.faces.new((f0[k], f0[k1], f1[k1], f1[k]))
        if tapa_abajo:
            self.bm.faces.new(list(reversed(filas[0])))
        if tapa_arriba:
            self.bm.faces.new(filas[-1])
        self._uv_locales(nuevas, uv)
        return self._poner(nuevas, mat, Matrix.Translation(Vector(centro)))

    def poligono(self, puntos, mat):
        """Cara libre (por ejemplo el triangulo de un hastial), con UV automaticas."""
        f = self.bm.faces.new([self.bm.verts.new(Vector(p)) for p in puntos])
        f.material_index = self._indice(mat)
        return f

    def decal(self, centro, normal, ancho, alto, mat, salida=0.02):
        """Puerta o ventana: quad de ancho x alto pegado a una pared que mira hacia 'normal'
        (horizontal). 'centro' es el centro de la pieza sobre la pared."""
        n = Vector(normal).normalized()
        der = Vector((0, 0, 1)).cross(n).normalized()  # derecha vista desde delante
        arr = Vector((0, 0, 1))
        c = Vector(centro) + n * salida
        w, h = der * (ancho / 2), arr * (alto / 2)
        return self.quad([c - w - h, c + w - h, c + w + h, c - w + h], mat)

    def quad(self, esquinas, mat, uvs=((0, 0), (1, 0), (1, 1), (0, 1))):
        """Cara de 4 esquinas (orden antihorario visto desde delante) con UV explicitas: para
        piezas que no se repiten, como una puerta o una ventana."""
        vs = [self.bm.verts.new(Vector(p)) for p in esquinas]
        f = self.bm.faces.new(vs)
        f.material_index = self._indice(mat)
        self.uv_fijas[f] = list(uvs)
        return f

    def objeto(self, nombre, col):
        """Crea el objeto de Blender con las UV calculadas y los materiales asignados."""
        bm = self.bm
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        uv = bm.loops.layers.uv.verify()
        for f in bm.faces:
            if f in self.uv_fijas:
                for loop, (u, v) in zip(f.loops, self.uv_fijas[f]):
                    loop[uv].uv = (u, v)
                continue
            escala = ESCALA_UV.get(self.mats[f.material_index], 1.0)
            ejes = _ejes_cara(f.normal)
            for loop in f.loops:
                p = loop.vert.co
                loop[uv].uv = (p.dot(ejes[0]) / escala, p.dot(ejes[1]) / escala)
        malla = bpy.data.meshes.new(nombre)
        bm.to_mesh(malla)
        bm.free()
        for poly in malla.polygons:
            poly.use_smooth = False  # caras planas: luz "a trozos" como en la PS1
        for m in self.mats:
            malla.materials.append(material(m))
        ob = bpy.data.objects.new(nombre, malla)
        col.objects.link(ob)
        ob["colisiones"] = [v for c, t, g in self.colisiones for v in (*c, *t, g)]
        return ob


def _rot(grados):
    rx, ry, rz = (math.radians(a) for a in grados)
    return (Matrix.Rotation(rz, 4, "Z") @ Matrix.Rotation(ry, 4, "Y")
            @ Matrix.Rotation(rx, 4, "X"))


def _alinear(d):
    """Rotacion que lleva el eje Z local a la direccion 'd' manteniendo el frente (-Y local)
    lo mas cerca posible de -Y del mundo."""
    z = Vector(d).normalized()
    y = Vector((0, 1, 0)) - z * z.y
    if y.length < 1e-4:
        y = Vector((0, 0, 1)) - z * z.z
    y.normalize()
    x = y.cross(z)
    m = Matrix((x, y, z)).transposed()
    return m.to_4x4()


def _ejes_cara(n):
    """Ejes (u, v) del plano de la cara: u horizontal y v "hacia arriba" por la cara. En caras
    horizontales, u = X y v = Y."""
    n = n.normalized()
    if abs(n.z) > 0.92:
        return Vector((1, 0, 0)), Vector((0, 1 if n.z > 0 else -1, 0))
    u = Vector((0, 0, 1)).cross(n).normalized()
    v = n.cross(u).normalized()
    return u, v


# ---------------------------------------------------------------------------
# Exportacion
# ---------------------------------------------------------------------------
def exportar(objetos, nombre, comentario=""):
    """Escribe assets/models/<nombre>.obj + .mtl con los ejes del motor. Devuelve un resumen."""
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    for ob in objetos:
        # un objeto oculto (o en una coleccion oculta) no se puede seleccionar ni exportar
        for col in ob.users_collection:
            col.hide_viewport = False
        ob.hide_set(False)
        ob.select_set(True)
    bpy.context.view_layer.objects.active = objetos[0]

    # Triangulado: el lector de OBJ de raylib (tinyobj) aborta con caras de mas de 5 vertices
    # (la tapa de un prisma octogonal tiene 8).
    ruta = os.path.join(MODELOS, nombre + ".obj")
    bpy.ops.wm.obj_export(filepath=ruta, export_selected_objects=True,
                          forward_axis="NEGATIVE_Z", up_axis="Y", export_materials=True,
                          path_mode="RELATIVE", export_normals=True, export_uv=True,
                          apply_modifiers=True, export_triangulated_mesh=True)

    # El .mtl: rutas con '/', color blanco (la textura manda) y sin brillo.
    ruta_mtl = os.path.splitext(ruta)[0] + ".mtl"
    lineas = []
    with open(ruta_mtl, encoding="utf-8") as f:
        for linea in f:
            clave = linea.split(" ", 1)[0]
            if clave in ("Ns", "Ka", "Ks", "Ke", "Ni", "illum"):
                continue
            if clave == "Kd":
                linea = "Kd 1.000000 1.000000 1.000000\n"
            if clave == "map_Kd":
                linea = linea.replace("\\", "/")
            lineas.append(linea)
    with open(ruta_mtl, "w", encoding="utf-8") as f:
        f.write("# RetroEngine: " + nombre + "\n")
        f.writelines(l for l in lineas if not l.startswith("#"))

    # Cabecera: el comentario y las cajas de colision ("# retro:col min max giro", en
    # coordenadas del motor: x igual, y = z de Blender, z = -y de Blender).
    cabecera = "".join("# " + c + "\n" for c in comentario.splitlines())
    datos = list(objetos[0].get("colisiones", []))
    for i in range(0, len(datos), 7):
        cx, cy, cz, sx, sy, sz, giro = datos[i:i + 7]
        c = (cx, cz, -cy)
        t = (sx, sz, sy)
        lo = [c[k] - t[k] / 2 for k in range(3)]
        hi = [c[k] + t[k] / 2 for k in range(3)]
        cabecera += "# retro:col " + " ".join(f"{v:.4f}" for v in (*lo, *hi, giro)) + "\n"
    if cabecera:
        with open(ruta, encoding="utf-8") as f:
            texto = f.read()
        with open(ruta, "w", encoding="utf-8") as f:
            f.write(cabecera + texto)

    caras = sum(len(ob.data.polygons) for ob in objetos)
    tris = sum(len(p.vertices) - 2 for ob in objetos for p in ob.data.polygons)
    return {"obj": ruta, "caras": caras, "triangulos": tris,
            "cajas": len(objetos[0].get("colisiones", [])) // 7}
