"""Acha, no decompilado, as strings de UI passadas a rotinas que quebram linha por largura; grava work/wrapped_ui.json (ids de UI a reflow)."""
import re, glob, json, os, sys
sys.stdout.reconfigure(encoding='utf-8')
HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.join(HERE, 'work')
cat = json.load(open(os.path.join(W, 'catalog.json'), encoding='utf8'))
lits = {}
for f in glob.glob(r'C:\Games\GrandChase1-decomp\decompiled\c\*.c'):
    t = open(f, encoding='utf8', errors='replace').read()
    for m in re.finditer(r'(text_draw_wrapped|draw_confirm_dialog|draw_message_box_ok)\(\s*"((?:[^"\\]|\\.)*)"', t):
        s = m.group(2).replace('\\"', '"').replace('\\\\', '\\')
        lits[s] = m.group(1)
ui = [c for c in cat if c['kind'] == 'ui']
hit = [c['id'] for c in ui if c['old_pt'] in lits]
pipe = [c['id'] for c in ui if '|' in c['ko'] or '|' in c['old_pt']]
print('literais:', len(lits), '| ui por literal:', len(hit), '| ui com pipe:', len(pipe))
json.dump(sorted(set(hit) | set(pipe)), open(os.path.join(W, 'wrapped_ui.json'), 'w'))
