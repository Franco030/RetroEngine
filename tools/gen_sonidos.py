#!/usr/bin/env python3
"""
gen_sonidos: sintetiza los efectos y ambientes del subterraneo (assets/audio/).

  cripta_amb.wav     ambiente de la cripta (bucle): tono de sala, gotas y un viento lejano
  tuneles_amb.wav    ambiente de los tuneles (bucle): gotas con eco, aliento del tunel,
                     piedrecitas que caen y crujidos de madera
  paso_tierra_1..3   pasos sobre tierra
  reja.wav           reja de hierro que se abre (chirrido y golpe)
  derrumbe.wav       rocas que se desprenden a lo lejos
  gemido.wav         un lamento lejano (para asustar una sola vez)
  hojas.wav          pasos entre hojas secas (al meterse en el bosque)
  buho.wav, pajaros.wav  bucles de bosque de noche y de dia (emisores)

Mono, 22050 Hz, 16 bits, como el resto de efectos. Los ambientes son BUCLES PERFECTOS: se
mezclan en un buffer circular y la reverb es circular (las funciones de gen_musica.py).
Deterministas: misma semilla, mismo resultado.

Uso:
  python tools/gen_sonidos.py              # todos
  python tools/gen_sonidos.py reja gemido  # solo esos

Requiere:  pip install numpy
"""
import argparse
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_musica import (SR, convolucion_circular, envolvente,  # noqa: E402
                        respuesta_sala, suavizar)


