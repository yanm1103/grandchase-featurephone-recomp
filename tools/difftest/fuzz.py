"""Teste diferencial por fuzzing de entrada: roda o recomp (blocos lifted) e o backend Unicorn com o mesmo roteiro aleatorio
de teclas e compara os traces (chamadas de API + hash do framebuffer em cada quadro) e os arquivos de save.

Uso: python -I fuzz.py <resdir> [seeds=6] [frames=3000] [semente_inicial=1]
"""
import os, sys, random, subprocess, filecmp, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(R, 'build', 'out'); WORK = os.path.join(R, 'build', 'fuzz'); os.makedirs(WORK, exist_ok=True)
res = sys.argv[1]
nseeds = int(sys.argv[2]) if len(sys.argv) > 2 else 6
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
s0 = int(sys.argv[4]) if len(sys.argv) > 4 else 1
KEYS = [-1, -2, -3, -4, -5, -5, -5, -6, -7, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 35, 42, -16]
weights = [4, 4, 4, 4, 8, 8, 8, 2, 2] + [1] * 13


def script(seed, total_ms):
    rnd = random.Random(seed); t = 200; ev = []
    held = {}
    while t < total_ms:
        k = rnd.choices(KEYS, weights)[0]
        dur = rnd.choice([40, 60, 80, 120, 200, 400])
        ev.append((t, k, 'p')); ev.append((t + dur, k, 'r'))
        t += rnd.choice([60, 100, 150, 300, 600, 1200]) + dur // 2
    ev.sort(key=lambda e: e[0])
    return ev


bad = 0
for seed in range(s0, s0 + nseeds):
    d = os.path.join(WORK, f's{seed}'); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    sa, sb = os.path.join(d, 'save_lift'), os.path.join(d, 'save_ref'); os.makedirs(sa); os.makedirs(sb)
    keys = os.path.join(d, 'keys.txt')
    with open(keys, 'w') as f:
        for ms, k, a in script(seed, frames * 40): f.write(f'{ms} {k} {a}\n')
    r1 = subprocess.run([os.path.join(OUT, 'gc1_headless.exe'), res, sa, '--frames', str(frames), '--trace', os.path.join(d, 'lift.txt'), '--keys', keys], capture_output=True, text=True)
    r2 = subprocess.run([os.path.join(OUT, 'gc1_ref_headless.exe'), res, sb, '--frames', str(frames), '--trace', os.path.join(d, 'ref.txt'), '--keys', keys], capture_output=True, text=True)
    la, lb = os.path.join(d, 'lift.txt'), os.path.join(d, 'ref.txt')
    same = os.path.getsize(la) == os.path.getsize(lb) and filecmp.cmp(la, lb, shallow=False)
    saves = sorted(os.listdir(sa)) == sorted(os.listdir(sb)) and all(filecmp.cmp(os.path.join(sa, n), os.path.join(sb, n), shallow=False) for n in os.listdir(sa))
    n = sum(1 for _ in open(la, errors='replace'))
    tag = 'OK ' if same and saves and r1.returncode == 0 and r2.returncode == 0 else 'DIVERGE'
    print(f'seed {seed}: {tag} linhas={n} lift_rc={r1.returncode} ref_rc={r2.returncode} saves_iguais={saves} saves={sorted(os.listdir(sa))}')
    if tag != 'OK ':
        bad += 1
        if r1.returncode or r2.returncode: print('   lift:', r1.stderr.strip()[-200:], '| ref:', r2.stderr.strip()[-200:])
        if not same:
            for i, (x, y) in enumerate(zip(open(la, errors='replace'), open(lb, errors='replace'))):
                if x != y: print(f'   primeira divergencia na linha {i + 1}:\n     lift: {x.strip()}\n     ref : {y.strip()}'); break
print('RESULTADO:', 'todas as sessoes identicas' if not bad else f'{bad} sessao(oes) divergente(s)')
sys.exit(1 if bad else 0)
