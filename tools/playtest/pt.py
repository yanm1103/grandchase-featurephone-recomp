"""pt.py - teste automatico do jogo sem jogar na mao.
Uso:  python -I tools/playtest/pt.py <cenario.txt> [--res pasta] [--frames N] [
O cenario e um roteiro de acoes (uma por linha, '#' comenta); o tempo corre em ms virtuais:
    boot                     # atalho: teclas que levam do titulo ate o gameplay (novo jogo, mapa 9) -- ~15,6 s
    wait 500                 # deixa o jogo rodar 500 ms
    press right | release right | tap ok 80 | hold right 1000     (nomes: up down left right ok soft1 soft2 clear 0-9 * #)
    shot nome                # dump de um quadro (png) nesse instante
    expect x> 130 | expect mode == 2 ...   # confere o estado no instante (campos: x y dir st act mv run dead bgm mode map)
Gera <out>/estado.txt (uma linha por quadro/tecla) e imprime um resumo: trajetoria, expectativas e falhas."""
import sys, os, re, subprocess, argparse, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(os.path.dirname(HERE))
KEYS = {'up': -1, 'down': -2, 'left': -3, 'right': -4, 'ok': -5, 'soft1': -6, 'soft2': -7, 'clear': -16, 'menu': -10,
        '*': 42, '#': 35, **{str(i): 48 + i for i in range(10)}}
BOOT = os.path.join(HERE, 'boot_keys.txt')

STATES = os.path.join(ROOT, 'build', 'pt', 'states')
def state_path(name): os.makedirs(STATES, exist_ok=True); return os.path.join(STATES, name + '.snap').replace(os.sep, '/')

def parse(path):
    fromstate = None; tboot = 0; marks = []; counts = []; ev = []; shots = []; expects = []; t = 0
    for ln, raw in enumerate(open(path, encoding='utf8'), 1):
        s = raw.split('#')[0].strip() if not raw.strip().startswith('#') else ''
        if raw.strip().startswith('#') or not raw.strip(): continue
        s = re.sub(r'\s+#.*$', '', raw.strip()) if ' #' in raw else raw.strip()
        a = s.split()
        c = a[0]
        if c == 'boot':
            for l in open(BOOT): p = l.split(); ev.append((int(p[0]), int(p[1]), p[2]))
            t = int(p[0]) + 1000; tboot = t
        elif c == 'from': fromstate = state_path(a[1])      # from <estado>: comeca de um save state em vez de `boot` (instantaneo)
        elif c == 'savestate': ev.append((t, 'S', state_path(a[1])))
        elif c == 'loadstate': ev.append((t, 'L', state_path(a[1])))
        elif c == 'wait': t += int(a[1])
        elif c in ('press', 'release'): ev.append((t, KEYS[a[1]], 'p' if c == 'press' else 'r'))
        elif c == 'tap': ev.append((t, KEYS[a[1]], 'p')); t += int(a[2]) if len(a) > 2 else 80; ev.append((t, KEYS[a[1]], 'r'))
        elif c == 'hold': ev.append((t, KEYS[a[1]], 'p')); t += int(a[2]); ev.append((t, KEYS[a[1]], 'r'))
        elif c == 'poke': ev.append((t, 'P', f'{int(a[1], 0):x} {int(a[2])} {int(a[3], 0)}'))     # poke <endereco> <bytes> <valor>
        elif c == 'call': ev.append((t, 'C', f'{int(a[1], 0):x} {int(a[2], 0)} {int(a[3], 0) if len(a) > 3 else 0}'))     # call <endereco_thumb> <a0> [a1]: chama uma funcao do jogo
        elif c == 'teleport':      # teleport <mapa> <x> <y> <dir>: pede a transicao de mapa do proprio jogo
            for ad, sz, v in ((0x162c442, 1, a[1]), (0x162c444, 2, a[2]), (0x162c446, 2, a[3]), (0x162c448, 1, a[4]), (0x162c440, 1, 1)): ev.append((t, 'P', f'{ad:x} {sz} {int(str(v), 0)}'))
            t += 400
        elif c == 'mark': marks.append(t)
        elif c == 'count': counts.append((marks[-1] if marks else 0, t, a[1], a[2], int(a[3]), ln))     # count music_on|music_off|sfx <op> <n>  (desde o ultimo 'mark')
        elif c == 'shot': shots.append((t, a[1]))
        elif c == 'expect': expects.append((t, ' '.join(a[1:]), ln))
        else: raise SystemExit(f'{path}:{ln}: comando desconhecido {c}')
    return sorted(ev, key=lambda e: e[0]), shots, expects + [('count',) + c for c in counts], t, tboot, fromstate

def state_at(rows, t):
    last = None
    for r in rows:
        if r['t'] >= t: break
        last = r
    return last

