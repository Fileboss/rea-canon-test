#!/usr/bin/env python3
"""Decode a Canon BJ raster stream (MX870 dialect) for inspection.

Usage:
  bjdump.py STREAM               list commands, check framing
  bjdump.py STREAM --planes DIR  also rebuild each page/plane as PGM images

Independent of libmx870 so it can validate its output (and Windows captures).
"""
import argparse
import os
import struct
import sys

SLOT_LETTERS = 'CMYKcmk'


def packbits_decode(data, start, end):
    """Decode one raster line up to its 0x80 terminator. Returns (bytes, next)."""
    out = bytearray()
    i = start
    while i < end:
        h = data[i]
        i += 1
        if h == 0x80:
            return bytes(out), i
        if h < 0x80:
            out += data[i:i + h + 1]
            i += h + 1
        else:
            out += bytes([data[i]]) * (257 - h)
            i += 1
    raise ValueError('raster line without 0x80 terminator')


def unpack_levels(line, bits, pack5, width):
    px = []
    if pack5:
        for b in line:
            if b > 242:
                raise ValueError(f'invalid base-3 byte {b}')
            digits = []
            for _ in range(5):
                digits.append(b % 3)
                b //= 3
            px += reversed(digits)
    else:
        per = 8 // bits
        mask = (1 << bits) - 1
        for b in line:
            for k in range(per):
                px.append((b >> (8 - bits * (k + 1))) & mask)
    px = px[:width]
    return px + [0] * (width - len(px))


class Page:
    def __init__(self):
        self.inks = {}        # letter -> (bits, pack5, levels)
        self.dpi = None
        self.lpb = None
        self.order = None
        self.rows = {}        # letter -> list of level lists
        self.row = 0          # current raster line (top of band)
        self.band_pos = 0     # index in order of the next ESC (F
        self.width = 0


