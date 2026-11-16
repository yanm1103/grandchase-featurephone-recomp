"""Auditoria de tamanho: compara o tamanho (em bytes) de cada texto PT com o maior texto coreano da mesma tabela.
Os buffers do jogo (nomes de monstro, NPC, item...) foram dimensionados para o coreano; um PT maior pode pisar em variaveis vizinhas (ver runtime/patch.c, hook_monster_name).
Uso: python -I tools/translate/audit_len.py [saida_temporaria]   (roda build.py numa pasta temporaria e lista o que passa do maior coreano do grupo)"""
import sys, os, tempfile, collections
HERE = os.path.dirname(os.path.abspath(__file__))
tmp = sys.argv[1] if len(sys.argv) > 1 else os.path.join(tempfile.gettempdir(), 'gc1_audit_out')
sys.argv = [os.path.join(HERE, 'build.py'), tmp]
g = {'__name__': 'build', '__file__': os.path.join(HERE, 'build.py')}
try: exec(compile(open(g['__file__'], encoding='utf8').read(), g['__file__'], 'exec'), g)       # build.py termina com sys.exit
except SystemExit: pass
cat, expected = g['cat'], g['expected']
def klen(s): return len(s.replace('|', '\n').encode('cp949', 'replace'))
def grp(c): return (c['kind'], c.get('table'), c.get('field'), c.get('file'), c.get('op'))
res = collections.defaultdict(list)
for c in cat:
    if c['id'] not in expected: continue
    t = expected[c['id']]; res[grp(c)].append((len(t), klen(c['ko']), c['id'], t, c['ko']))
print('\n%-44s %5s %5s %5s' % ('grupo', 'n', 'maxKO', 'maxPT'))
for k, v in sorted(res.items(), key=lambda x: str(x[0])):
    mk, mp = max(x[1] for x in v), max(x[0] for x in v)
    over = [x for x in v if x[0] > mk]
    print('%-44s %5d %5d %5d  %s' % ('/'.join(str(a) for a in k if a is not None), len(v), mk, mp, ('<-- %d acima do maior KO' % len(over)) if over else ''))
    for x in sorted(over, reverse=True)[:12]: print('      %3d (KO %3d) %s: %s' % (x[0], x[1], x[2], x[3][:70].replace('\n', '/')))

# ---- menu.tx / npcmenu.tx: titulo ('/') cabe em 30 bytes (menu_title_text, 0x162d3fc) e cada opcao ('@') em 30 (menu_option_strings), terminador incluso ----
print('\n--- menu: titulo/opcao >= 30 bytes (estoura) ---')
for c in cat:
    if c['kind'] != 'menu' or c['id'] not in expected: continue
    t = expected[c['id']].rstrip('$'); parts = t.split('/', 1)
    title = parts[0] if len(parts) > 1 else ''
    body = parts[1] if len(parts) > 1 else parts[0]
    opts = [o for o in (body.split('@')[:-1] if '@' in body else [])]
    for what, s in [('titulo', title)] + [('opcao', o) for o in opts]:
        if len(s.encode('latin1', 'replace')) >= 30: print('  %s %s (%d): %s' % (c['id'], what, len(s), s))

# ---- nomes de item: "%s%s" (prefixo "+N " + nome) em buffer de pilha de 32 bytes em varias telas (inventario, venda, melhoria...): nome <= 27 ----
LIM_ITEM = 27
print('\n--- nomes de item (eq/heal/util/misc/rune/pet) acima de %d bytes ---' % LIM_ITEM)
for c in cat:
    if c['kind'] == 'cd' and c['field'] == 'name' and c['table'] in ('eq', 'heal', 'util', 'misc', 'rune', 'pet') and c['id'] in expected:
        t = expected[c['id']]
        if len(t) > LIM_ITEM: print('  %s (%d): %s' % (c['id'], len(t), t))

# ---- textos de interface com formato (%d/%s): saem em buffers de pilha pequenos (20/32/40/100 bytes); lista os que ficaram maiores que o coreano ----
print('\n--- ui com %% maiores que o KO ---')
for c in cat:
    if c['kind'] == 'ui' and c['id'] in expected and '%' in expected[c['id']]:
        t = expected[c['id']]; k = klen(c['ko'])
        if len(t) > k: print('  %s PT %d KO %d: %r | %r' % (c['id'], len(t), k, t, c['ko'] if c['ko'].isascii() else '(ko)'))

# ---- "Local: %s %s %s %s" (4 nomes de mapa de ate 50 bytes cada) sai em buffer de 100 bytes (1a000.c, draw quest detail) ----
ml = sorted((len(expected[c['id']]) for c in cat if c['kind'] == 'tbl' and c['table'] == 'scp_map' and c['id'] in expected), reverse=True)
print('\n--- Local: 4 maiores nomes de mapa: %s -> soma %d + 10 (limite 99) ---' % (ml[:4], sum(ml[:4])))
print('mapas > 22 bytes:', sum(1 for x in ml if x > 22), 'de', len(ml))
for c in cat:
    if c['kind'] == 'tbl' and c['table'] == 'scp_map' and c['id'] in expected and len(expected[c['id']]) > 22: print('   ', c['id'], len(expected[c['id']]), expected[c['id']])

print('\n--- dlg op 26 (opcao de escolha do script -> menu_option_strings, 30 bytes) >= 30 ---')
for c in cat:
    if c['kind'] == 'dlg' and c.get('op') == 26 and c['id'] in expected and len(expected[c['id']]) >= 30: print('  %s (%d): %r  KO(%d)' % (c['id'], len(expected[c['id']]), expected[c['id']], klen(c['ko'])))
