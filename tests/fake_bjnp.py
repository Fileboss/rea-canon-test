#!/usr/bin/env python3
"""Minimal BJNP printer simulator, server side of the protocol observed in
the Windows port monitor (docs/protocole-mx870.md §4.6).

Answers UDP discover/get-id/job-details/close and TCP send (acknowledging
each chunk, optionally reporting "busy" first), and writes each received job
to OUTDIR/job-N.bj.

Usage: fake_bjnp.py [--port 18611] [--busy 2] OUTDIR
"""
import argparse
import os
import socket
import struct
import sys
import threading

DEVICE_ID = b'MFG:Canon;CMD:BJL,BJRaster3,BSCCe,IVEC,IVECPLI;SOJ:BJNP2,BJNPe;MDL:MX870 series;CLS:PRINTER;DES:Canon MX870 series;'


def header(cmd, seq, session, payload=b''):
    return b'BJNP' + struct.pack('>BBHHHI', 0x81, cmd, 0, seq, session, len(payload)) + payload


def parse(h):
    if len(h) < 16 or h[:4] != b'BJNP':
        raise ValueError('bad magic')
    dev, cmd, err, seq, session, plen = struct.unpack('>BBHHHI', h[4:16])
    if dev != 0x01:
        raise ValueError(f'unexpected device type {dev:#x}')
    return cmd, seq, session, plen


class Printer:
    def __init__(self, outdir, busy):
        self.outdir = outdir
        self.busy = busy
        self.session = 0x1234
        self.jobs = 0
        self.log = []

    def udp(self, sock):
        while True:
            data, addr = sock.recvfrom(4096)
            try:
                cmd, seq, session, plen = parse(data)
            except ValueError as e:
                print(f'UDP bad packet: {e}', file=sys.stderr)
                continue
            payload = data[16:16 + plen]
            self.log.append(('udp', cmd, plen))
            if cmd == 0x01:
                sock.sendto(header(cmd, seq, 0, b'\x00' * 16), addr)
            elif cmd == 0x30:
                sock.sendto(header(cmd, seq, 0, struct.pack('>H', len(DEVICE_ID) + 2) + DEVICE_ID), addr)
            elif cmd == 0x10:
                assert plen == 392, f'job details payload {plen} != 392'
                fields = [payload[o:o + n].decode('utf-16-le').rstrip('\0')
                          for o, n in ((8, 64), (72, 64), (136, 256))]
                print(f'job details: host={fields[0]!r} user={fields[1]!r} title={fields[2]!r}')
                self.session += 1
                sock.sendto(header(cmd, seq, self.session, b'\x00' * 4), addr)
            elif cmd == 0x11:
                print(f'close session {session:#x}')
                sock.sendto(header(cmd, seq, session), addr)

    def tcp(self, srv):
        while True:
            conn, _ = srv.accept()
            self.jobs += 1
            path = os.path.join(self.outdir, f'job-{self.jobs}.bj')
            busy = self.busy
            total = chunks = 0
            with open(path, 'wb') as f:
                while True:
                    h = self.recv_all(conn, 16)
                    if not h:
                        break
                    cmd, seq, session, plen = parse(h)
                    assert cmd == 0x21, f'unexpected TCP command {cmd:#x}'
                    assert session == self.session, f'session {session:#x} != {self.session:#x}'
                    data = self.recv_all(conn, plen)
                    if busy:  # report busy: nothing accepted, client must resend
                        busy -= 1
                        conn.sendall(header(cmd, seq, session, struct.pack('>I', 0)))
                        continue
                    f.write(data)
                    total += plen
                    chunks += 1
                    conn.sendall(header(cmd, seq, session, struct.pack('>I', plen)))
            conn.close()
            print(f'job {self.jobs}: {total} bytes in {chunks} chunks -> {path}', flush=True)

    @staticmethod
    def recv_all(conn, n):
        buf = b''
        while len(buf) < n:
            part = conn.recv(n - len(buf))
            if not part:
                return buf if buf else None
            buf += part
        return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir')
    ap.add_argument('--port', type=int, default=18611)
    ap.add_argument('--busy', type=int, default=0, help='answer "busy" to the first N chunks of each job')
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    p = Printer(args.outdir, args.busy)

    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.bind(('127.0.0.1', args.port))
    t = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    t.bind(('127.0.0.1', args.port))
    t.listen(4)
    threading.Thread(target=p.udp, args=(u,), daemon=True).start()
    print(f'fake BJNP printer on 127.0.0.1:{args.port}', flush=True)
    p.tcp(t)


if __name__ == '__main__':
    main()
