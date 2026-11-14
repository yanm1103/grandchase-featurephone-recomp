"""Reinsere as traducoes (work/out/*.json) e gera um jar novo (pasta) pronto para `gc1.exe --res <pasta>`.
Uso: python -I tools/translate/build.py [pasta_saida]     (padrao: build/out/game_ptbr)
Base de arquivos nao-textuais: jar PT (identico ao KO fora dos textos). Verifica por releitura: cada texto gravado tem de voltar igual."""
import sys, os, json, struct, shutil, re, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *
import scp, tables, constdata, ui
from normalize import norm, ui_ok

HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.join(HERE, 'work')
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'out', 'game_ptbr')
cat = json.load(open(os.path.join(W, 'catalog.json'), encoding='utf8')); by_id = {c['id']: c for c in cat}

# ---------------- traducoes ----------------
tr = {}
for f in sorted(glob.glob(os.path.join(W, 'out', '*.json'))):
    d = json.load(open(f, encoding='utf8'))
    for k_, v in d.items():
        if k_ != '_novos_termos': tr[k_] = norm(v)
tr.update({k_: norm(v) for k_, v in json.load(open(os.path.join(W, 'overrides.json'), encoding='utf8')).items()})
for f_ in sorted(glob.glob(os.path.join(W, 'acentos', 'out', '*.json'))):      # passe de acentuacao (acentos.py): por cima de tudo
    tr.update(json.load(open(f_, encoding='utf8')))
if os.environ.get('GC1_EXTRA'):      # texto extra (ex.: pilotos de acentuacao) por cima de tudo
    tr.update(json.load(open(os.environ['GC1_EXTRA'], encoding='utf8')))
import grind
grind_changed = grind.edit_texts(tr)           # contagens das missoes de matar/coletar reduzidas (descricoes e falas); a parte binaria vem no fim
npc = json.load(open(os.path.join(W, 'npc_pt.json'), encoding='utf8'))
for c in cat:
    if c['kind'] == 'cd' and c['table'] == 'npc': tr[c['id']] = npc[str(c['rec'])][0 if c['field'] == 'title' else 1]
missing = collections.Counter()
def T(uid):
    if uid in tr: return tr[uid]
    missing[by_id[uid]['kind']] += 1
    return by_id[uid]['old_pt']

# ---------------- quebra de linha por largura (modelo do motor: alnum 6, espaco 4, resto 7, '@N' = 0) ----------------
def wid(s):
    w = 0; i = 0
    while i < len(s):
        c = s[i]
        if c == '@' and i + 1 < len(s) and s[i + 1] in '123456789': i += 2; continue
        w += 6 if c.isalnum() else (4 if c == ' ' else 7); i += 1
    return w
def wrap(text, width, brk):
    out = []
    for seg in text.split('|') if brk == '|' else [text]:
        words = [x for x in re.sub(r' +', ' ', seg).strip().split(' ') if x != '']
        lines = []; cur = ''; cw = 0
        for wd in words:
            ww = wid(wd)
            if cur and cw + 4 + ww > width: lines.append(cur); cur, cw = wd, ww
            else: cur, cw = (cur + ' ' + wd if cur else wd), cw + (4 if cur else 0) + ww
        if cur or not lines: lines.append(cur)
        out.append(brk.join(lines))
    return '|'.join(out) if brk == '|' else out[0]
