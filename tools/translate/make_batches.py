import sys, os, json, collections
HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.join(HERE, 'work')
cat = json.load(open(os.path.join(W, 'catalog.json'), encoding='utf8'))
cat_by = {c['id']: c for c in cat}
LIM = {('eq', 'name'): 36, ('mon', 'name'): 28, ('pet', 'name'): 12, ('rune', 'name'): 26, ('heal', 'name'): 26, ('heal', 'desc'): 60,
       ('util', 'name'): 40, ('util', 'desc'): 72, ('misc', 'name'): 30, ('misc', 'desc'): 70, ('skill', 'name'): 26, ('skill', 'desc'): 150}


def unit(c):
    u = dict(id=c['id'], kind=c['kind'], ko=c['ko'], limit=c['limit'])
    if c['kind'] == 'dlg':
        u['speaker'] = c['speaker']; u['scene'] = c['scene']; u['script'] = f"{c['script']}#{c['idx']}"
        if c['op'] == 0x1a:
            u['limit'] = 40 if c['mode'] == 1 else 90
            u['nota'] = 'popup curto/titulo de cena/creditos; ' + ('mode1: aparece apos o nome da missao' if c['mode'] == 1 else 'texto de cena')
        else:
            u['limit'] = 900
    elif c['kind'] == 'cd':
        u['limit'] = LIM[(c['table'], c['field'])]; u['categoria'] = f"{c['table']}.{c['field']}"
    elif c['kind'] == 'tbl':
        t, f = c['table'], c['field']
        u['limit'] = {'scp_map': 36, 'scp_mix': 24, 'tips': 255}.get(t, 44 if f == 0 else 150); u['categoria'] = f'{t}.{f}'
        if t == 'scp_quest':
            u['nota'] = 'nome da missao' if f == 0 else 'descricao/objetivo da missao (ex.: "Fale com X", "Derrote N monstros")'
        if t == 'scp_mix':
            u['nota'] = 'nome de receita de criacao (aparece como "Receita <nome>")'
        if t == 'tips':
            u['nota'] = 'dica que rola na tela de carregamento'
    elif c['kind'] == 'menu':
        u['limit'] = len(c['old_pt']) + 60; u['ref_estrutura_pt_antigo'] = c['old_pt'][:200]
    elif c['kind'] == 'ui':
        u['ref_pt_antigo_so_comprimento'] = c['old_pt']
    return u


def write(name, ids, extra=''):
    units = [unit(cat_by[i]) for i in ids]
    json.dump(dict(batch=name, nota=extra, units=units), open(os.path.join(W, 'batches', name + '.json'), 'w', encoding='utf8'), ensure_ascii=False, indent=1)
    print(name, len(units), sum(len(u['ko']) for u in units), 'chars KO')


os.makedirs(os.path.join(W, 'batches'), exist_ok=True); os.makedirs(os.path.join(W, 'out'), exist_ok=True)
by_script = collections.OrderedDict()
for c in cat:
    if c['kind'] == 'dlg':
        by_script.setdefault(c['script'], []).append(c['id'])
scripts = sorted(by_script, key=lambda n: (0 if n[0] == 'n' else 1, int(n[1:])))
cur, curn, bi = [], 0, 1


def flush():
    global cur, curn, bi
    if cur:
        write(f'dlg{bi:02d}', [i for s in cur for i in by_script[s]], 'Scripts: ' + ', '.join(cur)); bi += 1
    cur, curn = [], 0


for s in scripts:
    n = len(by_script[s])
    if curn + n > 170 or (cur and cur[0][0] != s[0]):
        flush()
    cur.append(s); curn += n
flush()
q = [c['id'] for c in cat if c['kind'] == 'tbl' and c['table'] == 'scp_quest']
q.sort(key=lambda i: (int(i.split('|')[2]), int(i.split('|')[3])))
write('quest1', q[:180], 'Missoes: pares (nome, descricao) em ordem de id; primeiras 90 missoes (principais)')
write('quest2', q[180:], 'Missoes: restante (secundarias)')
write('maps_mix_tips', [c['id'] for c in cat if c['kind'] == 'tbl' and c['table'] in ('scp_map', 'scp_mix', 'tips')], 'Nomes de mapa, receitas, dicas')
m = [c['id'] for c in cat if c['kind'] == 'menu']
write('menu1', m[:30], 'menu.tx linhas 0..29: estrutura Titulo/corpo$ ou opcoes@..@$')
write('menu2', m[30:], 'menu.tx restante + npcmenu.tx')
u = [c['id'] for c in cat if c['kind'] == 'ui']; n = (len(u) + 3) // 4
for i in range(4):
    write(f'ui{i+1}', u[i * n:(i + 1) * n], 'Textos de interface (ordem de endereco: vizinhos costumam ser da mesma tela)')


def cd(tabs):
    return [c['id'] for c in cat if c['kind'] == 'cd' and c['table'] in tabs]


write('cd_eq', cd(['eq']), 'Nomes de equipamentos/acessorios (cosmeticos: oculos, chapeus, mascaras, armas, armaduras)')
write('cd_mon', cd(['mon', 'pet', 'rune']), 'Nomes de monstros (e chefes), pets e runas')
write('cd_skill', cd(['skill']), 'Habilidades das 3 classes da Elesis (Cavaleiro, Lanceiro, Salvador), 20 por classe, nome e descricao')
write('cd_items1', cd(['heal', 'util']), 'Pocoes e itens uteis: nome e descricao')
write('cd_items2', cd(['misc']), 'Materiais/itens diversos: nome e descricao')
