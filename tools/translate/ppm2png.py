"""ppm2png.py entrada.ppm saida.png [escala]  (sem dependencias)"""
import sys, zlib, struct
d = open(sys.argv[1], 'rb').read(); sc = int(sys.argv[3]) if len(sys.argv) > 3 else 2
parts = d.split(b'\n', 3); w, h = map(int, parts[1].split()); px = parts[3]
rows = []
for y in range(h):
    row = bytearray()
    for x in range(w):
        row += px[(y * w + x) * 3:(y * w + x) * 3 + 3] * sc
    for _ in range(sc): rows.append(b'\0' + bytes(row))
raw = b''.join(rows)
def ch(t, b): c = struct.pack('>I', len(b)) + t + b; return c + struct.pack('>I', zlib.crc32(t + b))
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w * sc, h * sc, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