def load_state(path):
    rows = []
    for l in open(path):
        p = l.split(); d = {'t': int(p[0][2:]), 'why': p[1]}
        if p[1] in ('A', 'M'): d['audio'] = p[1] + ' ' + ' '.join(p[2:]); rows.append(d); continue
        for kv in p[2:]:
            if '=' in kv: k, v = kv.split('=', 1); d[k] = int(v) if v.lstrip('-').isdigit() else v
        rows.append(d)
    return rows

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('cenario'); ap.add_argument('--res', default=os.path.join(ROOT, 'build/out/game_ptbr'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'build/pt/out')); ap.add_argument('--exe', default=os.path.join(ROOT, 'build/out/gc1_headless.exe'))
    ap.add_argument('--raw', action='store_true', help='entrada crua (sem a camada inteligente), para reproduzir o bug')
    a = ap.parse_args()
    ev, shots, expects, tend, tboot, fromstate = parse(a.cenario)
    os.makedirs(a.out, exist_ok=True); sv = os.path.join(a.out, 'save'); shutil.rmtree(sv, ignore_errors=True); os.makedirs(sv)
    for f in os.listdir(a.out):
        if f.endswith('.ppm') or f.endswith('.png'): os.remove(os.path.join(a.out, f))
    kf = os.path.join(a.out, 'teclas.txt'); open(kf, 'w').write(''.join(f'{t} {k} {x}\n' for t, k, x in ev))
    # o headless roda por numero de quadros; 25 ms/quadro (40 fps) -> margem generosa, para quando acabar as teclas + espera
    frames = (tend + 1500) // 25 + 50
    cmd = [a.exe, a.res, sv, '--frames', str(frames), '--keys', kf, '--state', os.path.join(a.out, 'estado.txt'), '--dump', os.path.join(a.out, 'q_'), '--dump-every', '0'] + ([] if a.raw else ['--smart-from', str(tboot)]) + (['--load', fromstate] if fromstate else [])
    if shots: cmd += ['--shots', ','.join(str(t) for t, _ in shots)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    dbg = [l for l in r.stderr.splitlines() if l.startswith('DIAGDBG')]
    if dbg: print(f'  DIAGDBG: {len(dbg)} quadros com o jogador em cima de parede; primeiro: {dbg[0]}')
    for l in r.stderr.splitlines():
        if l.startswith('SHOPDBG'): print('  '+l)      # GC1_SHOPDBG=1: estoque das lojas (runtime/patch.c)
    print(r.stderr.strip().splitlines()[-1] if r.stderr.strip() else r.stdout)
    for t, nm in shots:
        src = os.path.join(a.out, f'q_t{t:07d}.ppm')
        if os.path.exists(src): subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools/translate/ppm2png.py'), src, os.path.join(a.out, nm + '.png'), '3']); os.remove(src)
    rows = load_state(os.path.join(a.out, 'estado.txt')); fails = 0
    for item in expects:
        if item[0] == 'count':
            _, t0, t1, kind, op, n, ln = item; kinds = {'music_on': 'M music on', 'music_off': 'M music off', 'sfx': 'M sfx'}
            v = sum(1 for r in rows if 'audio' in r and t0 <= r['t'] <= t1 and r['audio'].startswith(kinds[kind]))
            ok = eval(f'{v}{op}{n}'); print(('ok   ' if ok else 'FALHA'), f'contagem {kind} em [{t0},{t1}] {op} {n} (valor={v})', f'linha {ln}'); fails += not ok; continue
        t, e, ln = item
        if e.startswith('online '):      # expect online won|medals|coupons <op> <n>: confere o <saves>/online.sav do servidor falso (runtime/netsim.h) ao fim da execucao
            _, campo, op, n = e.split(); import struct
            try: d = open(os.path.join(sv, 'online.sav'), 'rb').read(); v = dict(zip(('won', 'medals', 'coupons'), (struct.unpack('<5I', d[:20])[1], struct.unpack('<5I', d[:20])[2], struct.unpack('<5I', d[:20])[3])))[campo]
            except OSError: v = 0
            ok = eval(f'{v}{op}{n}'); print(('ok   ' if ok else 'FALHA'), 'online', campo, op, n, f'(valor={v})', f'linha {ln}'); fails += not ok; continue
        s = state_at(rows, t); m = re.match(r'(\w+)\s*(==|!=|>=|<=|>|<)\s*(-?\d+)', e)
        v = s.get(m.group(1)) if s else None; ok = v is not None and eval(f'{v}{m.group(2)}{m.group(3)}')
        print(('ok   ' if ok else 'FALHA'), f't={t}', e, f'(valor={v})', f'linha {ln}'); fails += not ok
    print('trajetoria (t, x, y, dir, mv, act):')
    prev = None
    for r in rows:
        if 'audio' in r: print(f"  t={r['t']:6d} AUDIO   {r['audio']}"); continue
        if 'x' not in r: continue
        k = (r['x'], r['y'], r['mv'], r['act'], r['dir'])
        if k != prev and (r['why'] != 'F' or prev is None or True): print(f"  t={r['t']:6d} {r['why']:7s} x={r['x']} y={r['y']} dir={r['dir']} mv={r['mv']} run={r['run']} act={r['act']} st={r['st']} bgm={r['bgm']}"); prev = k
    sys.exit(1 if fails else 0)
main()
