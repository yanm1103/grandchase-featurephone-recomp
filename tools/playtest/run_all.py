"""run_all.py - roda todos os cenarios de tools/playtest/*.txt (exceto tp.txt e boot_keys.txt) e resume. Uso: python -I tools/playtest/run_all.py"""
import glob, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__)); bad = 0
root = os.path.dirname(os.path.dirname(here))
if not os.path.exists(os.path.join(root, 'build', 'pt', 'states', 'torre1.snap')):     # save states usados por 'from <nome>'
    subprocess.run([sys.executable, '-I', os.path.join(here, 'pt.py'), os.path.join(here, 'criar_estados.txt')], capture_output=True)
for f in sorted(glob.glob(os.path.join(here, '*.txt'))):
    n = os.path.basename(f)
    if n in ('boot_keys.txt', 'tp.txt') or n.startswith('cenarios_') or n == 'criar_estados.txt': continue
    r = subprocess.run([sys.executable, '-I', os.path.join(here, 'pt.py'), f], capture_output=True, text=True)
    res = [l for l in r.stdout.splitlines() if l.startswith(('ok  ', 'FALHA'))]
    print(('PASSOU ' if r.returncode == 0 else 'FALHOU ') + n + f'  ({len(res)} verificacoes)')
    for l in res:
        if l.startswith('FALHA'): print('    ' + l)
    bad += r.returncode != 0
sys.exit(1 if bad else 0)
