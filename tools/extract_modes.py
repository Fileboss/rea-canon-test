#!/usr/bin/env python3
"""Extract MX870 print-mode records (ESC (c / (d / (t + ICC name) from the
user's own copy of the Windows model database (cnm_0367.dll).

Usage: extract_modes.py path/to/cnm_0367.dll > modes.json
"""
import json
import re
import sys

ESC_C = re.compile(rb'\x1b\(c\x03\x00(...)\x1b\(d\x04\x00(....)\x1b\(t\x2a\x00(.{42})', re.S)
ICC = re.compile(rb'(cnb[a-z0-9]{5}\.icm)\x00', re.I)


def main(path):
    data = open(path, 'rb').read()
    modes = []
    for m in ESC_C.finditer(data):
        c, d, t = m.groups()
        tail = data[m.end():m.end() + 64]
        icc = ICC.search(tail)
        modes.append({
            'offset': m.start(),
            'c': c.hex(' '),
            'model_byte': c[0], 'media': c[1], 'quality': c[2],
            'dpi': [int.from_bytes(d[0:2], 'big'), int.from_bytes(d[2:4], 'big')],
            't_header': t[:3].hex(' '),
            'inks': [{'slot': i, 'info': t[3 + 3 * i], 'levels': t[5 + 3 * i]}
                     for i in range(13) if t[3 + 3 * i] or t[5 + 3 * i]],
            't': t.hex(' '),
            'icc': icc.group(1).decode().lower() if icc and icc.start() < 24 else None,
        })
    json.dump(modes, sys.stdout, indent=1)


if __name__ == '__main__':
    main(sys.argv[1])
