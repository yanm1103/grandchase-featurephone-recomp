"""Passe de acentuacao: o texto traduzido hoje e ASCII ("voce", "nao"); o motor ja desenha 24 letras acentuadas (runtime/patch.c, charset.py).
  python -I acentos.py prepare   -> work/acentos/in/NN.json  ({id: texto}) para agentes devolverem o MESMO texto com acentos em work/acentos/out/NN.json
  python -I acentos.py check     -> confere: tirando os acentos, cada texto volta IDENTICO ao original; so as 24 letras de charset.py; lista erros
build.py aplica work/acentos/out/*.json por cima de tudo (se existir). Regras para o agente em work/acentos/PROMPT.md."""
import sys, os, json, glob, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import charset
from normalize import norm
HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.join(HERE, 'work'); A = os.path.join(W, 'acentos')


def current_texts():
    cat = json.load(open(os.path.join(W, 'catalog.json'), encoding='utf8')); by = {c['id']: c for c in cat}
    tr = {}
    for f in sorted(glob.glob(os.path.join(W, 'out', '*.json'))):
        for k, v in json.load(open(f, encoding='utf8')).items():
            if k != '_novos_termos': tr[k] = norm(v)
    tr.update({k: norm(v) for k, v in json.load(open(os.path.join(W, 'overrides.json'), encoding='utf8')).items()})
    npc = json.load(open(os.path.join(W, 'npc_pt.json'), encoding='utf8'))
    for c in cat:
        if c['kind'] == 'cd' and c['table'] == 'npc': tr[c['id']] = npc[str(c['rec'])][0 if c['field'] == 'title' else 1]
    return {k: v for k, v in tr.items() if k in by and re.search('[A-Za-z]', v)}


def prepare(n=120):
    os.makedirs(os.path.join(A, 'in'), exist_ok=True); os.makedirs(os.path.join(A, 'out'), exist_ok=True)
    t = current_texts(); ids = sorted(t)
    for i in range(0, len(ids), n):
        json.dump({k: t[k] for k in ids[i:i + n]}, open(os.path.join(A, 'in', f'{i // n + 1:02d}.json'), 'w', encoding='utf8'), ensure_ascii=False, indent=0)
    print(len(ids), 'textos em', (len(ids) + n - 1) // n, 'lotes')


def check():
    bad = 0; done = 0
    t = current_texts()
    for f in sorted(glob.glob(os.path.join(A, 'out', '*.json'))):
        for k, v in json.load(open(f, encoding='utf8')).items():
            done += 1
            if k not in t: print('id desconhecido', k); bad += 1; continue
            if charset.strip(v) != t[k]: print('MUDOU alem dos acentos:', k, repr(t[k][:70]), '->', repr(v[:70])); bad += 1
            elif any(ord(c) > 0x7e and c not in charset.TO_BYTE for c in v): print('letra nao suportada:', k, repr(v[:70])); bad += 1
    print('conferidos', done, '| erros', bad, '| faltam', len(t) - done)
    return bad


if __name__ == '__main__':
    {'prepare': prepare, 'check': check}[sys.argv[1] if len(sys.argv) > 1 else 'check']()
