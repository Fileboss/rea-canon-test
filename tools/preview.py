#!/usr/bin/env python3
"""Recombine bjdump plane images (PGM) into an approximate RGB preview (PPM).

Usage: preview.py PLANES_DIR PAGE_NUMBER OUT.ppm [scale]
"""
import os
import sys


def read_pgm(path):
    data = open(path, 'rb').read()
    head, _, pix = data.partition(b'\n')
    _, w, h, _ = head.split()
    return int(w), int(h), pix


def main(d, page, out, scale=4):
    planes = {}
    for name in os.listdir(d):
        if name.startswith(f'page{page}_') and name.endswith('.pgm'):
            key = name[len(f'page{page}_'):-4]
            planes[key] = read_pgm(os.path.join(d, name))
    w = max(p[0] for p in planes.values())
    h = max(p[1] for p in planes.values())
    ow, oh = w // scale, h // scale

    def amount(key, x, y):  # ink coverage 0..1 averaged over the scale block
        if key not in planes:
            return 0.0
        pw, ph, pix = planes[key]
        tot = n = 0
        for yy in range(y * scale, min((y + 1) * scale, ph)):
            row = yy * pw
            for xx in range(x * scale, min((x + 1) * scale, pw)):
                tot += 255 - pix[row + xx]
                n += 1
        return tot / (255 * n) if n else 0.0

    rows = bytearray()
    for y in range(oh):
        for x in range(ow):
            c, m, yv = amount('C', x, y), amount('M', x, y), amount('Y', x, y)
            k = min(1.0, amount('K', x, y) + amount('k_dye', x, y))
            rows += bytes(int(255 * (1 - min(1, v)) * (1 - k)) for v in (c, m, yv))
    with open(out, 'wb') as f:
        f.write(f'P6 {ow} {oh} 255\n'.encode())
        f.write(rows)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 4)
