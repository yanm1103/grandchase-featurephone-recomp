"""Cobertura por fuzzing no backend lifted (rapido): roda N sessoes aleatorias, une os blocos executados e lista erros fatais.
Uso: python -I cover.py <resdir> [sessoes=40] [frames=6000] [semente_inicial=100]"""
import os, sys, random, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import importlib.util
OUT = os.path.join(R, 'build', 'out'); W = os.path.join(R, 'build', 'cover'); os.makedirs(W, exist_ok=True)
res = sys.argv[1]; n = int(sys.argv[2]) if len(sys.argv) > 2 else 40; frames = int(sys.argv[3]) if len(sys.argv) > 3 else 6000; s0 = int(sys.argv[4]) if len(sys.argv) > 4 else 100
KEYS = [-1, -2, -3, -4, -5, -6, -7, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 35, 42]
def script(seed, total, fire):
    rnd = random.Random(seed); t = 200; ev = []
    W_ = [5, 5, 5, 5, fire, 3, 2] + [1] * 12
    while t < total:
        k = rnd.choices(KEYS, W_)[0]; dur = rnd.choice([40, 80, 120, 300, 600, 1500])    # 1500 ms = andar/segurar
        ev.append((t, k, 'p')); ev.append((t + dur, k, 'r')); t += rnd.choice([60, 120, 250, 500]) + dur // 3
    return sorted(ev)
union = set(); fatals = []; exits = 0; saves = set()
for i in range(n):
    seed = s0 + i; d = os.path.join(W, f's{seed}'); os.makedirs(d, exist_ok=True)
    kf = os.path.join(d, 'keys.txt')
    with open(kf, 'w') as f:
        for ms, k, a in script(seed, frames * 40, 14 if i % 2 else 6): f.write(f'{ms} {k} {a}\n')
    cov = os.path.join(d, 'cov.txt'); sv = os.path.join(d, 'save'); os.makedirs(sv, exist_ok=True)
    r = subprocess.run([os.path.join(OUT, 'gc1_headless.exe'), res, sv, '--frames', str(frames), '--keys', kf, '--cov', cov], capture_output=True, text=True)
    if r.returncode: fatals.append((seed, r.returncode, r.stderr.strip()[-160:]))
    if os.path.exists(cov): union |= set(open(cov).read().split())
    saves |= set(os.listdir(sv))
    if (i + 1) % 10 == 0: print(f'  {i+1}/{n}: uniao {len(union)} blocos ({100*len(union)/22858:.1f}%), fatais {len(fatals)}, saves {sorted(saves)}', flush=True)
open(os.path.join(W, 'union.txt'), 'w').write('\n'.join(sorted(union)))
print(f'COBERTURA: {len(union)} de 22858 blocos = {100*len(union)/22858:.1f}%  | erros fatais: {len(fatals)}')
for f in fatals[:8]: print('  ', f)