DLG_W, DESC_W, QUEST_W = 182, 145, 130
WRAPPED_UI = set(json.load(open(os.path.join(W, 'wrapped_ui.json'), encoding='utf8')))
UI_W = 112
SCROLL_IDS = {'dlg|m9|0', 'dlg|m124|3'}
def final_text(uid):
    c = by_id[uid]; t = T(uid); k = c['kind']
    if k == 'dlg' and (uid in SCROLL_IDS or '|' in c['ko']): return wrap(t, 140, '|')   # texto rolante (largura 145 px) e poemas/cartas/creditos
    if k == 'dlg' and c['op'] == 0x11: return wrap(t, DLG_W, '/')
    if k == 'cd' and c['field'] == 'desc': return wrap(t, DESC_W, '|')
    if k == 'tbl' and c['table'] == 'scp_quest' and c['field'] == 1: return wrap(t, QUEST_W, '|')
    if k == 'ui' and uid in WRAPPED_UI: return wrap(t, UI_W, '|')
    if k == 'menu' and c['file'] == 'menu.tx' and '/' in t and '@' not in t:
        title, body = t.split('/', 1); return title + '/' + wrap(body, 112 if c['line'] in (40, 41, 42) else DESC_W, '|')
    return t
expected = {c['id']: final_text(c['id']) for c in cat if not (c['kind'] == 'ui' and not ui_ok(c))}
import charset
enc = charset.enc

# ---------------- reconstrucao ----------------
if os.path.exists(OUT): shutil.rmtree(OUT)
shutil.copytree(jar('pt'), OUT)
def wr(name, data): open(os.path.join(OUT, name), 'wb').write(data)
def rd(lang, name): return open(jar(lang) + name, 'rb').read()

# 1. scripts de dialogo
for n in script_names('ko'):
    code = bytearray(rd('ko', n + '.scp')); dat = rd('ko', n + '.dat'); refs = scp.text_refs(code)
    spans = sorted((r['off'], r['off'] + r['len'], i) for i, r in enumerate(refs) if r['len'] > 0)
    new = bytearray(); pos = 0
    for a, b, i in spans:
        if a > pos: new += dat[pos:a]
        txt = enc(expected[f'dlg|{n}|{i}']); r = refs[i]
        assert len(txt) < 0x7fff
        code[r['fo']:r['fo'] + 2] = struct.pack('<h', len(new)); code[r['fl']:r['fl'] + 2] = struct.pack('<h', len(txt)); new += txt; pos = b
    new += dat[pos:]
    # refs de texto vazio: aponta para o inicio (len 0)
    for i, r in enumerate(refs):
        if r['len'] == 0: code[r['fo']:r['fo'] + 2] = struct.pack('<h', 0)
    wr(n + '.scp', bytes(code)); wr(n + '.dat', bytes(new))

# 2. tabelas .scp/.dat
for f in tables.SCHEMAS:
    if f == 'scp_npc': continue
    s = bytearray(rd('ko', f + '.scp')); d = rd('ko', f + '.dat'); fl = tables.fields(f, s, len(d))
    new = bytearray()
    for r, fi, po, pl, w, off, ln in sorted(fl, key=lambda x: x[5]):
        if ln == 0: continue
        txt = enc(expected[f'tbl|{f}|{r}|{fi}'])
        s[po:po + 2] = struct.pack('<H', len(new))
        if w == 1: assert len(txt) < 256; s[pl] = len(txt)
        else: s[pl:pl + 2] = struct.pack('<H', len(txt))
        new += txt
    for r, fi, po, pl, w, off, ln in fl:
        if ln == 0: s[po:po + 2] = struct.pack('<H', 0)
    wr(f + '.scp', bytes(s)); wr(f + '.dat', bytes(new))

# 3. menus
for f in ('menu.tx', 'npcmenu.tx'):
    kl = rd('ko', f).split(b'\r\n'); out = []
    for i, a in enumerate(kl): out.append(enc(expected[f'menu|{f}|{i}']) if a else b'')
    data = b'\r\n'.join(out); assert len(data) < 9990, (f, len(data)); wr(f, data)

# 4. ConstData.br
ck = bytearray(rd('ko', 'ConstData.br')); fl = constdata.fields(ck); new = bytearray()
for name, i, kind, po, pl, off, ln in sorted(fl, key=lambda x: x[5]):
    uid = f'cd|{name}|{i}|{kind}|{po}'; txt = enc(expected[uid])
    assert len(txt) < 256 and len(new) + len(txt) < 0x7fff, (uid, len(new))
    ck[po:po + 2] = struct.pack('<h', len(new)); ck[pl] = len(txt); new += txt
