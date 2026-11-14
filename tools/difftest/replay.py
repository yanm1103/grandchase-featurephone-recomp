"""Repete um roteiro de teclas (gravado com `gc1.exe --record`) nos dois backends e compara traces, saves e cobertura.
Uso: python -I replay.py <resdir> <roteiro.txt> [frames=20000]"""
import os, sys, subprocess, filecmp, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(R, 'build', 'out'); W = os.path.join(R, 'build', 'replay'); shutil.rmtree(W, ignore_errors=True); os.makedirs(W)
res, keys = sys.argv[1], os.path.abspath(sys.argv[2]); frames = sys.argv[3] if len(sys.argv) > 3 else '20000'
for n in ('save_lift', 'save_ref'): os.makedirs(os.path.join(W, n))
a = subprocess.run([os.path.join(OUT, 'gc1_headless.exe'), res, os.path.join(W, 'save_lift'), '--frames', frames, '--trace', os.path.join(W, 'lift.txt'), '--keys', keys, '--cov', os.path.join(W, 'cov.txt')], capture_output=True, text=True)
print('lifted:', a.stderr.strip().splitlines()[-2:] if a.stderr else '', 'rc', a.returncode)
b = subprocess.run([os.path.join(OUT, 'gc1_ref_headless.exe'), res, os.path.join(W, 'save_ref'), '--frames', frames, '--trace', os.path.join(W, 'ref.txt'), '--keys', keys], capture_output=True, text=True)
print('unicorn:', b.stderr.strip().splitlines()[-1:] if b.stderr else '', 'rc', b.returncode)
la, lb = os.path.join(W, 'lift.txt'), os.path.join(W, 'ref.txt')
same = filecmp.cmp(la, lb, shallow=False)
sa, sb = os.path.join(W, 'save_lift'), os.path.join(W, 'save_ref')
saves = sorted(os.listdir(sa)) == sorted(os.listdir(sb)) and all(filecmp.cmp(os.path.join(sa, f), os.path.join(sb, f), shallow=False) for f in os.listdir(sa))
print('traces identicos:', same, '| saves identicos:', saves, sorted(os.listdir(sa)))
if not same:
    for i, (x, y) in enumerate(zip(open(la, errors='replace'), open(lb, errors='replace'))):
        if x != y: print(f'primeira divergencia na linha {i+1}:\n  lift: {x.strip()}\n  ref : {y.strip()}'); break
sys.exit(0 if same and saves else 1)
