"""
Interior de la capilla del cementerio (escena iglesia.json) y su mobiliario.

Por fuera la capilla (cementerio.py) mide 7 x 11 m con muros de 0.3: por dentro la nave es de
6.4 x 10.4 m, con las ventanas, los contrafuertes (pilastras por dentro) y el roseton en los
mismos sitios. El suelo de dentro esta a Y=0 (por fuera el zocalo lo sube 0.35).

  capilla_interior  nave: suelo de losas, muros con zocalo de madera, cerchas de madera bajo el
                    tejado, presbiterio elevado y cortinas. Puerta hacia +Z.
  alfombra          alfombra roja del pasillo (aparte: se dibuja con "layer": 1)
  vidrieras         las ventanas de colores y el roseton (se usa con "emissive": true)
  banco_iglesia     banco de 2.4 m (el respaldo hacia +Z: se sientan mirando a -Z, al altar)
  altar             altar de marmol con mantel rojo, caliz y libro
  candelabro        pie de hierro con tres velas (llamas: sprite spr_llama en la escena)
  lampara           lampara colgante de ocho velas, con su cadena
  organo            consola con teclas de hueso y tubos de metal (frente +Z)

Ejecutar en Blender:  exec(open(ruta).read(), {"__file__": ruta, "__name__": "__main__"})
"""
import importlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retro_comun as rc  # noqa: E402
import subterraneo as st  # noqa: E402  (piezas comunes: cara, caja, pared, plano...)

importlib.reload(rc)
importlib.reload(st)

B, cara, caja, col, pared, plano, cortes = st.B, st.cara, st.caja, st.col, st.pared, st.plano, st.cortes

X, Z, H = 3.2, 5.2, 5.2          # media anchura, medio fondo y alto de los muros
CUMBRE = 8.2                     # altura de la cumbrera por dentro
PILASTRAS = (-3.9, -1.3, 1.3, 3.9)
VENTANAS = (-2.6, 0.0, 2.6)      # z de las ventanas laterales
Y_VENTANA = 2.55
Z_PRESBITERIO = -2.6             # el presbiterio (elevado 0.3) va de -Z a aqui
ALTO_PRESBITERIO = 0.3
CADENA = 4.0                     # largo de la cadena de la lampara


def capilla_interior(col_):
    m = rc.Malla()
    plano(m, -X, X, -Z, Z, 0.0, "mat_losa", True)
    # muros (piedra humeda) y zocalo de madera
    pared(m, (-X, -Z), (-X, Z), 0.0, H, "mat_piedra_musgo", (1, 0, 0))
    pared(m, (X, -Z), (X, Z), 0.0, H, "mat_piedra_musgo", (-1, 0, 0))
    pared(m, (-X, -Z), (X, -Z), 0.0, H, "mat_piedra_musgo", (0, 0, 1))
    pared(m, (-X, Z), (X, Z), 0.0, H, "mat_piedra_musgo", (0, 0, -1))
    for lado in (-1, 1):
        caja(m, (min(lado * X, lado * (X - 0.06)), 0, -Z), (max(lado * X, lado * (X - 0.06)), 1.1, Z),
             "mat_madera_gris")
    # hastiales y faldones del tejado por dentro (tablas)
    for z, nz in ((-Z, 1), (Z, -1)):
        cara(m, [(-X, H, z), (X, H, z), (0, CUMBRE, z)], "mat_piedra_musgo", (0, 0, nz))
    zs = cortes(-Z, Z)
    for za, zb in zip(zs, zs[1:]):
        cara(m, [(-X, H, za), (0, CUMBRE, za), (0, CUMBRE, zb), (-X, H, zb)], "mat_madera_gris",
             (CUMBRE - H, -X, 0))
        cara(m, [(X, H, za), (0, CUMBRE, za), (0, CUMBRE, zb), (X, H, zb)], "mat_madera_gris",
             (-(CUMBRE - H), -X, 0))
    # pilastras y cerchas (tirante, pendolon y pares) en el mismo sitio que los contrafuertes
    for z in PILASTRAS:
        for lado in (-1, 1):
            a, b = sorted((lado * X, lado * (X - 0.28)))
            caja(m, (a, 0, z - 0.25), (b, H, z + 0.25), "mat_piedra_musgo", colision=True)
        caja(m, (-X, H - 0.3, z - 0.12), (X, H - 0.05, z + 0.12), "mat_madera")
        caja(m, (-0.1, H - 0.05, z - 0.1), (0.1, CUMBRE - 0.15, z + 0.1), "mat_madera")
        for lado in (-1, 1):
            m.segmento(B(lado * (X - 0.1), H - 0.05, z), B(0, CUMBRE - 0.2, z), 0.1, 0.1, 4, "mat_madera")
    # presbiterio elevado y cortinas detras del altar (la alfombra es un modelo aparte)
    caja(m, (-X, 0, -Z), (X, ALTO_PRESBITERIO, Z_PRESBITERIO), "mat_marmol_viejo", colision=True)
    for lado in (-1, 1):
        a, b = sorted((lado * 1.4, lado * 2.6))
        caja(m, (a, ALTO_PRESBITERIO, -Z + 0.02), (b, 4.8, -Z + 0.12), "mat_tela_roja")
    caja(m, (-2.8, 4.8, -Z + 0.02), (2.8, 4.9, -Z + 0.15), "mat_metal")       # barra de la cortina
    # puerta por dentro
    m.decal(B(0, 1.5, Z - 0.02), (0, 1, 0), 1.8, 3.0, "mat_puerta", salida=0.0)
    # colision: muros por fuera y la puerta
    col(m, (-X - 1, 0, -Z - 1), (-X, 9, Z + 1))
    col(m, (X, 0, -Z - 1), (X + 1, 9, Z + 1))
    col(m, (-X, 0, -Z - 1), (X, 9, -Z))
    col(m, (-X, 0, Z), (X, 9, Z + 1))
    return m.objeto("capilla_interior", col_)


