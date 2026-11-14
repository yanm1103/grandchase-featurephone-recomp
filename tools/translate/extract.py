"""Extrai TODAS as unidades traduziveis do jar KO (fonte) + o PT atual (so referencia) para work/catalog.json.
Uso: python -I tools/translate/extract.py"""
import sys, os, json, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *; import tables, constdata, ui
HERE = os.path.dirname(os.path.abspath(__file__)); WORK = os.path.join(HERE, 'work')
cat = {}      # id -> dict
order = []    # ordem de insercao

def add(uid, **kw):
    cat[uid] = dict(id=uid, **kw); order.append(uid)

def k(b): return b.decode('cp949')
def p(b): return b.decode('latin1')

# ---- 1. falas dos scripts ---------------------------------------------------------------------------------------------
# nomes de NPC/monstro pelo indice de ator (ConstData, tabelas npc e mon) e nomes de mapa (scp_map)
ck = open(jar('ko') + 'ConstData.br', 'rb').read(); cp = open(jar('pt') + 'ConstData.br', 'rb').read()
def cd(buf, lang, off, ln): return dec(lang, buf[constdata.BLOB + off: constdata.BLOB + off + ln])
def cd_field(buf, lang, base, S, i, po, pl):
    q = base + i * S; return cd(buf, lang, struct.unpack_from('<h', buf, q + po)[0], buf[q + pl])
npc_ko = [(cd_field(ck,'ko',21875,39,i,1,3), cd_field(ck,'ko',21875,39,i,4,6)) for i in range(66)]
mon_ko = [cd_field(ck,'ko',24448,66,i,2,4) for i in range(137)]
def map_names():
    s = open(jar('ko') + 'scp_map.scp', 'rb').read(); d = open(jar('ko') + 'scp_map.dat', 'rb').read()
    out = {}
    for i in range(len(s) // 12):
        off = struct.unpack_from('<H', s, i * 12 + 2)[0]; ln = s[i * 12 + 4]
        if ln: out[i] = dec('ko', d[off:off + ln])
    return out
MAPS = map_names()
def speaker(a):
    if a == 0: return '엘리시스 (Elesis, a protagonista)'
    if a < 66: return f'{npc_ko[a][1]} ({npc_ko[a][0]})'
    if a >= 70 and a - 70 < len(mon_ko): return f'{mon_ko[a-70]} (monstro/chefe)'
    return f'ator#{a}'
for n in script_names('ko'):
    code = open(f'{jar("ko")}{n}.scp', 'rb').read(); dat = open(f'{jar("ko")}{n}.dat', 'rb').read()
    scene = ''
    num = int(n[1:])
    if n[0] == 'n': scene = f'conversa com o NPC {npc_ko[num][1]} ({npc_ko[num][0]})' if num < 66 else ''
    else: scene = 'cena no mapa: ' + MAPS.get(num, '?')
    pt_lines = lines('pt', n)
    for idx, (r, t) in enumerate(lines('ko', n)):
        d = dict(kind='dlg', ko=t, script=n, idx=idx, op=r['op'], scene=scene, old_pt=pt_lines[idx][1],
                 limit=1499 if r['op'] == 0x11 else 299)
        if r['op'] == 0x11: d['speaker'] = speaker(r['actor']); d['actor'] = r['actor']
        else: d['mode'] = r['mode']; d['speaker'] = '(fala de missao/narracao)'
        add(f'dlg|{n}|{idx}', **d)

# ---- 2. tabelas .scp/.dat -------------------------------------------------------------------------------------------------
for f in tables.SCHEMAS:
    if f == 'scp_npc': continue     # morto: o jogo le os nomes de NPC do ConstData
    sk = open(jar('ko') + f + '.scp', 'rb').read(); dk = open(jar('ko') + f + '.dat', 'rb').read()
    sp = open(jar('pt') + f + '.scp', 'rb').read(); dp = open(jar('pt') + f + '.dat', 'rb').read()
    fk = tables.fields(f, sk, len(dk)); fp = tables.fields(f, sp, len(dp))
    for a, b in zip(fk, fp):
        if a[6] == 0: continue
        add(f'tbl|{f}|{a[0]}|{a[1]}', kind='tbl', ko=dec('ko', dk[a[5]:a[5]+a[6]]), old_pt=dec('pt', dp[b[5]:b[5]+b[6]]),
            table=f, rec=a[0], field=a[1], limit=255)

# ---- 3. menus ------------------------------------------------------------------------------------------------------------
for f, lim in (('menu.tx', 9900), ('npcmenu.tx', 400)):
    kl = open(jar('ko') + f, 'rb').read().split(b'\r\n'); pl = open(jar('pt') + f, 'rb').read().split(b'\r\n')
    for i, (a, b) in enumerate(zip(kl, pl)):
        if a: add(f'menu|{f}|{i}', kind='menu', ko=dec('ko', a), old_pt=dec('pt', b), file=f, line=i, limit=1499)

# ---- 4. UI na rodata do binary.mod ------------------------------------------------------------------------------------
tk, _ = ui.load_text(jar('ko') + 'binary.mod'); tp, _ = ui.load_text(jar('pt') + 'binary.mod')
for (a, kb, cap), (a2, pb, cap2) in zip(ui.slots(tk), ui.slots(tp)):
    assert a == a2
    if any(c >= 0x80 for c in kb):                       # so o que tem coreano
        add(f'ui|{a:#x}', kind='ui', ko=dec('ko', kb), old_pt=dec('pt', pb), addr=a, limit=cap - 1)

# ---- 5. ConstData ---------------------------------------------------------------------------------------------------------
fk = constdata.fields(ck); fp = constdata.fields(cp)
assert [x[:5] for x in fk] == [x[:5] for x in fp]
npc_seen = set()
for a, b in zip(fk, fp):
    tab, i, kind = a[0], a[1], a[2]
    ko = dec('ko', ck[constdata.BLOB + a[5]: constdata.BLOB + a[5] + a[6]]); pt = dec('pt', cp[constdata.BLOB + b[5]: constdata.BLOB + b[5] + b[6]])
    add(f'cd|{tab}|{i}|{kind}|{a[3]}', kind='cd', ko=ko, old_pt=pt, table=tab, rec=i, field=kind, limit=255)

os.makedirs(WORK, exist_ok=True)
json.dump([cat[u] for u in order], open(os.path.join(WORK, 'catalog.json'), 'w', encoding='utf8'), ensure_ascii=False, indent=0)
import collections
print(collections.Counter(c['kind'] for c in cat.values()), len(cat))
