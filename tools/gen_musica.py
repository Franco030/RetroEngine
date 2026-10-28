#!/usr/bin/env python3
"""
gen_musica: compone y sintetiza la musica de las escenas (assets/audio/musica_*_v2.wav).

Piezas originales, lentas y melancolicas, con sonido de sampler antiguo: pads calidos, coro,
caja de musica, flauta, piano y campanas, con una reverb larga. Todo es sintesis aditiva con
numpy (sin samples ni otras dependencias) y determinista.

Cada pieza es un BUCLE PERFECTO: las notas que pasan del final, la cola de la reverb y los ecos
se pliegan sobre el principio (todo se mezcla en un buffer circular y la reverb es una
convolucion circular por FFT), asi que al repetirse no hay corte ni salto.

  22050 Hz estereo 16 bits (el "techo" de 11 kHz suena a consola antigua).

Uso:
  python tools/gen_musica.py                 # todas
  python tools/gen_musica.py noche aldea     # solo esas
  python tools/gen_musica.py --out carpeta

Requiere:  pip install numpy
"""
import argparse
import os
import wave

import numpy as np

SR = 22050


def hz(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def envolvente(n, ataque, caida, sr=SR):
    """Ataque lineal y caida final con forma de coseno (sin clics)."""
    e = np.ones(n, np.float32)
    a = min(int(ataque * sr), n)
    r = min(int(caida * sr), n - a)
    if a > 0:
        e[:a] = np.linspace(0, 1, a, dtype=np.float32)
    if r > 0:
        e[n - r:] *= (0.5 + 0.5 * np.cos(np.linspace(0, np.pi, r, dtype=np.float32)))
    return e


def suavizar(x, ancho):
    """Paso bajo sencillo: media movil (para el soplo de la flauta)."""
    k = np.ones(ancho, np.float32) / ancho
    return np.convolve(x, k, mode="same")


# ---------------------------------------------------------------------------
# Instrumentos: devuelven una senal mono (float32)
# ---------------------------------------------------------------------------
class Instrumentos:
    def __init__(self, rng):
        self.rng = rng

    def _aditivo(self, f, n, amplitudes, decaimientos=None, desafine=0.0, vibrato=None):
        t = np.arange(n, dtype=np.float32) / SR
        # "reloj" de la nota: con vibrato avanza un poco mas rapido o mas lento. Se integra
        # (cumsum) para que la desviacion de tono no crezca con la duracion de la nota.
        reloj = t
        if vibrato is not None:
            vel, prof, retraso = vibrato
            rampa = np.clip((t - retraso) / 0.6, 0, 1)
            frec_rel = 1 + prof * rampa * np.sin(2 * np.pi * vel * t)
            reloj = (np.cumsum(frec_rel) / SR).astype(np.float32)
        out = np.zeros(n, np.float32)
        for k, a in enumerate(amplitudes, 1):
            fk = f * k * (1 + desafine * k * k)
            if fk >= SR / 2 - 200 or a < 0.005:
                continue
            fase = 2 * np.pi * fk * reloj + self.rng.uniform(0, 2 * np.pi)
            s = a * np.sin(fase)
            if decaimientos is not None:
                s *= np.exp(-t / decaimientos[k - 1])
            out += s.astype(np.float32)
        return out

    def pad(self, midi, dur, brillo=6.0, ataque=1.4, caida=2.6):
        """Cuerdas sinteticas calidas: dos sierras desafinadas, filtradas (pocos armonicos)."""
        n = int((dur + caida) * SR)
        f = hz(midi)
        amps = [np.exp(-k / brillo) / k for k in range(1, 16)]
        s = sum(self._aditivo(f * 2 ** (c / 1200), n, amps) for c in (-7, 6))
        t = np.arange(n) / SR
        s *= (0.85 + 0.15 * np.sin(2 * np.pi * 0.21 * t + self.rng.uniform(0, 6)))
        return s * envolvente(n, ataque, caida) * 0.5

    def coro(self, midi, dur, vocal="a", ataque=0.9, caida=2.0):
        """Coro sintetico: armonicos con picos de formante (vocal "a" u "o") y tres voces."""
        formantes = {"a": [(700, 1.0), (1220, 0.5), (2600, 0.25)],
                     "o": [(450, 1.0), (800, 0.45), (2830, 0.1)]}[vocal]
        f = hz(midi)
        amps = []
        for k in range(1, 30):
            fk = f * k
            a = sum(g * np.exp(-((fk - fc) / 160.0) ** 2) for fc, g in formantes) + 0.04 / k
            amps.append(a)
        n = int((dur + caida) * SR)
        s = sum(self._aditivo(f * 2 ** (c / 1200), n, amps,
                              vibrato=(5.1 + 0.3 * i, 0.0028, 0.4))
                for i, c in enumerate((-9, 0, 8)))
        return s * envolvente(n, ataque, caida) * 0.35

    def caja_musica(self, midi, dur=None):
        """Caja de musica / celesta: ataque brillante y caida rapida."""
        n = int(2.2 * SR)
        f = hz(midi)
        s = self._aditivo(f, n, [1.0, 0.42, 0.18, 0.09], [1.3, 0.7, 0.35, 0.2], desafine=0.0008)
        s += self._aditivo(f * 4.07, n, [0.12], [0.08])  # el "ting" metalico del ataque
        return s * envolvente(n, 0.002, 0.3) * 0.6

    def campana(self, midi, dur=None):
        """Campana grave de iglesia, a lo lejos: parciales inarmonicos y cola larga."""
        n = int(7.0 * SR)
        f = hz(midi)
        parciales = [(0.5, 0.6, 4.5), (1.0, 1.0, 3.5), (1.19, 0.5, 2.6), (1.5, 0.35, 2.2),
                     (2.0, 0.4, 1.8), (2.51, 0.25, 1.2), (2.66, 0.2, 1.0), (3.01, 0.15, 0.8)]
        t = np.arange(n, dtype=np.float32) / SR
        s = np.zeros(n, np.float32)
        for r, a, d in parciales:
            s += (a * np.sin(2 * np.pi * f * r * t + self.rng.uniform(0, 6)) * np.exp(-t / d)).astype(np.float32)
        return s * envolvente(n, 0.004, 1.0) * 0.45

    def flauta(self, midi, dur, ataque=0.09, caida=0.35):
        n = int((dur + caida) * SR)
        f = hz(midi)
        s = self._aditivo(f, n, [1.0, 0.16, 0.06, 0.02], vibrato=(5.3, 0.004, 0.35))
        soplo = suavizar(self.rng.standard_normal(n).astype(np.float32), 6) * 0.06
        return (s + soplo) * envolvente(n, ataque, caida) * 0.5

    def piano(self, midi, dur, fuerza=1.0):
        n = int((dur + 1.4) * SR)
        f = hz(midi)
        amps = [1.0, 0.5, 0.3, 0.18, 0.1, 0.06, 0.04]
        dec = [2.6 / (1 + 0.55 * k) for k in range(len(amps))]
        s = self._aditivo(f, n, amps, dec, desafine=0.0004)
        e = envolvente(n, 0.004, 0.9)
        corte = int((dur + 0.15) * SR)
        if corte < n:  # al soltar la tecla, la nota se apaga antes
            e[corte:] *= np.exp(-np.arange(n - corte) / (0.25 * SR))
        return s * e * 0.45 * fuerza

    def bajo(self, midi, dur):
        n = int((dur + 0.6) * SR)
        s = self._aditivo(hz(midi), n, [1.0, 0.22, 0.05])
        return s * envolvente(n, 0.06, 0.6) * 0.55

    def susurro(self, dur, oscuridad=40, ataque=2.5, caida=3.0):
        """Soplo de aire grave (ruido filtrado) que sube y baja: el aliento de los tuneles."""
        n = int((dur + caida) * SR)
        ruido = self.rng.standard_normal(n).astype(np.float32)
        s = suavizar(suavizar(ruido, oscuridad), oscuridad // 2) * 3.0
        t = np.arange(n) / SR
        s *= 0.6 + 0.4 * np.sin(2 * np.pi * 0.13 * t + self.rng.uniform(0, 6))
        return s * envolvente(n, ataque, caida) * 0.5

    def organo(self, midi, dur, ataque=0.12, caida=0.7, brillo=1.0):
        """Organo de tubos: principal con su octava y su quinta (armonicos fijos, sin caida),
        tremolo lento y un poco de soplo. 'brillo' < 1 lo deja mas aflautado."""
        n = int((dur + caida) * SR)
        f = hz(midi)
        amps = [1.0, 0.55 * brillo, 0.3 * brillo, 0.32 * brillo, 0.12 * brillo, 0.15 * brillo,
                0.06 * brillo, 0.1 * brillo]
        s = self._aditivo(f, n, amps) + 0.35 * self._aditivo(f * 2, n, [1.0, 0.3 * brillo])
        t = np.arange(n) / SR
        s *= 1 + 0.06 * np.sin(2 * np.pi * 5.4 * t)
        s += suavizar(self.rng.standard_normal(n).astype(np.float32), 3) * 0.015
        return (s * envolvente(n, ataque, caida) * 0.22).astype(np.float32)

    def dron(self, midi, dur):
        """Nota grave sostenida con un batido lento (dos senos casi iguales)."""
        n = int((dur + 3.0) * SR)
        f = hz(midi)
        s = self._aditivo(f, n, [1.0, 0.3, 0.1]) + self._aditivo(f * 1.003, n, [0.7, 0.2])
        return s * envolvente(n, 3.0, 3.0) * 0.3


# ---------------------------------------------------------------------------
# Mezcla circular, reverb y master
# ---------------------------------------------------------------------------
class Pista:
    """Buffer estereo circular: lo que pasa del final vuelve al principio (bucle perfecto)."""

    def __init__(self, segundos):
        self.n = int(segundos * SR)
        self.buf = np.zeros((2, self.n), np.float32)

    def poner(self, senal, en_segundos, pan=0.0, ganancia=1.0):
        i0 = int(en_segundos * SR) % self.n
        izq = np.cos((pan + 1) * np.pi / 4) * ganancia
        der = np.sin((pan + 1) * np.pi / 4) * ganancia
        resto = senal
        pos = i0
        while len(resto):
            k = min(len(resto), self.n - pos)
            self.buf[0, pos:pos + k] += resto[:k] * izq
            self.buf[1, pos:pos + k] += resto[:k] * der
            resto = resto[k:]
            pos = 0


def respuesta_sala(rng, segundos, rt60, brillo=4500.0):
    """Respuesta al impulso de una sala grande: ruido con caida exponencial, cada vez mas
    oscuro, y unas primeras reflexiones. Distinta en cada canal (amplitud estereo)."""
    n = int(segundos * SR)
    t = np.arange(n) / SR
    ir = np.zeros((2, n), np.float32)
    for c in range(2):
        ruido = rng.standard_normal(n).astype(np.float32)
        oscuro = suavizar(ruido, 5)
        mezcla = np.exp(-t / (rt60 * 0.25))  # al principio brillante, luego oscuro
        cola = (ruido * mezcla + oscuro * (1 - mezcla)) * np.exp(-6.9 * t / rt60)
        espectro = np.fft.rfft(cola)
        freqs = np.fft.rfftfreq(n, 1 / SR)
        espectro *= 1 / (1 + (freqs / brillo) ** 2)
        ir[c] = np.fft.irfft(espectro, n)
        for _ in range(8):
            ir[c, int(rng.uniform(0.012, 0.09) * SR)] += rng.uniform(0.2, 0.5)
    return ir / np.abs(ir).sum(axis=1, keepdims=True) * 18.0


def convolucion_circular(x, ir):
    n = x.shape[1]
    out = np.zeros_like(x)
    for c in range(2):
        h = np.zeros(n, np.float32)
        m = min(n, ir.shape[1])
        h[:m] = ir[c, :m]
        out[c] = np.fft.irfft(np.fft.rfft(x[c]) * np.fft.rfft(h), n)
    return out


def eco(x, segundos, realim=0.35, mezcla=0.25):
    """Eco de ida y vuelta entre canales, circular (encaja en el bucle)."""
    d = int(segundos * SR)
    out = x.copy()
    golpe = x.copy()
    for i in range(1, 6):
        golpe = np.roll(golpe[::-1], d, axis=1) * realim  # cruza de canal en cada rebote
        out += golpe * mezcla
    return out


def master(seco, humedo, rng, mezcla_reverb, corte=7500.0, soplido=0.0015):
    x = seco * (1 - mezcla_reverb) + humedo * mezcla_reverb
    for c in range(2):  # calidez: recorte suave de agudos
        e = np.fft.rfft(x[c])
        f = np.fft.rfftfreq(x.shape[1], 1 / SR)
        e *= 1 / np.sqrt(1 + (f / corte) ** 4)
        x[c] = np.fft.irfft(e, x.shape[1])
    x = np.tanh(x / np.abs(x).max() * 1.2) / np.tanh(1.2)
    x += rng.standard_normal(x.shape).astype(np.float32) * soplido  # "cinta"
    return x / np.abs(x).max() * 0.89


def escribir(ruta, x):
    datos = (np.clip(x.T, -1, 1) * 32767).astype("<i2")
    with wave.open(ruta, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(datos.tobytes())


# ---------------------------------------------------------------------------
# Composicion
# ---------------------------------------------------------------------------
class Pieza:
    """Ayuda para escribir por compases: tiempos en negras, compas de 4."""

    def __init__(self, bpm, compases, rng):
        self.negra = 60.0 / bpm
        self.compas = 4 * self.negra
        self.pista = Pista(compases * self.compas)
        self.i = Instrumentos(rng)
        self.rng = rng

    def t(self, compas, tiempo=0.0):
        return compas * self.compas + tiempo * self.negra

    def acordes(self, progresion, inst="pad", ganancia=0.16, desde=0, **kw):
        for k, notas in enumerate(progresion):
            for j, m in enumerate(notas):
                s = getattr(self.i, inst)(m, self.compas * 1.02, **kw)
                self.pista.poner(s, self.t(desde + k), pan=(j / max(len(notas) - 1, 1) - 0.5) * 0.7,
                                 ganancia=ganancia)

    def bajos(self, progresion, desde=0, ganancia=0.5, octava=-12, quinta=True):
        for k, notas in enumerate(progresion):
            r = notas[0] + octava
            self.pista.poner(self.i.bajo(r, self.compas * (0.5 if quinta else 0.95)),
                             self.t(desde + k), ganancia=ganancia)
            if quinta:
                self.pista.poner(self.i.bajo(r + 7, self.compas * 0.45), self.t(desde + k, 2),
                                 ganancia=ganancia * 0.7)

    def melodia(self, notas, inst, desde=0, ganancia=0.5, pan=0.0, transp=0, **kw):
        """notas: [(compas, tiempo, duracion_en_negras, midi), ...]"""
        for c, tiempo, dur, m in notas:
            s = getattr(self.i, inst)(m + transp, dur * self.negra, **kw)
            self.pista.poner(s, self.t(desde + c, tiempo), pan=pan, ganancia=ganancia)

    def arpegio(self, progresion, patron, inst="caja_musica", desde=0, ganancia=0.22,
                subdivision=0.5, octava=12, pan_vaiven=0.4):
        pasos = int(4 / subdivision)
        for k, notas in enumerate(progresion):
            altas = sorted(notas)[1:]
            for p in range(pasos):
                m = altas[patron[p % len(patron)] % len(altas)] + octava
                pan = pan_vaiven * (1 if p % 2 else -1)
                self.pista.poner(getattr(self.i, inst)(m, subdivision * self.negra),
                                 self.t(desde + k, p * subdivision), pan=pan,
                                 ganancia=ganancia * (1.0 if p % 4 == 0 else 0.8))

    def final(self, rt60=3.8, reverb=0.42, ecos=None, corte=7500.0):
        seco = self.pista.buf
        if ecos:
            seco = eco(seco, ecos * self.negra)
        ir = respuesta_sala(self.rng, min(rt60 * 1.3, seco.shape[1] / SR), rt60)
        humedo = convolucion_circular(seco, ir)
        # misma energia que la senal seca: asi "reverb" es de verdad la proporcion de la mezcla
        humedo *= np.sqrt((seco ** 2).mean() / max((humedo ** 2).mean(), 1e-12))
        return master(seco, humedo, self.rng, reverb, corte)


def aldea(rng):
    """De dia, nostalgica y luminosa: re mayor con septimas y novenas, flauta y caja de musica."""
    p = Pieza(70, 24, rng)
    Dmaj9 = [50, 57, 61, 64, 66]
    Bm11 = [47, 54, 57, 62, 64]
    Gmaj7 = [43, 50, 54, 59, 61]
    Asus = [45, 52, 59, 62, 64]
    Em9 = [52, 55, 59, 62, 66]
    Fsm7 = [42, 49, 52, 57, 61]
    DA = [45, 50, 57, 61, 66]
    A1 = [Dmaj9, Bm11, Gmaj7, Asus, Dmaj9, Fsm7, Gmaj7, Asus]
    B = [Em9, Fsm7, Gmaj7, DA, Em9, Fsm7, Gmaj7, Asus]
    prog = A1 + B + A1
    p.acordes(prog, "pad", 0.13)
    p.bajos(prog, ganancia=0.42)
    p.arpegio(prog, [0, 2, 1, 3, 2, 4, 3, 1], ganancia=0.17)
    tema = [(0, 0, 1.5, 78), (0, 1.5, 0.5, 76), (0, 2, 2, 74),
            (1, 0, 1, 74), (1, 1, 1, 76), (1, 2, 1.5, 78), (1, 3.5, 0.5, 81),
            (2, 0, 2, 83), (2, 2, 0.5, 81), (2, 2.5, 1.5, 78),
            (3, 0, 3, 76),
            (4, 0, 1, 78), (4, 1, 1, 81), (4, 2, 1, 83), (4, 3, 1, 81),
            (5, 0, 2, 85), (5, 2, 2, 81),
            (6, 0, 1.5, 83), (6, 1.5, 0.5, 81), (6, 2, 1, 78), (6, 3, 1, 76),
            (7, 0, 4, 74)]
    p.melodia(tema, "flauta", desde=0, ganancia=0.42, pan=0.1)
    p.melodia(tema, "piano", desde=16, ganancia=0.5, pan=-0.15, transp=-12)
    p.melodia(tema[:10], "flauta", desde=16, ganancia=0.25, pan=0.25)
    # parte B: coro en notas largas y respuestas de piano
    coro = [(0, 0, 4, 66), (1, 0, 4, 66), (2, 0, 4, 62), (3, 0, 4, 64),
            (4, 0, 4, 67), (5, 0, 4, 69), (6, 0, 2, 71), (6, 2, 2, 69), (7, 0, 4, 66)]
    p.melodia(coro, "coro", desde=8, ganancia=0.3, vocal="a")
    resp = [(0, 2, 1, 83), (0, 3, 1, 78), (2, 2, 1, 81), (2, 3, 1, 78), (4, 2, 1, 86),
            (4, 3, 1, 83), (6, 2, 2, 85)]
    p.melodia(resp, "piano", desde=8, ganancia=0.32, pan=0.3)
    return p.final(rt60=3.4, reverb=0.38, ecos=0.75)


def noche(rng):
    """Plaza de noche: la menor, piano quebrado, campanitas y coro en la segunda parte."""
    p = Pieza(62, 16, rng)
    Am9 = [45, 52, 55, 59, 60]
    Fmaj7 = [41, 48, 52, 57, 60]
    CG = [43, 48, 52, 55, 59]
    Em7 = [40, 47, 50, 55, 59]
    Dm9 = [50, 53, 57, 60, 64]
    E7s = [40, 47, 50, 57, 59]
    A = [Am9, Fmaj7, CG, Em7, Am9, Fmaj7, Dm9, E7s]
    B = [Fmaj7, CG, Dm9, Am9, Fmaj7, Em7, Dm9, E7s]
    prog = A + B
    p.acordes(prog, "pad", 0.12, brillo=4.0)
    p.bajos(prog, ganancia=0.45, quinta=False)
    for k, notas in enumerate(prog):  # piano en arpegio lento, de abajo arriba
        for j, m in enumerate(sorted(notas)):
            p.pista.poner(p.i.piano(m + 12, p.negra * 1.6, fuerza=0.75), p.t(k, j * 0.75),
                          pan=-0.3 + 0.15 * j, ganancia=0.3)
    campanitas = [(0, 0, 2, 76), (0, 2, 2, 72), (1, 0, 3, 69), (2, 0, 1, 67), (2, 1, 1, 71),
                  (2, 2, 2, 72), (3, 0, 4, 71), (4, 0, 1, 76), (4, 1, 1, 79), (4, 2, 2, 81),
                  (5, 0, 1.5, 79), (5, 1.5, 2.5, 76), (6, 0, 2, 77), (6, 2, 1, 76), (6, 3, 1, 74),
                  (7, 0, 4, 76)]
    p.melodia(campanitas, "caja_musica", ganancia=0.38, pan=0.2)
    coro = [(0, 0, 4, 72), (1, 0, 4, 71), (2, 0, 2, 69), (2, 2, 2, 72), (3, 0, 4, 69),
            (4, 0, 4, 69), (5, 0, 4, 67), (6, 0, 2, 65), (6, 2, 2, 69), (7, 0, 4, 68)]
    p.melodia(coro, "coro", desde=8, ganancia=0.33, vocal="o")
    p.melodia(campanitas[8:], "caja_musica", desde=8, ganancia=0.22, pan=-0.3, transp=12)
    return p.final(rt60=4.2, reverb=0.48, ecos=1.5)


def cementerio(rng):
    """Cementerio: re menor oscuro, dron, coro en "o", campana lejana y caja de musica
    ligeramente desafinada."""
    p = Pieza(54, 16, rng)
    Dm9 = [50, 57, 64, 65]
    Bb = [46, 53, 57, 62, 64]
    Gm6 = [43, 50, 58, 64]
    A7 = [45, 52, 55, 58, 62]
    Eb = [51, 58, 62, 67, 69]
    GmBb = [46, 50, 55, 58]
    Asus = [45, 52, 57, 62]
    prog = [Dm9, Bb, Gm6, A7, Dm9, Eb, GmBb, Asus] * 2
    p.acordes(prog, "pad", 0.09, brillo=3.0, ataque=2.5, caida=3.5)
    p.acordes(prog, "coro", 0.06, vocal="o", ataque=2.0, caida=3.0)
    p.pista.poner(p.i.dron(38, p.pista.n / SR - 3), 0, ganancia=0.5)
    p.pista.poner(p.i.dron(45, p.pista.n / SR - 3), 0.5, ganancia=0.25)
    for k in range(0, 16, 2):  # la campana, cada dos compases
        p.pista.poner(p.i.campana(50), p.t(k), pan=-0.5, ganancia=0.5)
    tema = [(0, 0, 1, 81), (0, 1, 1, 77), (0, 2, 2, 76), (1, 0, 4, 74),
            (2, 0, 1, 76), (2, 1, 1, 82), (2, 2, 2, 81), (3, 0, 1, 79), (3, 1, 1, 77),
            (3, 2, 2, 76), (4, 0, 2, 81), (4, 2, 2, 86), (5, 0, 1, 86), (5, 1, 1, 82),
            (5, 2, 2, 79), (6, 0, 2, 77), (6, 2, 2, 76), (7, 0, 4, 74)]
    # la caja de musica suena un pelin desafinada (inquietante)
    viejo = p.i.caja_musica

    def caja_vieja(m, dur=None):
        return viejo(m + p.rng.uniform(-0.12, 0.12), dur)

    p.i.caja_musica = caja_vieja
    p.melodia(tema, "caja_musica", ganancia=0.34, pan=0.35)
    p.i.caja_musica = viejo
    p.melodia(tema, "coro", desde=8, ganancia=0.22, vocal="o", transp=-12)
    p.melodia(tema[::3], "caja_musica", desde=8, ganancia=0.2, pan=-0.4)
    return p.final(rt60=5.5, reverb=0.55, ecos=2.0, corte=6000.0)


def demo(rng):
    """Escena de pruebas: do lidio, ligera y curiosa."""
    p = Pieza(76, 16, rng)
    Cmaj7 = [48, 55, 59, 62, 64]
    DC = [48, 54, 57, 62, 66]
    Em7 = [52, 55, 59, 62, 67]
    Am9 = [45, 52, 55, 59, 60]
    Fmaj7 = [41, 48, 52, 57, 64]
    G6 = [43, 50, 52, 59, 62]
    prog = [Cmaj7, DC, Em7, Am9, Fmaj7, G6, Cmaj7, DC] * 2
    p.acordes(prog, "pad", 0.12)
    p.bajos(prog, ganancia=0.4)
    p.arpegio(prog, [0, 1, 2, 3, 2, 1, 3, 4], inst="caja_musica", ganancia=0.18)
    tema = [(0, 0, 2, 76), (0, 2, 2, 79), (1, 0, 3, 78), (2, 0, 1, 79), (2, 1, 1, 83),
            (2, 2, 2, 81), (3, 0, 4, 76), (4, 0, 2, 77), (4, 2, 2, 81), (5, 0, 3, 79),
            (6, 0, 2, 76), (6, 2, 2, 74), (7, 0, 4, 78)]
    p.melodia(tema, "flauta", ganancia=0.38)
    p.melodia(tema, "piano", desde=8, ganancia=0.42, transp=-12, pan=-0.2)
    return p.final(rt60=3.0, reverb=0.35, ecos=0.75)


def cripta(rng):
    """Dentro de la cripta: suspenso sencillo. Pedal de mi grave, un latido, la caja de musica
    desafinada con un motivo de cuatro notas y el coro lejano en la segunda mitad."""
    p = Pieza(48, 12, rng)
    Em = [40, 47, 55, 59, 66]
    FE = [40, 48, 53, 57, 64]     # fa mayor sobre mi: el semitono frigio
    CE = [40, 48, 55, 59, 64]
    AmE = [40, 48, 57, 60, 64]
    Bsus = [35, 47, 52, 57, 59]
    prog = [Em, Em, FE, Em, CE, AmE, FE, Bsus, Em, FE, Em, Bsus]
    p.acordes(prog, "pad", 0.08, brillo=2.5, ataque=3.0, caida=4.0)
    p.pista.poner(p.i.dron(40, p.pista.n / SR - 3), 0, ganancia=0.3)
    for k in range(len(prog)):  # latido: dos golpes graves, el segundo mas flojo
        p.pista.poner(p.i.bajo(40, 0.25), p.t(k, 0), ganancia=0.3)
        p.pista.poner(p.i.bajo(40, 0.2), p.t(k, 0.45), ganancia=0.18)
    motivo = [(1, 0, 1, 76), (1, 1, 1, 79), (1, 2, 2, 78),
              (3, 0, 1, 76), (3, 1, 1, 79), (3, 2, 1, 78), (3, 3, 1, 75),
              (5, 0, 1, 76), (5, 1, 1, 77), (5, 2, 2, 76),
              (7, 0, 2, 75), (7, 2, 2, 71),
              (9, 0, 1, 76), (9, 1, 1, 77), (9, 2, 2, 76), (11, 0, 4, 71)]
    viejo = p.i.caja_musica

    def caja_vieja(m, dur=None):
        return viejo(m + p.rng.uniform(-0.18, 0.18), dur)

    p.i.caja_musica = caja_vieja
    p.melodia(motivo, "caja_musica", ganancia=0.32, pan=0.3)
    p.i.caja_musica = viejo
    coro = [(0, 0, 4, 64), (1, 0, 4, 65), (2, 0, 4, 64), (3, 0, 4, 63)]
    p.melodia(coro, "coro", desde=6, ganancia=0.2, vocal="o")
    p.pista.poner(p.i.campana(40), p.t(0), pan=-0.6, ganancia=0.35)
    p.pista.poner(p.i.campana(41), p.t(6), pan=0.6, ganancia=0.3)
    return p.final(rt60=6.0, reverb=0.58, ecos=1.5, corte=5000.0)


def tuneles(rng):
    """Los tuneles: miedo. Do menor con acordes a un triton de distancia, dron grave que bate,
    el aliento del tunel, golpes graves de piano y una caja de musica en escala de tonos
    enteros que nunca se resuelve."""
    p = Pieza(50, 16, rng)
    Cm = [36, 43, 51, 55, 62]
    Db = [37, 44, 53, 56, 60]
    Gb = [42, 49, 54, 58, 61]     # fa sostenido mayor: a un triton de do
    G7b9 = [43, 50, 53, 56, 59]
    Ab = [44, 51, 55, 60, 63]
    Fm = [41, 48, 53, 56, 60]
    A = [Cm, Db, Cm, Gb, Ab, Fm, Db, G7b9]
    B = [Cm, Cm, Gb, Gb, Fm, Db, G7b9, G7b9]
    prog = A + B
    p.acordes(prog, "pad", 0.075, brillo=2.2, ataque=3.5, caida=4.5)
    p.acordes(B, "coro", 0.035, desde=8, vocal="o", ataque=3.0, caida=4.0)
    dur = p.pista.n / SR - 3
    p.pista.poner(p.i.dron(36, dur), 0, ganancia=0.32)
    p.pista.poner(p.i.dron(42, dur), 1.0, ganancia=0.14)      # el triton, muy bajo
    for k in range(0, 16, 4):
        p.pista.poner(p.i.susurro(p.compas * 2.5), p.t(k, 1), pan=p.rng.uniform(-0.7, 0.7),
                      ganancia=0.5)
    for c, tiempo in ((2, 3.0), (5, 1.5), (9, 2.5), (12, 0.0), (14, 3.5)):
        for m in (37, 38, 43):   # racimo grave, como una tecla pisada en la oscuridad
            p.pista.poner(p.i.piano(m, p.negra * 3, fuerza=0.55), p.t(c, tiempo), pan=-0.2,
                          ganancia=0.38)
    tonos = [72, 74, 76, 78, 80, 82]
    k, notas = 2, []
    for c in range(16):
        if c % 2 == 1 and c % 4 != 3:
            continue  # silencios largos
        for tiempo in (0.0, 1.5):
            k = max(0, min(len(tonos) - 1, k + int(p.rng.choice([-1, -1, 1, 2]))))
            notas.append((c, tiempo, 1.5, tonos[k]))
    viejo = p.i.caja_musica

    def caja_vieja(m, dur=None):
        return viejo(m + p.rng.uniform(-0.25, 0.25), dur)

    p.i.caja_musica = caja_vieja
    p.melodia(notas, "caja_musica", ganancia=0.24, pan=0.45)
    p.i.caja_musica = viejo
    for c in (3, 11):
        p.pista.poner(p.i.campana(62), p.t(c, 2), pan=0.7, ganancia=0.18)
    return p.final(rt60=7.0, reverb=0.62, ecos=2.5, corte=4500.0)


def iglesia(rng):
    """Dentro de la capilla: bonita pero inquietante. Coral de organo en re menor que se tuerce
    hacia acordes prestados (mi bemol mayor, si bemol menor), melodia de flauta de organo,
    coro en la segunda mitad y una campana a lo lejos. Reverb de catedral."""
    p = Pieza(50, 16, rng)
    Dm = [38, 50, 57, 62, 65]
    GmD = [38, 50, 55, 58, 62]
    Bbmaj7 = [34, 46, 53, 57, 62]
    A7b9 = [33, 45, 52, 55, 61]
    FC = [36, 48, 53, 57, 60]
    Ebmaj7 = [39, 51, 55, 58, 62]
    Asus = [33, 45, 52, 57, 62]
    Gm9 = [43, 50, 55, 58, 62]
    DmF = [41, 50, 57, 62, 65]
    Bbm6 = [34, 46, 53, 56, 61]
    DmA = [33, 50, 57, 62, 65]
    A = [Dm, GmD, Bbmaj7, A7b9, Dm, FC, Ebmaj7, Asus]
    B = [Gm9, DmF, Ebmaj7, Bbm6, DmA, A7b9, Dm, Asus]
    prog = A + B
    for k, notas in enumerate(prog):        # el coral: acordes enteros, el pedal una octava abajo
        for j, m in enumerate(notas[1:]):
            p.pista.poner(p.i.organo(m, p.compas * 1.02, ataque=0.3, caida=1.2, brillo=0.7),
                          p.t(k), pan=(j / 3 - 0.5) * 0.6, ganancia=0.15)
        p.pista.poner(p.i.organo(notas[0] - 12, p.compas * 1.02, ataque=0.4, caida=1.5, brillo=0.4),
                      p.t(k), ganancia=0.3)
    tema = [(0, 0, 2, 69), (0, 2, 2, 74), (1, 0, 1, 72), (1, 1, 1, 70), (1, 2, 1, 69), (1, 3, 1, 67),
            (2, 0, 3, 65), (2, 3, 1, 64), (3, 0, 2, 64), (3, 2, 2, 73),
            (4, 0, 1.5, 74), (4, 1.5, 0.5, 76), (4, 2, 2, 77), (5, 0, 2, 76), (5, 2, 2, 72),
            (6, 0, 2, 75), (6, 2, 1, 74), (6, 3, 1, 72), (7, 0, 4, 73)]
    p.melodia(tema, "organo", ganancia=0.42, pan=0.2, brillo=0.25, ataque=0.08, caida=0.5)
    segunda = [(0, 0, 2, 79), (0, 2, 2, 77), (1, 0, 4, 81), (2, 0, 1.5, 79), (2, 1.5, 0.5, 77),
               (2, 2, 2, 75), (3, 0, 4, 73), (4, 0, 2, 74), (4, 2, 2, 77), (5, 0, 2, 76),
               (5, 2, 2, 73), (6, 0, 4, 74), (7, 0, 4, 69)]
    p.melodia(segunda, "organo", desde=8, ganancia=0.36, pan=-0.2, brillo=0.25, ataque=0.08,
              caida=0.5)
    coro = [(0, 0, 4, 62), (1, 0, 4, 62), (2, 0, 4, 63), (3, 0, 4, 61), (4, 0, 4, 62), (5, 0, 4, 61),
            (6, 0, 4, 62), (7, 0, 4, 61)]
    p.melodia(coro, "coro", desde=8, ganancia=0.24, vocal="a")
    for k in (0, 4, 8, 12):
        p.pista.poner(p.i.campana(38), p.t(k, 2), pan=0.6, ganancia=0.28)
    return p.final(rt60=8.0, reverb=0.6, ecos=2.0, corte=6000.0)


PIEZAS = {"aldea": aldea, "noche": noche, "cementerio": cementerio, "demo": demo,
          "cripta": cripta, "tuneles": tuneles, "iglesia": iglesia}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("piezas", nargs="*", help="cuales generar (por defecto, todas)")
    ap.add_argument("--out", default=os.path.join(here, "..", "assets", "audio"))
    ap.add_argument("--seed", type=int, default=3)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for nombre in args.piezas or PIEZAS:
        # semilla por pieza (su posicion en PIEZAS): igual al generarla sola o con todas
        rng = np.random.default_rng(args.seed * 100 + list(PIEZAS).index(nombre))
        x = PIEZAS[nombre](rng)
        ruta = os.path.join(args.out, f"musica_{nombre}_v2.wav")
        escribir(ruta, x)
        print(f"{ruta}  {x.shape[1] / SR:.1f} s")


if __name__ == "__main__":
    main()