def alfombra(col_):
    """Alfombra roja del pasillo y del presbiterio: va aparte para dibujarla con "layer": 1
    (apoyada en el suelo, sin parpadear con el)."""
    m = rc.Malla()
    caja(m, (-0.6, 0, Z_PRESBITERIO), (0.6, 0.01, Z - 0.4), "mat_tela_roja")
    caja(m, (-0.6, ALTO_PRESBITERIO, -4.2), (0.6, ALTO_PRESBITERIO + 0.01, Z_PRESBITERIO),
         "mat_tela_roja")
    return m.objeto("alfombra", col_)


def vidrieras(col_):
    """Las ventanas como piezas aparte: en la escena llevan "emissive": true y brillan solas."""
    m = rc.Malla()
    for z in VENTANAS:
        m.decal(B(-X + 0.03, Y_VENTANA, z), (1, 0, 0), 0.85, 2.2, "mat_vidriera", salida=0.0)
        m.decal(B(X - 0.03, Y_VENTANA, z), (-1, 0, 0), 0.85, 2.2, "mat_vidriera", salida=0.0)
    m.decal(B(0, 6.5, Z - 0.03), (0, 1, 0), 1.3, 1.3, "mat_roseton", salida=0.0)
    m.decal(B(0, 6.2, -Z + 0.03), (0, -1, 0), 0.9, 1.8, "mat_vidriera", salida=0.0)
    return m.objeto("vidrieras", col_)


def banco_iglesia(col_):
    m = rc.Malla()
    caja(m, (-1.2, 0.42, -0.22), (1.2, 0.48, 0.2), "mat_madera")               # asiento
    caja(m, (-1.2, 0.5, 0.18), (1.2, 1.0, 0.24), "mat_madera")                 # respaldo
    caja(m, (-1.15, 0.1, -0.3), (1.15, 0.16, -0.18), "mat_madera_gris")        # reclinatorio
    for x in (-1.2, 1.16):
        caja(m, (x, 0, -0.3), (x + 0.04, 1.05, 0.26), "mat_madera_gris")      # costados
    col(m, (-1.22, 0, -0.3), (1.22, 1.05, 0.26))
    return m.objeto("banco_iglesia", col_)


def altar(col_):
    m = rc.Malla()
    caja(m, (-1.0, 0, -0.45), (1.0, 0.95, 0.45), "mat_marmol_viejo")
    caja(m, (-1.1, 0.95, -0.5), (1.1, 1.05, 0.5), "mat_marmol_viejo")
    caja(m, (-1.12, 1.05, -0.52), (1.12, 1.07, 0.52), "mat_tela_roja")         # mantel
    caja(m, (-0.6, 0.4, 0.5), (0.6, 1.05, 0.53), "mat_tela_roja")               # frontal
    m.prisma(B(0.45, 1.07, 0.05), 0.06, 0.035, 0.08, 6, "mat_metal")           # caliz: pie
    m.prisma(B(0.45, 1.15, 0.05), 0.02, 0.07, 0.1, 6, "mat_metal")             # copa
    caja(m, (-0.4, 1.07, -0.15), (0.0, 1.11, 0.15), "mat_lona")                # libro abierto
    col(m, (-1.12, 0, -0.52), (1.12, 1.1, 0.53))
    return m.objeto("altar", col_)