def parse(data, planes_dir=None, verbose=True):
    i, n = 0, len(data)
    pages, page = [], None
    stats = {}

    def log(msg):
        if verbose:
            print(msg)

    while i < n:
        if data.startswith(b'<?xml', i):
            j = data.find(b'</cmd>', i)
            j = n if j < 0 else j + 6
            op = data[i:j].split(b'<ivec:operation>')[1].split(b'<')[0].decode()
            log(f'{i:08x} XML {op}')
            i = j
            continue
        b = data[i]
        if b == 0x0c:
            log(f'{i:08x} FF (page end)')
            if page:
                if page.band_pos:
                    raise ValueError(f'page ends inside an incomplete band at 0x{i:x}')
                pages.append(page)
                page = None
            i += 1
            continue
        if data.startswith(b'\x1b@', i):
            log(f'{i:08x} ESC @ (job end)')
            i += 2
            continue
        if data.startswith(b'\x1b[K', i):
            ln = struct.unpack_from('<H', data, i + 3)[0]
            arg = data[i + 5:i + 5 + ln]
            i += 5 + ln
            if arg == b'\x00\x1f':
                j = data.find(b'BJLEND\n', i) + 7
                log(f'{i:08x} BJL {data[i:j].decode().strip().replace(chr(10), " | ")}')
                i = j
            else:
                log(f'{i:08x} ESC [K {arg.hex(" ")} (reset)')
            continue
        if not data.startswith(b'\x1b(', i):
            raise ValueError(f'unexpected byte 0x{b:02x} at 0x{i:x}')

        c = chr(data[i + 2])
        ln = struct.unpack_from('<H', data, i + 3)[0]
        arg = data[i + 5:i + 5 + ln]
        if len(arg) != ln:
            raise ValueError(f'truncated ESC ({c} at 0x{i:x}')
        stats[c] = stats.get(c, 0) + 1
        if page is None and c not in 'b':
            page = Page()

        if c == 'd':
            page.dpi = struct.unpack('>HH', arg)
            log(f'{i:08x} ESC (d resolution {page.dpi[0]}x{page.dpi[1]}')
        elif c == 't':
            for s in range(13):
                info, lv = arg[3 + 3 * s], arg[5 + 3 * s]
                if lv and s < len(SLOT_LETTERS):
                    page.inks[SLOT_LETTERS[s]] = (info & 0x1f, bool(info & 0x20), lv)
            desc = ' '.join(f'{k}:{"5px" if p else ""}{bt}b/{lv}' for k, (bt, p, lv) in page.inks.items())
            log(f'{i:08x} ESC (t {arg[:3].hex(" ")} {desc}')
        elif c == 'p':
            f = struct.unpack('>HHHHIH8i', arg)
            page.width = f[8]
            log(f'{i:08x} ESC (p area x={f[6]} y={f[7]} {f[8]}x{f[9]} paper {f[12]}x{f[13]} (1/600in)')
        elif c == 'J':
            page.lpb = arg[0]
            log(f'{i:08x} ESC (J {arg[0]} lines per block')
        elif c == 'L':
            if page.band_pos:
                raise ValueError(f'ESC (L inside an incomplete band at 0x{i:x}')
            page.order = arg.decode('latin1')
            log(f'{i:08x} ESC (L {page.order}')
        elif c == 'e':
            if page.band_pos:
                raise ValueError(f'ESC (e inside an incomplete band at 0x{i:x}')
            nblk = struct.unpack('>H', arg)[0]
            page.row += nblk * page.lpb
            log(f'{i:08x} ESC (e skip {nblk} blocks')
        elif c == 'F':
            # One ESC (F per letter of the current ESC (L order makes a band.
            if not page.order:
                raise ValueError(f'ESC (F before any ESC (L at 0x{i:x}')
            decode_band(page, page.order[page.band_pos], data, i + 5, i + 5 + ln)
            page.band_pos += 1
            if page.band_pos == len(page.order):
                page.band_pos = 0
                page.row += page.lpb
        else:
            log(f'{i:08x} ESC ({c} {arg.hex(" ")}')
        i += 5 + ln

    if page:
        pages.append(page)
    print(f'{len(pages)} page(s); command counts: {dict(sorted(stats.items()))}')
    for k, p in enumerate(pages):
        for letter, rows in p.rows.items():
            inked = sum(1 for r in rows if r and any(r))
            print(f'  page {k + 1} plane {letter}: {len(rows)} lines, {inked} with ink')
        if planes_dir:
            write_planes(p, k + 1, planes_dir)
    return pages


def decode_band(page, letter, data, start, end):
    bits, pack5, levels = page.inks[letter.replace('\x00', '')]
    width = page.width * page.dpi[0] // 600
    rows = page.rows.setdefault(letter, [])
    i, line = start, 0
    while i < end:
        raw, i = packbits_decode(data, i, end)
        y = page.row + line
        while len(rows) <= y:
            rows.append(None)
        px = unpack_levels(raw, bits, pack5, width)
        if max(px, default=0) >= levels:
            raise ValueError(f'level {max(px)} >= {levels} in plane {letter}')
        rows[y] = px
        line += 1
    if line not in (0, page.lpb):
        raise ValueError(f'band of plane {letter} has {line} lines, expected {page.lpb}')


def write_planes(page, number, outdir):
    os.makedirs(outdir, exist_ok=True)
    width = page.width * page.dpi[0] // 600
    for letter, rows in page.rows.items():
        levels = page.inks[letter][2]
        name = f'page{number}_{letter if letter.isupper() else letter + "_dye"}.pgm'
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(f'P5 {width} {len(rows)} 255\n'.encode())
            for r in rows:
                r = r or [0] * width
                f.write(bytes(255 - v * 255 // (levels - 1) for v in r))
        print(f'  wrote {name}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('stream')
    ap.add_argument('--planes', metavar='DIR')
    ap.add_argument('-q', '--quiet', action='store_true')
    args = ap.parse_args()
    try:
        parse(open(args.stream, 'rb').read(), args.planes, not args.quiet)
    except (ValueError, KeyError) as e:
        print(f'INVALID STREAM: {e}', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
