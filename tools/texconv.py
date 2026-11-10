#!/usr/bin/env python3
"""
texconv: convierte cualquier imagen (foto, textura libre, pintura) al aspecto de
una textura de PS1: poca resolucion, color de 15 bits (32 niveles por canal),
trama de dithering ordenado y, opcionalmente, paleta limitada y ruido.

Requiere:  pip install pillow numpy

Ejemplos:
  python texconv.py ladrillo.jpg ladrillo.png --size 64
  python texconv.py cesped.jpg cesped.png --size 64 --tileable --noise 0.02
  python texconv.py piedra.png piedra.png --size 64x64 --palette 32 --contrast 1.15
  python texconv.py muro.jpg muro.png --size 32x32 --tint 1.05,1.0,0.9 --preview muro_x8.png
"""
import argparse
import sys

import numpy as np
from PIL import Image

# Matriz de Bayer 4x4 centrada en 0 (la misma que usan los shaders del motor).
_BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]],
                   dtype=np.float32)
BAYER4 = (_BAYER4 + 0.5) / 16.0 - 0.5


def parse_size(text: str):
    if "x" in text.lower():
        w, h = text.lower().split("x", 1)
        return int(w), int(h)
    n = int(text)
    return n, n


def parse_tint(text: str):
    parts = [float(p) for p in text.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("--tint necesita tres valores: r,g,b")
    return np.array(parts, dtype=np.float32)


def make_seamless(rgb: np.ndarray) -> np.ndarray:
    """
    Hace que la imagen encaje consigo misma. Se mezcla con su version desplazada
    media imagen: los bordes de la original son el centro de la desplazada (continuo),
    y el peso vale 1 en el centro de la original y 0 en sus bordes.
    Pierde algo de contraste en la zona mezclada, pero elimina la costura.
    """
    h, w = rgb.shape[:2]
    shifted = np.roll(np.roll(rgb, h // 2, axis=0), w // 2, axis=1)
    wy = 1.0 - np.abs(np.linspace(-1.0, 1.0, h, dtype=np.float32))
    wx = 1.0 - np.abs(np.linspace(-1.0, 1.0, w, dtype=np.float32))
    weight = (wy[:, None] * wx[None, :])[..., None]
    weight = weight * weight * (3.0 - 2.0 * weight)          # smoothstep
    return rgb * weight + shifted * (1.0 - weight)


def seam_error(rgb: np.ndarray) -> float:
    """Diferencia media entre bordes opuestos (0 = encaja perfecto)."""
    horiz = np.abs(rgb[:, 0] - rgb[:, -1]).mean()
    vert = np.abs(rgb[0, :] - rgb[-1, :]).mean()
    return float((horiz + vert) * 0.5)


def adjust_color(rgb, brightness, contrast, saturation, tint):
    rgb = rgb * brightness
    rgb = (rgb - 0.5) * contrast + 0.5
    luma = (rgb * np.array([0.299, 0.587, 0.114], dtype=np.float32)).sum(-1, keepdims=True)
    rgb = luma + (rgb - luma) * saturation
    return rgb * tint


def quantize_ordered(rgb, levels, dither):
    """Reduce cada canal a 'levels' niveles con dithering ordenado (Bayer 4x4)."""
    h, w = rgb.shape[:2]
    steps = max(levels, 2) - 1
    pattern = np.tile(BAYER4, (h // 4 + 1, w // 4 + 1))[:h, :w][..., None]
    return np.floor(rgb * steps + 0.5 + pattern * dither) / steps


def main():
    ap = argparse.ArgumentParser(description="Convierte una imagen a textura estilo PS1.")
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--size", type=parse_size, default=(64, 64), metavar="N|WxH",
                    help="tamano final en pixeles (por defecto 64)")
    ap.add_argument("--tileable", action="store_true",
                    help="hace la textura repetible sin costuras antes de reducirla")
    ap.add_argument("--levels", type=int, default=32,
                    help="niveles por canal (32 = 15 bits, como la PS1)")
    ap.add_argument("--dither", type=float, default=0.8,
                    help="fuerza del dithering ordenado, 0..1 (por defecto 0.8)")
    ap.add_argument("--palette", type=int, default=0, metavar="N",
                    help="limita a N colores (por ejemplo 16 o 64); 0 = sin limite")
    ap.add_argument("--noise", type=float, default=0.0,
                    help="ruido de luminosidad, 0..0.1 (por ejemplo 0.02)")
    ap.add_argument("--brightness", type=float, default=1.0)
    ap.add_argument("--contrast", type=float, default=1.0)
    ap.add_argument("--saturation", type=float, default=1.0)
    ap.add_argument("--tint", type=parse_tint, default=np.ones(3, dtype=np.float32),
                    help="multiplicador de color r,g,b (por ejemplo 1.05,1.0,0.9)")
    ap.add_argument("--alpha-cut", type=float, default=0.5,
                    help="umbral para dejar el alfa en 0 o 255 (por defecto 0.5)")
    ap.add_argument("--seed", type=int, default=1, help="semilla del ruido")
    ap.add_argument("--preview", metavar="ARCHIVO",
                    help="guarda ademas una vista ampliada x8 sin suavizado")
    args = ap.parse_args()

    img = Image.open(args.input).convert("RGBA")
    rgb = np.asarray(img, dtype=np.float32)[..., :3] / 255.0
    alpha = np.asarray(img, dtype=np.float32)[..., 3] / 255.0
    has_alpha = bool((alpha < 0.999).any())

    before = None
    if args.tileable:
        before = seam_error(rgb)
        rgb = make_seamless(rgb)

    # Reduccion por promedio de area (BOX): sin aliasing, y cada texel final es
    # la media de los pixeles que cubre. Si hay que ampliar, bicubico.
    w, h = args.size
    big = img.width > w or img.height > h
    method = Image.BOX if big else Image.BICUBIC
    small_rgb = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).resize((w, h), method)
    rgb = np.asarray(small_rgb, dtype=np.float32) / 255.0
    if has_alpha:
        a_img = Image.fromarray((alpha * 255).astype(np.uint8)).resize((w, h), method)
        alpha = np.asarray(a_img, dtype=np.float32) / 255.0
    else:
        alpha = np.ones((h, w), dtype=np.float32)

    rgb = adjust_color(rgb, args.brightness, args.contrast, args.saturation, args.tint)

    if args.noise > 0:
        rng = np.random.default_rng(args.seed)
        rgb = rgb + rng.normal(0.0, args.noise, size=(h, w, 1)).astype(np.float32)

    rgb = np.clip(rgb, 0.0, 1.0)
    rgb = quantize_ordered(rgb, args.levels, args.dither)
    rgb = np.clip(rgb, 0.0, 1.0)

    out8 = (rgb * 255.0 + 0.5).astype(np.uint8)

    if args.palette > 0:
        pal = Image.fromarray(out8).quantize(colors=args.palette, method=Image.MEDIANCUT,
                                             dither=Image.Dither.NONE)
        out8 = np.asarray(pal.convert("RGB"))

    a8 = np.where(alpha >= args.alpha_cut, 255, 0).astype(np.uint8)   # alfa duro
    result = Image.fromarray(np.dstack([out8, a8]), "RGBA")
    result.save(args.output)

    colors = len({tuple(p) for p in out8.reshape(-1, 3)})
    print(f"{args.input} -> {args.output}: {w}x{h}, {args.levels} niveles/canal, {colors} colores"
          + (f", costura {before:.3f} -> {seam_error(np.asarray(Image.open(args.output).convert('RGB'), dtype=np.float32) / 255.0):.3f}"
             if before is not None else ""))

    if args.preview:
        result.resize((w * 8, h * 8), Image.NEAREST).save(args.preview)
        print("vista previa:", args.preview)


if __name__ == "__main__":
    sys.exit(main())