# Donde van las llamas del candelabro (para la escena)
LLAMAS_CANDELABRO = [(-0.28, 1.6, 0.0), (0.28, 1.6, 0.0), (0.0, 1.78, 0.0)]


def candelabro(col_):
    m = rc.Malla()
    m.prisma(B(0, 0, 0), 0.25, 0.18, 0.08, 8, "mat_metal")
    caja(m, (-0.03, 0.08, -0.03), (0.03, 1.35, 0.03), "mat_metal")
    caja(m, (-0.32, 1.32, -0.025), (0.32, 1.36, 0.025), "mat_metal")
    for x, alto in ((-0.28, 0.2), (0.28, 0.2)):
        m.prisma(B(x, 1.36, 0), 0.03, 0.03, alto, 6, "mat_cera")
    m.prisma(B(0, 1.36, 0), 0.035, 0.035, 0.38, 6, "mat_cera")
    col(m, (-0.3, 0, -0.3), (0.3, 1.6, 0.3))
    return m.objeto("candelabro", col_)


def llamas_lampara(n=8, r=0.8):
    return [(r * math.cos(2 * math.pi * k / n), 0.24, r * math.sin(2 * math.pi * k / n)) for k in range(n)]


def lampara(col_):
    m = rc.Malla()
    n, r = 8, 0.8
    pts = [(r * math.cos(2 * math.pi * k / n), 0.0, r * math.sin(2 * math.pi * k / n)) for k in range(n)]
    for (x0, _, z0), (x1, _, z1) in zip(pts, pts[1:] + pts[:1]):
        m.segmento(B(x0, 0, z0), B(x1, 0, z1), 0.035, 0.035, 4, "mat_metal")
    for x, _, z in pts:
        m.prisma(B(x, 0.02, z), 0.03, 0.03, 0.18, 6, "mat_cera")
    for x, _, z in pts[::2]:
        m.segmento(B(x, 0, z), B(0, 1.2, 0), 0.012, 0.012, 4, "mat_metal")   # cadenas
    caja(m, (-0.02, 1.2, -0.02), (0.02, 1.2 + CADENA, 0.02), "mat_metal")
    return m.objeto("lampara", col_)


def organo(col_):
    m = rc.Malla()
    caja(m, (-1.0, 0, -0.45), (1.0, 0.8, 0.45), "mat_madera")                  # consola
    caja(m, (-0.85, 0.8, 0.15), (0.85, 0.86, 0.45), "mat_hueso")              # teclado
    caja(m, (-0.85, 0.86, 0.15), (0.85, 0.88, 0.2), "mat_oscuro")
    caja(m, (-1.0, 0.8, -0.45), (1.0, 1.7, 0.1), "mat_madera")                 # caja de tubos
    caja(m, (-0.5, 1.0, 0.1), (0.5, 1.4, 0.14), "mat_madera_gris")             # atril
    n = 11
    for k in range(n):
        x = -0.9 + 1.8 * k / (n - 1)
        alto = 1.6 + 1.8 * (1 - abs(x) / 0.9) ** 1.3
        m.prisma(B(x, 1.7, -0.2), 0.075, 0.075, alto, 8, "mat_metal")
        m.prisma(B(x, 1.7 + alto * 0.18, -0.2 - 0.07), 0.03, 0.03, 0.06, 4, "mat_oscuro",
                 rot=(90, 0, 0))                                               # boca del tubo
    col(m, (-1.0, 0, -0.5), (1.0, 2.6, 0.5))
    return m.objeto("organo", col_)


PIEZAS = [
    (capilla_interior, "Interior de la capilla: nave de 6.4 x 10.4 m, puerta hacia +Z."),
    (vidrieras, "Vidrieras y roseton de la capilla (usar con \"emissive\": true)."),
    (alfombra, "Alfombra roja de la capilla (usar con \"layer\": 1)."),
    (banco_iglesia, "Banco de iglesia de 2.4 m; el respaldo hacia +Z."),
    (altar, "Altar de marmol con mantel rojo."),
    (candelabro, "Candelabro de tres velas; llamas en " + str(LLAMAS_CANDELABRO) + "."),
    (lampara, "Lampara de ocho velas; el aro en el origen y la cadena hasta Y=" + str(1.2 + CADENA) + "."),
    (organo, "Organo de tubos (frente +Z)."),
]


def construir():
    sc = rc.escena()
    col_ = rc.coleccion("iglesia", sc)
    obs = []
    for k, (fn, _) in enumerate(PIEZAS):
        ob = fn(col_)
        ob.location.x = 0 if fn in (capilla_interior, vidrieras, alfombra) else 10 + k * 4
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