wr('ConstData.br', bytes(ck[:constdata.BLOB]) + bytes(new))
print('ConstData blob', len(new), 'bytes (limite 32767)')

# 5. strings de UI no binary.mod (.text rodata)
from elftools.elf.elffile import ELFFile
mod = bytearray(rd('pt', 'binary.mod'))
with open(jar('pt') + 'binary.mod', 'rb') as f_:
    sec = ELFFile(f_).get_section_by_name('.text'); soff = sec['sh_offset']
text = bytes(mod[soff:soff + sec['sh_size']])
for a, sb, cap in ui.slots(bytearray(text)):
    uid = f'ui|{a:#x}'
    if uid not in expected: continue
    txt = enc(expected[uid]); assert len(txt) + 1 <= cap, (uid, len(txt), cap)
    p = soff + a - ui.TEXT_BASE; mod[p:p + cap] = txt + b'\0' * (cap - len(txt))
wr('binary.mod', bytes(mod))

# 6. grind das missoes: contagens na tabela e nos scripts de entrega (os textos ja foram trocados em grind.edit_texts)
print('grind: %d textos alterados; registros/remocoes de item: %s' % (len(grind_changed), grind.patch_binary(OUT)))
import dennis
print('dennis: n56.scp vira loja; %d precos fixados em ConstData' % dennis.patch_binary(OUT))

# ---------------- verificacao por releitura ----------------
def reread():
    got = {}
    for n in script_names('ko'):
        code = open(f'{OUT}/{n}.scp', 'rb').read(); dat = open(f'{OUT}/{n}.dat', 'rb').read()
        for i, r in enumerate(scp.text_refs(code)):
            got[f'dlg|{n}|{i}'] = charset.dec(dat[r['off']:r['off'] + r['len']]) if r['len'] else ''
    for f in tables.SCHEMAS:
        if f == 'scp_npc': continue
        s = open(f'{OUT}/{f}.scp', 'rb').read(); d = open(f'{OUT}/{f}.dat', 'rb').read()
        for r, fi, po, pl, w, off, ln in tables.fields(f, s, len(d)):
            if ln: got[f'tbl|{f}|{r}|{fi}'] = charset.dec(d[off:off + ln])
    for f in ('menu.tx', 'npcmenu.tx'):
        for i, a in enumerate(open(f'{OUT}/{f}', 'rb').read().split(b'\r\n')):
            if a: got[f'menu|{f}|{i}'] = charset.dec(a)
    d = open(f'{OUT}/ConstData.br', 'rb').read()
    for name, i, kind, po, pl, off, ln in constdata.fields(d):
        got[f'cd|{name}|{i}|{kind}|{po}'] = charset.dec(d[constdata.BLOB + off:constdata.BLOB + off + ln])
    t2, _ = ui.load_text(f'{OUT}/binary.mod')
    for a, sb, cap in ui.slots(t2): got[f'ui|{a:#x}'] = charset.dec(sb)
    return got
got = reread(); bad = [u for u in expected if u in got and got[u] != expected[u]]
nomatch = [u for u in expected if by_id[u]['kind'] != 'ui' and u not in got and expected[u] != '']
print('verificacao: unidades', len(expected), '| divergentes', len(bad), '| ausentes', len(nomatch))
for u in (bad + nomatch)[:10]: print('  ', u, repr(expected[u][:60]), repr(got.get(u, '?')[:60]))
# codigo intocado?
def code_part(path):
    with open(path, 'rb') as f_: t = ELFFile(f_).get_section_by_name('.text').data()
    return t[:0x40fc8 - 0x1000]
assert code_part(f'{OUT}/binary.mod') == code_part(jar('pt') + 'binary.mod'), 'codigo alterado!'
print('codigo (.text < 0x40fc8) intacto. fallbacks para PT antigo (sem traducao):', dict(missing))
print('saida:', OUT)
sys.exit(1 if bad or nomatch else 0)
