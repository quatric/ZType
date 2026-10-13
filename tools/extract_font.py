#!/usr/bin/env python3
"""Extract one face from a .ttc into a standalone .ttf (no dependencies).
usage: extract_font.py <font.ttc> <face index> <out.ttf>"""
import struct, sys

def main(src, idx, out):
    d = open(src, 'rb').read()
    assert d[:4] == b'ttcf'
    n = struct.unpack('>I', d[8:12])[0]
    off = struct.unpack('>%dI' % n, d[12:12 + 4 * n])[int(idx)]
    sfnt, nt = struct.unpack('>IH', d[off:off + 6])[0], struct.unpack('>H', d[off + 4:off + 6])[0]
    tabs = []
    for t in range(nt):
        tag, cs, to, tl = struct.unpack('>4sIII', d[off + 12 + 16 * t:off + 28 + 16 * t])
        tabs.append((tag, cs, d[to:to + tl]))
    hdr = 12 + 16 * nt
    body, ent, pos = b'', b'', hdr
    for tag, cs, data in tabs:
        ent += struct.pack('>4sIII', tag, cs, pos, len(data))
        pad = (-len(data)) % 4
        body += data + b'\0' * pad
        pos += len(data) + pad
    sr = 1
    while sr * 2 <= nt: sr *= 2
    head = struct.pack('>IHHHH', sfnt, nt, sr * 16, sr.bit_length() - 1, nt * 16 - sr * 16)
    open(out, 'wb').write(head + ent + body)

main(*sys.argv[1:4])
