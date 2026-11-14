"""montage.py saida.png colunas escala ppm1 ppm2 ...  (folha de contato sem dependencias)"""
import sys, zlib, struct
out, cols, sc = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); files = sys.argv[4:]
imgs = []
for f in files:
    d = open(f, 'rb').read(); p = d.split(b'\n', 3); w, h = map(int, p[1].split()); imgs.append((w, h, p[3]))
w, h = imgs[0][0], imgs[0][1]; rows = (len(imgs) + cols - 1) // cols
W, H = cols * w * sc, rows * h * sc; buf = [bytearray(b'\0' * (W * 3)) for _ in range(H)]
for k, (iw, ih, px) in enumerate(imgs):
    ox, oy = (k % cols) * w * sc, (k // cols) * h * sc
    for y in range(ih):
        row = bytearray()
        for x in range(iw): row += px[(y * iw + x) * 3:(y * iw + x) * 3 + 3] * sc
        for s in range(sc): buf[oy + y * sc + s][ox * 3:ox * 3 + len(row)] = row
raw = b''.join(b'\0' + bytes(r) for r in buf)
def ch(t, b): c = struct.pack('>I', len(b)) + t + b; return c + struct.pack('>I', zlib.crc32(t + b))
open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
