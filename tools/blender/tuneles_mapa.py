"""
Mapa de los tuneles bajo la cripta (escena tuneles.json): una cuadricula de celdas de CELDA
metros. Python puro (sin bpy) para que lo usen el modelo de Blender (subterraneo.py) y quien
arme la escena o lo revise:  python tuneles_mapa.py  dibuja el mapa y comprueba que todo este
conectado.

Leyenda (cada letra es una sala; el punto es tunel):
  #  roca               .  tunel
  A  llegada: la escalera que sube a la cripta
  B  cruce central      C  osario              D  lago subterraneo
  E  fondo del pozo de la aldea                F  salida a la plaza de noche (escalera)
  G  santuario de los cristales (el ermitano)  H  derrumbe (sin salida)
  R  reja (tunel con una reja que se abre con la llave del osario)

Coordenadas del MOTOR: la columna crece hacia +X y la fila hacia +Z; el centro de la celda
de la llegada (A) cae en el origen.
"""

CELDA = 3.0

# Salas: letra -> (columna0, fila0, columna1, fila1), ambos extremos incluidos
SALAS = {
    "G": (1, 1, 4, 3),
    "E": (9, 1, 11, 2),
    "D": (17, 2, 21, 5),
    "C": (1, 7, 3, 10),
    "B": (8, 7, 12, 9),
    "F": (20, 7, 21, 8),
    "H": (1, 12, 2, 13),
    "A": (10, 15, 12, 16),
}
# Tuneles: polilineas de celdas (cada tramo, recto en columna o en fila)
TUNELES = [
    [(11, 14), (11, 10)],                                  # llegada -> cruce
    [(11, 14), (18, 14), (18, 11), (14, 11), (14, 8), (13, 8)],  # rodeo del este
    [(7, 8), (4, 8)],                                      # cruce -> osario
    [(6, 5), (6, 12)],                                     # galeria del oeste
    [(6, 5), (2, 5), (2, 4)],                              # -> santuario
    [(6, 12), (3, 12)],                                    # -> derrumbe
    [(10, 6), (10, 3)],                                    # cruce -> pozo
    [(10, 5), (16, 5)],                                    # -> lago
    [(13, 7), (19, 7)],                                    # -> salida (con la reja)
]
REJA = (18, 7)
ANCHO, ALTO = 23, 18


def _tallar():
    m = [["#"] * ANCHO for _ in range(ALTO)]
    for t, (i0, j0, i1, j1) in SALAS.items():
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                m[j][i] = t
    for linea in TUNELES:
        for (i0, j0), (i1, j1) in zip(linea, linea[1:]):
            assert i0 == i1 or j0 == j1, "tramo no recto"
            for j in range(min(j0, j1), max(j0, j1) + 1):
                for i in range(min(i0, i1), max(i0, i1) + 1):
                    if m[j][i] == "#":
                        m[j][i] = "."
    m[REJA[1]][REJA[0]] = "R"
    return ["".join(f) for f in m]


MAPA = _tallar()

# Altura del techo por tipo de celda (metros)
ALTURA = {".": 2.6, "R": 2.6, "A": 3.2, "B": 4.6, "C": 3.6, "D": 4.6, "E": 3.4, "F": 3.4,
          "G": 4.6, "H": 2.8}

# Celda (fraccionaria) que cae en el origen del motor: el centro de la sala A
ORIGEN = (11.0, 15.5)


def celda(i, j):
    if j < 0 or j >= len(MAPA) or i < 0 or i >= len(MAPA[j]):
        return "#"
    return MAPA[j][i]


def abierta(i, j):
    return celda(i, j) != "#"


def centro(i, j):
    """Centro de la celda en coordenadas del motor (x, z)."""
    return ((i - ORIGEN[0]) * CELDA, (j - ORIGEN[1]) * CELDA)


def celdas(tipo=None):
    return [(i, j) for j in range(len(MAPA)) for i in range(ANCHO)
            if abierta(i, j) and (tipo is None or celda(i, j) == tipo)]


def centro_sala(tipo):
    cs = celdas(tipo)
    xs = [centro(i, j)[0] for i, j in cs]
    zs = [centro(i, j)[1] for i, j in cs]
    return (sum(xs) / len(xs), sum(zs) / len(zs))


def conectado():
    inicio = celdas("A")[0]
    vistas, pila = {inicio}, [inicio]
    while pila:
        i, j = pila.pop()
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (i + di, j + dj)
            if abierta(*n) and n not in vistas:
                vistas.add(n)
                pila.append(n)
    return set(celdas()) - vistas


if __name__ == "__main__":
    for j, fila in enumerate(MAPA):
        print(f"{j:2d} {fila}")
    sueltas = conectado()
    print("celdas abiertas:", len(celdas()), " sin conexion:", sorted(sueltas))
    for t in "ABCDEFGH":
        print(t, centro_sala(t))
