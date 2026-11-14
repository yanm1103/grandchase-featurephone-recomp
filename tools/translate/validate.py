"""Valida uma traducao de lote. Uso: python -I validate.py saida.json lote.json   (sai com 1 se houver erro)"""
import sys, json, re, collections
sys.stdout.reconfigure(encoding='utf-8')
TOK = re.compile(r'%[0-9]*[dsxc]|@[1-9]')
BAD = [(r'\bLira\b', 'Lire (nunca Lira)'), (r'\bGaikoz\b', 'Gaicoz'), (r'\bCanabana\b', 'Canaban'),
       (r'(?i)\b(merda|porra|caralho|puta|foda|foder|cacete|bosta)\b', 'palavrao inexistente no original'),
       (r'\bArma\b(?= ?[,.!?])', 'Arme e nome (nao "arma")'), (r'\bTumulo\b', 'use Cemiterio dos Mortos'), (r'\bHit-Run\b', 'traducao invalida')]


def check(uid, u, pt):
    errs = []
    if not isinstance(pt, str):
        return [f'{uid}: nao e texto']
    ko = u['ko']; lim = u['limit']
    bad = [c for c in pt if ord(c) < 0x20 or ord(c) > 0x7e]
    if bad:
        errs.append(f'{uid}: caractere fora de ASCII imprimivel: {bad[:5]}')
    if len(pt) > lim:
        errs.append(f'{uid}: {len(pt)} > limite {lim}: {pt[:50]!r}')
    if ko and not pt.strip():
        errs.append(f'{uid}: vazio')
    if collections.Counter(TOK.findall(ko)) != collections.Counter(TOK.findall(pt)):
        errs.append(f'{uid}: marcadores diferentes {TOK.findall(ko)} vs {TOK.findall(pt)}')
    k = u['kind']
    if k == 'dlg':
        for ch in '|/':
            if ch in pt and ch not in ko:
                errs.append(f'{uid}: nao use "{ch}" em dialogo (o programa quebra linhas)')
    if k == 'menu':
        for ch in '@$/':
            if ko.count(ch) != pt.count(ch):
                errs.append(f'{uid}: contagem de "{ch}" difere (ko {ko.count(ch)}, pt {pt.count(ch)})')
        if ko.endswith('$') != pt.endswith('$'):
            errs.append(f'{uid}: fim "$"')
    if k in ('cd', 'tbl') and '|' in pt and '|' not in ko:
        errs.append(f'{uid}: nao use "|" aqui')
    for pat, why in BAD:
        if re.search(pat, pt):
            errs.append(f'{uid}: termo suspeito ({why}): {pt[:60]!r}')
    return errs


if __name__ == '__main__':
    out = json.load(open(sys.argv[1], encoding='utf8')); batch = json.load(open(sys.argv[2], encoding='utf8'))
    units = {u['id']: u for u in batch['units']}
    errs = []
    for uid, u in units.items():
        if uid not in out:
            errs.append(f'{uid}: FALTA'); continue
        errs += check(uid, u, out[uid])
    for uid in out:
        if uid not in units and uid != '_novos_termos':
            errs.append(f'{uid}: id desconhecido')
    print('\n'.join(errs[:80])); print(f'{len(units)} unidades, {len(errs)} problemas')
    sys.exit(1 if errs else 0)
