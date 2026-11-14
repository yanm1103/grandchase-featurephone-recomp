"""Comprime a pasta de audio do recomp: cada <hash>.wav grande (musica, >512 KB) vira <hash>.ogg (Vorbis, com perdas) e o WAV original e movido
para <pasta>_wav (backup, fora do APK). Efeitos pequenos continuam em WAV. O runtime (platform/main_sdl.c, get_wav) procura .ogg e depois .wav.
Uso: python -I tools/compress_audio.py <pasta_audio> [qualidade 0-10, padrao 4 (~128 kbps)]   Precisa do ffmpeg com libvorbis no PATH."""
import os, sys, glob, shutil, subprocess
d = sys.argv[1].rstrip('/\\'); q = sys.argv[2] if len(sys.argv) > 2 else '4'
bak = d + '_wav'; os.makedirs(bak, exist_ok=True)
before = after = 0
for f in sorted(glob.glob(os.path.join(d, '*.wav'))):
    sz = os.path.getsize(f)
    if sz < 512 * 1024: continue
    out = f[:-4] + '.ogg'
    r = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', f, '-c:a', 'libvorbis', '-q:a', q, out], capture_output=True, text=True)
    if r.returncode or not os.path.exists(out): print('FALHOU', f, r.stderr[-200:]); continue
    before += sz; after += os.path.getsize(out)
    shutil.move(f, os.path.join(bak, os.path.basename(f)))
print(f'{before / 1e6:.1f} MB de WAV -> {after / 1e6:.1f} MB de OGG; pasta {d}: {sum(os.path.getsize(x) for x in glob.glob(os.path.join(d, "*"))) / 1e6:.1f} MB')
