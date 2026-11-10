#!/usr/bin/env python3
"""
charatlas: pone una FOTO de cara real en el atlas de un personaje (persona_m / persona_f).

El modelo proyecta la region FACE del atlas (48x56 px) sobre el frente de la cabeza, de
forma plana. Las fotos de cara de los juegos de PS1 funcionan igual: poca resolucion,
proyeccion plana y bordes estirados por los lados. Esta herramienta:
  1. recorta la cara de tu foto y la reduce a 48x56 (promedio de area),
  2. la pasa a 15 bits con dithering ordenado (como texconv.py),
  3. deduce el color de piel y de pelo de la propia foto,
  4. regenera nuca/lados, cuello, manos y pelo para que casen con esa cara,
  5. opcionalmente cambia camisa y pantalon por texturas tuyas.

Requiere:  pip install pillow numpy

Uso:
  python charatlas.py base.png foto.jpg salida.png --box 120,60,300,350
  python charatlas.py persona_f1_tex.png retrato.jpg ana_tex.png --box 80,40,260,300 --long-hair
  python charatlas.py persona_m1_tex.png retrato.jpg luis_tex.png --box 80,40,260,300 --shirt tela.jpg

  base.png   cualquier atlas de persona (aporta el cuerpo y la ropa)
  --box-rel x,y,w,h   lo mismo que --box, pero en proporciones (0..1) de la foto
  --guide guia.png    guarda tu foto con el recuadro (rojo) y la linea de los ojos (verde)
  --preview cara.png  guarda la cara resultante ampliada x8
  --box x,y,w,h   zona de la foto: de la CORONILLA (pelo incluido) a la BARBILLA, ancha como
                  la cabeza. Los ojos deben quedar a ~45% desde arriba, como en los atlas base.
"""
import argparse
import sys

import numpy as np
from PIL import Image, ImageDraw

# Mismas regiones que el generador del modelo (x0, y0, x1, y1) en pixeles del atlas 128x128.
R = dict(FACE=(0, 0, 48, 56), HEADBACK=(48, 0, 96, 56), HAIR=(96, 0, 128, 32), SKIN=(96, 32, 128, 48),
         TORSO=(0, 56, 96, 96), SLEEVE=(96, 48, 128, 80), FOREARM=(96, 80, 128, 112),
         PANTS=(0, 96, 64, 128), SHOES=(64, 96, 96, 128))

_B = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32)
BAYER = (_B + 0.5) / 16 - 0.5


def parse_floats(text, n):
    v = [float(x) for x in text.split(",")]
    if len(v) != n:
        raise argparse.ArgumentTypeError(f"se esperaban {n} valores separados por comas")
    return v


def parse_ints(text, n):
    v = [int(x) for x in text.split(",")]
    if len(v) != n:
        raise argparse.ArgumentTypeError(f"se esperaban {n} valores separados por comas")
    return v


def quantize(rgb, levels=32, dither=0.7):
    h, w = rgb.shape[:2]
    steps = levels - 1
    pat = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w][..., None]
    return np.clip(np.floor(np.clip(rgb, 0, 1) * steps + 0.5 + pat * dither) / steps, 0, 1)


def adjust(rgb, brightness, contrast, saturation):
    rgb = rgb * brightness
    rgb = (rgb - 0.5) * contrast + 0.5
    luma = (rgb * np.array([0.299, 0.587, 0.114])).sum(-1, keepdims=True)
    return np.clip(luma + (rgb - luma) * saturation, 0, 1)


def load_resized(path, size, box=None, method=Image.BOX):
    img = Image.open(path).convert("RGB")
    if box:
        x, y, w, h = box
        img = img.crop((x, y, x + w, y + h))
    big = img.width > size[0] or img.height > size[1]
    return np.asarray(img.resize(size, method if big else Image.BICUBIC), np.float32) / 255.0


def fit_region(path, rect):
    """Cubre una region con una imagen (recorte central, sin deformarla)."""
    w, h = rect[2] - rect[0], rect[3] - rect[1]
    img = Image.open(path).convert("RGB")
    s = max(w / img.width, h / img.height)
    nw, nh = max(w, round(img.width * s)), max(h, round(img.height * s))
    img = img.resize((nw, nh), Image.BOX if s < 1 else Image.BICUBIC)
    l, t = (nw - w) // 2, (nh - h) // 2
    return np.asarray(img.crop((l, t, l + w, t + h)), np.float32) / 255.0


def strands(h, w, color, rng, salt=0.0):
    cols = np.convolve(rng.random(w + 2), [0.25, 0.5, 0.25], mode="valid")[:w]
    tex = (0.62 + 0.76 * cols)[None, :] * (0.92 + 0.16 * rng.random((h, w)))
    return np.clip(color[None, None, :] * tex[..., None], 0, 1)


