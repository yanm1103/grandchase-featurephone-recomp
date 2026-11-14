"""Monta a pasta de audio do recomp: para cada N.mmf do jogo, copia o WAV ja renderizado (musicas/N.wav ou efeitos/N.wav)
com o nome <hash FNV-64 dos bytes do mmf>.wav, que e como o runtime (host_audio_load) procura o clipe.
Uso: python -I make_audio.py <pasta_jar_extraida> <pasta_wavs (music\wav)> <saida (audio)>"""
import os, sys, shutil, glob
jar, wavs, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
def fnv64(b):
    h = 0xcbf29ce484222325
    for x in b: h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h
n = miss = 0
for f in sorted(glob.glob(os.path.join(jar, '*.mmf'))):
    name = os.path.splitext(os.path.basename(f))[0]
    src = None
    for sub in ('musicas', 'efeitos'):
        p = os.path.join(wavs, sub, name + '.wav')
        if os.path.exists(p): src = p; break
    if not src: miss += 1; print('sem WAV para', name); continue
    h = fnv64(open(f, 'rb').read())
    shutil.copyfile(src, os.path.join(out, f'{h:016x}.wav')); n += 1
print(f'{n} clipes mapeados, {miss} sem WAV')