def escribir_mono(ruta, x, pico=0.9):
    x = x / max(np.abs(x).max(), 1e-9) * pico
    datos = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(ruta, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(datos.tobytes())


def poner(buf, senal, t):
    """Suma 'senal' en el buffer circular a partir de t segundos (lo que sobra da la vuelta)."""
    i = int(t * SR) % len(buf)
    while len(senal):
        k = min(len(senal), len(buf) - i)
        buf[i:i + k] += senal[:k]
        senal = senal[k:]
        i = 0


def reverb(buf, rng, rt60, mezcla):
    ir = respuesta_sala(rng, min(rt60 * 1.3, len(buf) / SR), rt60)
    humedo = convolucion_circular(np.stack([buf, buf]), ir)[0]
    humedo *= np.sqrt((buf ** 2).mean() / max((humedo ** 2).mean(), 1e-12))
    return buf * (1 - mezcla) + humedo * mezcla


def ruido_filtrado(rng, n, ancho):
    return suavizar(rng.standard_normal(n).astype(np.float32), ancho)


# ---------------------------------------------------------------------------
# Piezas sueltas
# ---------------------------------------------------------------------------
def gota(rng, tono=1.0):
    """Gota de agua: un "plink" (seno que sube de tono rapidisimo) con un clic."""
    n = int(0.18 * SR)
    t = np.arange(n) / SR
    f = (700 + 1600 * (1 - np.exp(-t / 0.012))) * tono
    fase = 2 * np.pi * np.cumsum(f) / SR
    s = np.sin(fase) * np.exp(-t / 0.045)
    s[:40] += rng.standard_normal(40) * 0.3 * np.linspace(1, 0, 40)
    return s.astype(np.float32)


def piedrecitas(rng):
    """Un punado de piedrecitas que ruedan: golpecitos agudos cada vez mas espaciados."""
    n = int(1.4 * SR)
    s = np.zeros(n, np.float32)
    t = 0.0
    for k in range(rng.integers(6, 11)):
        dur = int(0.03 * SR)
        golpe = rng.standard_normal(dur).astype(np.float32) * np.exp(-np.arange(dur) / (0.004 * SR))
        golpe = golpe - suavizar(golpe, 6)  # solo lo agudo
        i = int(t * SR)
        if i + dur >= n:
            break
        s[i:i + dur] += golpe * (1 - k * 0.07)
        t += rng.uniform(0.04, 0.16) * (1 + k * 0.15)
    return s


def crujido(rng):
    """Madera que cruje: pulsos rapidos de frecuencia irregular (como una viga cargada)."""
    n = int(0.9 * SR)
    s = np.zeros(n, np.float32)
    t = 0.0
    while t < 0.75:
        i = int(t * SR)
        k = int(0.004 * SR)
        s[i:i + k] += rng.standard_normal(k) * np.exp(-np.arange(k) / 20)
        t += rng.uniform(0.006, 0.03)
    s = suavizar(s, 3) - suavizar(s, 40)  # banda media
    return s * envolvente(n, 0.05, 0.3)


# ---------------------------------------------------------------------------
# Ambientes (bucles)
# ---------------------------------------------------------------------------
def cripta_amb(rng):
    seg = 20.0
    n = int(seg * SR)
    buf = ruido_filtrado(rng, n, 120) * 0.6          # tono de sala, muy grave
    t = np.arange(n) / SR
    viento = ruido_filtrado(rng, n, 25) * (0.5 + 0.5 * np.sin(2 * np.pi * t / seg)) ** 2
    buf += viento * 0.35
    for t0 in (2.3, 9.1, 15.7):
        poner(buf, gota(rng, rng.uniform(0.8, 1.1)) * 0.5, t0)
    poner(buf, crujido(rng) * 0.25, 12.4)
    return reverb(buf, rng, 3.5, 0.55)


def tuneles_amb(rng):
    seg = 24.0
    n = int(seg * SR)
    t = np.arange(n) / SR
    # aliento del tunel: ruido grave que sube y baja dos veces por bucle
    buf = ruido_filtrado(rng, n, 60) * (0.35 + 0.65 * (0.5 + 0.5 * np.sin(4 * np.pi * t / seg)) ** 2)
    buf += ruido_filtrado(rng, n, 160) * 0.8               # retumbar lejano
    for _ in range(9):
        poner(buf, gota(rng, rng.uniform(0.7, 1.3)) * rng.uniform(0.3, 0.7), rng.uniform(0, seg))
    poner(buf, piedrecitas(rng) * 0.5, 7.2)
    poner(buf, piedrecitas(rng) * 0.3, 18.9)
    poner(buf, crujido(rng) * 0.4, 13.0)
    return reverb(buf, rng, 5.0, 0.65)


# ---------------------------------------------------------------------------
# Efectos
# ---------------------------------------------------------------------------
def paso_tierra(rng):
    """Paso amortiguado: golpe grave y arenilla."""
    n = int(0.22 * SR)
    t = np.arange(n) / SR
    golpe = np.sin(2 * np.pi * rng.uniform(70, 95) * t) * np.exp(-t / 0.03)
    arena = ruido_filtrado(rng, n, 4) * np.exp(-t / 0.05) * 0.8
    arena += rng.standard_normal(n).astype(np.float32) * np.exp(-((t - 0.04) / 0.02) ** 2) * 0.15
    return (golpe + arena).astype(np.float32) * envolvente(n, 0.002, 0.05)


def reja(rng):
    """Chirrido de bisagra oxidada (tono que tiembla y sube) y el golpe metalico al final."""
    n = int(1.6 * SR)
    t = np.arange(n) / SR
    temblor = suavizar(rng.standard_normal(n).astype(np.float32), 200) * 30
    f = 420 + 260 * np.clip(t / 1.1, 0, 1) + temblor
    fase = 2 * np.pi * np.cumsum(f) / SR
    chirrido = sum(np.sin(k * fase) / k for k in range(1, 9)).astype(np.float32)
    chirrido *= (0.5 + 0.5 * np.abs(np.sin(2 * np.pi * 7 * t))) * envolvente(n, 0.05, 0.2)
    chirrido[t > 1.15] = 0
    s = chirrido * 0.5
    i = int(1.18 * SR)
    m = n - i
    tt = np.arange(m) / SR
    golpe = sum(np.sin(2 * np.pi * f0 * tt) * np.exp(-tt / d)
                for f0, d in ((180, 0.25), (433, 0.15), (911, 0.08), (1530, 0.05)))
    s[i:] += golpe.astype(np.float32) * 0.8
    s += rng.standard_normal(n).astype(np.float32) * 0.01
    return np.concatenate([s, np.zeros(int(0.5 * SR), np.float32)])


def derrumbe(rng):
    """Rocas que caen lejos: retumbo grave, golpes y piedrecitas al final."""
    n = int(3.5 * SR)
    t = np.arange(n) / SR
    s = ruido_filtrado(rng, n, 90) * 4.0 * envolvente(n, 0.15, 1.8)
    for _ in range(14):
        t0 = rng.uniform(0.1, 1.8)
        k = int(0.12 * SR)
        g = ruido_filtrado(rng, k, 25) * np.exp(-np.arange(k) / (0.02 * SR)) * 3.0
        i = int(t0 * SR)
        s[i:i + k] += g
    poner(s, piedrecitas(rng) * 0.6, 2.0)
    s *= np.exp(-np.maximum(t - 2.5, 0) / 0.6)
    return reverb(s, rng, 2.5, 0.5)


def gemido(rng):
    """Lamento lejano: una voz (formantes de "o" a "u") que baja de tono, con mucho eco."""
    n = int(3.2 * SR)
    t = np.arange(n) / SR
    f0 = 210 * np.exp(-t / 3.5) * (1 + 0.012 * np.sin(2 * np.pi * 4.5 * t))
    fase = 2 * np.pi * np.cumsum(f0) / SR
    s = np.zeros(n, np.float32)
    for k in range(1, 18):
        fk = f0 * k
        a = np.exp(-((fk - 420) / 180) ** 2) + 0.5 * np.exp(-((fk - 750) / 160) ** 2) + 0.03 / k
        s += (a * np.sin(k * fase)).astype(np.float32)
    s *= envolvente(n, 0.6, 1.4)
    s += ruido_filtrado(rng, n, 10) * 0.15 * envolvente(n, 0.6, 1.4)
    out = np.concatenate([s, np.zeros(int(3 * SR), np.float32)])
    return reverb(out, rng, 4.0, 0.7)


def hojas(rng):
    """Pasos entre maleza y hojas secas: crujidos cortos de ruido agudo, tres o cuatro pasos."""
    n = int(1.5 * SR)
    s = np.zeros(n, np.float32)
    for k in range(4):
        t0 = 0.05 + k * 0.33 + rng.uniform(-0.03, 0.03)
        m = int(0.22 * SR)
        r = rng.standard_normal(m).astype(np.float32)
        r = r - suavizar(r, 4)                                  # solo lo agudo: hojarasca
        crujidos = (rng.random(m) < 0.06).astype(np.float32)
        r *= np.exp(-np.arange(m) / (0.06 * SR)) * (0.4 + suavizar(crujidos, 30) * 20)
        i = int(t0 * SR)
        s[i:i + m] += r[:n - i] * (1 - 0.15 * k)
    return reverb(s, rng, 0.8, 0.15)


def buho(rng):
    """Bucle de bosque de noche: un buho a lo lejos (dos ululatos) y mucho silencio."""
    seg = 14.0
    buf = np.zeros(int(seg * SR), np.float32)
    def ulular(f0, dur):
        n = int(dur * SR)
        t = np.arange(n) / SR
        f = f0 * (1 + 0.04 * np.sin(np.pi * t / dur))            # sube y baja un poco
        fase = 2 * np.pi * np.cumsum(f) / SR
        voz = np.sin(fase) + 0.2 * np.sin(2 * fase)
        return (voz * np.sin(np.pi * t / dur) ** 1.5).astype(np.float32)
    for t0, f0, dur in ((2.0, 390, 0.35), (2.6, 360, 0.7), (8.4, 385, 0.35), (8.9, 355, 0.25),
                        (9.3, 350, 0.75)):
        poner(buf, ulular(f0, dur) * 0.6, t0)
    buf += ruido_filtrado(rng, len(buf), 90) * 0.5            # aire entre los arboles
    return reverb(buf, rng, 3.0, 0.5)


def pajaros(rng):
    """Bucle de bosque de dia: trinos cortos de dos o tres pajaros y brisa en las hojas."""
    seg = 12.0
    buf = ruido_filtrado(rng, int(seg * SR), 12) * 0.25          # brisa
    for _ in range(9):
        t0 = rng.uniform(0, seg)
        f0 = rng.uniform(2200, 3600)
        for k in range(int(rng.integers(2, 6))):                  # frase de varios trinos
            n = int(rng.uniform(0.05, 0.12) * SR)
            t = np.arange(n) / SR
            f = f0 * (1 + rng.uniform(-0.25, 0.25) * t / t[-1])
            tr = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.sin(np.pi * t / t[-1])
            poner(buf, tr.astype(np.float32) * 0.35, t0 + k * rng.uniform(0.09, 0.16))
    return reverb(buf, rng, 1.5, 0.3)


SONIDOS = {
    "cripta_amb": cripta_amb,
    "tuneles_amb": tuneles_amb,
    "paso_tierra_1": paso_tierra,
    "paso_tierra_2": paso_tierra,
    "paso_tierra_3": paso_tierra,
    "reja": reja,
    "derrumbe": derrumbe,
    "gemido": gemido,
    "hojas": hojas,
    "buho": buho,
    "pajaros": pajaros,
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sonidos", nargs="*", help="cuales generar (por defecto, todos)")
    ap.add_argument("--out", default=os.path.join(here, "..", "assets", "audio"))
    ap.add_argument("--seed", type=int, default=9)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for nombre in args.sonidos or SONIDOS:
        rng = np.random.default_rng(args.seed * 100 + list(SONIDOS).index(nombre))
        x = SONIDOS[nombre](rng)
        ruta = os.path.join(args.out, nombre + ".wav")
        escribir_mono(ruta, x, 0.8 if nombre.endswith("_amb") or nombre in ("buho", "pajaros") else 0.9)
        print(f"{ruta}  {len(x) / SR:.2f} s")


if __name__ == "__main__":
    main()