def auto_box(size):
    """Recuadro centrado con la proporcion de la cara del atlas (48:56)."""
    W, H = size
    aspect = 48 / 56
    if W / H > aspect:
        h = H; w = int(h * aspect)
    else:
        w = W; h = int(w / aspect)
    return [(W - w) // 2, (H - h) // 2, w, h]


def draw_guide(photo, box, path):
    img = Image.open(photo).convert("RGB")
    x, y, w, h = box
    t = max(2, img.width // 300)
    d = ImageDraw.Draw(img)
    d.rectangle([x, y, x + w, y + h], outline=(255, 40, 40), width=t)
    ey = y + int(h * 0.45)
    d.line([x, ey, x + w, ey], fill=(40, 255, 40), width=t)                 # donde deben ir los ojos
    d.line([x + w // 2, y, x + w // 2, y + h], fill=(255, 255, 0), width=max(1, t // 2))
    img.save(path)


def sstep(a, b, x):
    t = np.clip((x - a) / (b - a + 1e-9), 0, 1)
    return t * t * (3 - 2 * t)


def main():
    ap = argparse.ArgumentParser(description="Pone una foto de cara en el atlas de un personaje.")
    ap.add_argument("base"); ap.add_argument("photo"); ap.add_argument("output")
    ap.add_argument("--box", type=lambda t: parse_ints(t, 4), help="x,y,w,h de la cara en la foto")
    ap.add_argument("--box-rel", type=lambda t: parse_floats(t, 4), metavar="x,y,w,h",
                    help="como --box pero en proporciones de la foto (0..1); sirve con cualquier resolucion")
    ap.add_argument("--hair", type=lambda t: parse_ints(t, 3), help="color de pelo r,g,b (por defecto, de la foto)")
    ap.add_argument("--skin", type=lambda t: parse_ints(t, 3), help="color de piel r,g,b (por defecto, de la foto)")
    ap.add_argument("--long-hair", action="store_true", help="el pelo cubre toda la nuca (modelo persona_f)")
    ap.add_argument("--side-v", type=float, default=0.50, help="hasta donde baja el pelo en los lados, 0..1")
    ap.add_argument("--squash", type=float, default=1.0, help="<1 estrecha la cara (mas deformacion PS1)")
    ap.add_argument("--brightness", type=float, default=1.0)
    ap.add_argument("--contrast", type=float, default=1.1)
    ap.add_argument("--saturation", type=float, default=1.0)
    ap.add_argument("--levels", type=int, default=32)
    ap.add_argument("--dither", type=float, default=0.7)
    ap.add_argument("--no-edge-fix", action="store_true",
                    help="no limpiar los bordes de la cara (por defecto se funden con pelo/piel para ocultar el fondo)")
    ap.add_argument("--shirt", help="textura para torso y mangas")
    ap.add_argument("--pants", help="textura para las piernas")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--guide", metavar="ARCHIVO", help="guarda tu foto con el recuadro y la linea de los ojos")
    ap.add_argument("--preview", metavar="ARCHIVO", help="guarda la cara resultante ampliada x8")
    a = ap.parse_args()

    rng = np.random.default_rng(a.seed)
    atlas = np.asarray(Image.open(a.base).convert("RGB"), np.float32) / 255.0
    if atlas.shape[:2] != (128, 128):
        sys.exit("el atlas base debe ser de 128x128")

    # 1) cara
    fx0, fy0, fx1, fy1 = R["FACE"]
    fw, fh = fx1 - fx0, fy1 - fy0
    nw = max(8, round(fw * min(a.squash, 1.0)))
    photo_size = Image.open(a.photo).size
    if a.box and a.box_rel:
        sys.exit("usa --box o --box-rel, no los dos")
    if a.box_rel:
        W, H = photo_size
        rx, ry, rw, rh = a.box_rel
        bx0, by0 = round(rx * W), round(ry * H)
        box = [bx0, by0, min(round(rw * W), W - bx0), min(round(rh * H), H - by0)]
    else:
        box = a.box or auto_box(photo_size)
    bx, by, bw, bh = box
    if bw <= 0 or bh <= 0 or bx < 0 or by < 0 or bx + bw > photo_size[0] or by + bh > photo_size[1]:
        sys.exit(f"--box {box} se sale de la foto ({photo_size[0]}x{photo_size[1]} px)")
    if a.guide:
        draw_guide(a.photo, box, a.guide)
        print("guia:", a.guide, "(rojo = recorte, verde = linea de los ojos)")
    face = load_resized(a.photo, (nw, fh), box)
    if nw < fw:
        pad = fw - nw
        face = np.pad(face, ((0, 0), (pad // 2, pad - pad // 2), (0, 0)), mode="edge")
    face = adjust(face, a.brightness, a.contrast, a.saturation)

    # 2) colores deducidos de la foto
    def med(r0, r1, c0, c1):
        return np.median(face[int(r0 * fh):int(r1 * fh), int(c0 * fw):int(c1 * fw)].reshape(-1, 3), axis=0)
    skin = np.array(a.skin, np.float32) / 255 if a.skin else (med(.52, .64, .14, .32) + med(.52, .64, .68, .86)) / 2
    hair = np.array(a.hair, np.float32) / 255 if a.hair else med(.00, .10, .25, .75)

    # Los bordes del recuadro suelen traer fondo (una pared, una cortina). Como la proyeccion
    # plana los estira por los lados de la cabeza, se funden con pelo (arriba) o piel (abajo).
    if not a.no_edge_fix:
        rows = (np.arange(fh) + 0.5) / fh
        expected = np.where((rows < a.side_v)[:, None], hair[None, :], skin[None, :] * 0.95)
        for k, wgt in enumerate((0.85, 0.60, 0.35)):
            face[:, k] = face[:, k] * (1 - wgt) + expected * wgt
            face[:, fw - 1 - k] = face[:, fw - 1 - k] * (1 - wgt) + expected * wgt

    face_q = quantize(face, a.levels, a.dither)
    atlas[fy0:fy1, fx0:fx1] = face_q

    # 3) nuca y lados: pelo arriba, piel abajo, y los bordes casan con los de la cara
    bx0, by0, bx1, by1 = R["HEADBACK"]
    h, w = by1 - by0, bx1 - bx0
    V, U = np.mgrid[0:h, 0:w]; U = (U + .5) / w; V = (V + .5) / h
    back_v = 1.0 if a.long_hair else 0.58
    side_v = max(a.side_v, 0.62) if a.long_hair else a.side_v
    cover = side_v + (back_v - side_v) * np.sin(np.pi * U) ** 1.2
    hm = (1 - sstep(cover - .03, cover + .03, V))[..., None]
    skin_img = skin[None, None, :] * 0.92 * (0.94 + 0.12 * rng.random((h, w, 1)))
    back = skin_img * (1 - hm) + strands(h, w, hair, rng) * hm
    wl = np.exp(-U * 14)[..., None]                       # u=0 ↔ borde derecho de la cara
    wr = np.exp(-(1 - U) * 14)[..., None]                 # u=1 ↔ borde izquierdo de la cara
    back = back * (1 - wl) + face_q[:, -1][:, None, :] * wl
    back = back * (1 - wr) + face_q[:, 0][:, None, :] * wr
    atlas[by0:by1, bx0:bx1] = quantize(back, a.levels, a.dither)

    # 4) cuello/manos y pelo largo
    sx0, sy0, sx1, sy1 = R["SKIN"]
    atlas[sy0:sy1, sx0:sx1] = quantize(skin[None, None, :] * (0.94 + 0.12 * rng.random((sy1 - sy0, sx1 - sx0, 1))), a.levels, a.dither)
    hx0, hy0, hx1, hy1 = R["HAIR"]
    atlas[hy0:hy1, hx0:hx1] = quantize(strands(hy1 - hy0, hx1 - hx0, hair, rng), a.levels, a.dither)

    # antebrazo en piel solo si la manga es corta: se mantiene el del atlas base,
    # pero se retinta para que case con la nueva piel
    ax0, ay0, ax1, ay1 = R["FOREARM"]
    fore = atlas[ay0:ay1, ax0:ax1]
    if np.abs(fore.mean(axis=(0, 1)) - skin).max() < 0.25:      # parece piel
        atlas[ay0:ay1, ax0:ax1] = quantize(skin[None, None, :] * (0.94 + 0.12 * rng.random((ay1 - ay0, ax1 - ax0, 1))), a.levels, a.dither)

    # 5) ropa opcional
    def cloth(rect, path):
        img = fit_region(path, rect)
        atlas[rect[1]:rect[3], rect[0]:rect[2]] = quantize(adjust(img, a.brightness, a.contrast, a.saturation), a.levels, a.dither)
    if a.shirt:
        for name in ("TORSO", "SLEEVE", "FOREARM"):
            cloth(R[name], a.shirt)
    if a.pants:
        cloth(R["PANTS"], a.pants)

    out = Image.fromarray((np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8))
    out.save(a.output)
    if a.preview:
        out.crop((0, 0, 48, 56)).resize((384, 448), Image.NEAREST).save(a.preview)
        print("vista previa de la cara:", a.preview)
    f = lambda c: tuple(int(v * 255) for v in c)
    print(f"{a.output}: piel {f(skin)}, pelo {f(hair)}, cara {fw}x{fh}"
          + f", recorte {box}" + ("" if (a.box or a.box_rel) else " (automatico)"))


if __name__ == "__main__":
    sys.exit(main())
