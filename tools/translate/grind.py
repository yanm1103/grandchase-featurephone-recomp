"""Reduz o grind das missoes de matar/coletar (contador acima de 3) para ~1/3: scale(n) = max(3, ceil(n/3)).
Onde o numero aparece:
  1. scp_quest.scp, byte +0x0f do registro de 37 bytes (quantidade exigida; tipo 0 = matar, 1 = ter N itens no inventario)
  2. scripts de entrega: opcode 0x22 (u32 item, u8 quantidade) remove os itens quando a missao e entregue
  3. texto: descricao da missao (scp_quest campo 1) e as falas dos scripts que mencionam a missao (so numeros iguais a contagem de alguma
     missao daquele script; revisados a mao)
Nao mexe: missoes de combinar item (Onyx, Missao Importante?), cujo custo vem da receita de combinacao no codigo do jogo."""
import re, struct, os
import scp
from common import jar, script_names

def scale(n): return n if n <= 3 else max(3, -(-n // 3))
WORD_UIDS = ('dlg|n23|14', 'dlg|n23|24', 'dlg|n23|25')            # numeros por extenso nas falas do n23: Vinte -> Sete, Dez -> Quatro
WORDS = (('Vinte', 'Sete'), ('vinte', 'sete'), ('Dez', 'Quatro'), ('dez', 'quatro'))

def quests():
    """-> {registro: (tipo, contagem, item_ou_monstro)} das missoes afetadas, do scp_quest.scp original (KO)"""
    q = open(jar('ko') + 'scp_quest.scp', 'rb').read(); out = {}
    for r in range(len(q) // 37):
        rec = q[r * 37:(r + 1) * 37]
        if rec[12] in (0, 1) and rec[15] > 3: out[r] = (rec[12], rec[15], struct.unpack_from('<H', rec, 13)[0])
    return out

def _quest_of(arg):                                                # u32 dos opcodes 0..5: grupo 0x1e = principal (idx-1), senao secundaria (idx+79)
    g = arg >> 24; idx = (arg >> 8) & 0xffff
    return idx - 1 if g == 0x1e else idx + 79

def _sub(text, nums):
    return re.sub(r'(?<!\d)\d{1,2}(?!\d)', lambda m: str(scale(int(m.group()))) if int(m.group()) in nums else m.group(), text)

def edit_texts(tr):
    """altera `tr` (uid -> texto) no lugar; devolve a lista de uids alterados"""
    qs = quests(); changed = []
    for r, (kind, cnt, _) in qs.items():                           # descricao da missao (se tiver um unico numero, ele vira a contagem nova)
        uid = f'tbl|scp_quest|{r}|1'
        if uid not in tr: continue
        if len(re.findall(r'(?<!\d)\d+(?!\d)', tr[uid])) != 1: continue
        new = re.sub(r'(?<!\d)\d+(?!\d)', str(scale(cnt)), tr[uid])
        if new != tr[uid]: tr[uid] = new; changed.append(uid)
    for n in script_names('ko'):                                   # falas dos scripts que citam as missoes
        code = open(jar('ko') + n + '.scp', 'rb').read(); mine = set()
        for pc, op, b in scp.walk(code):
            if op <= 5:
                r = _quest_of(struct.unpack_from('<I', b, 1)[0])
                if r in qs: mine.add(qs[r][1])
        if not mine: continue
        for i in range(len(scp.text_refs(code))):
            uid = f'dlg|{n}|{i}'
            if uid not in tr: continue
            t = _sub(tr[uid], mine)
            if uid in WORD_UIDS:
                for a, b_ in WORDS: t = re.sub(r'\b' + a + r'\b', b_, t)
            if t != tr[uid]: tr[uid] = t; changed.append(uid)
    return changed

def patch_binary(out):
    """grava as contagens novas em OUT/scp_quest.scp e nos opcodes 0x22 dos scripts; devolve (registros, remocoes)"""
    qs = quests(); path = os.path.join(out, 'scp_quest.scp'); s = bytearray(open(path, 'rb').read())
    for r, (kind, cnt, _) in qs.items():
        assert s[r * 37 + 15] == cnt; s[r * 37 + 15] = scale(cnt)
    open(path, 'wb').write(bytes(s))
    want = {(item, cnt) for kind, cnt, item in qs.values() if kind == 1}; nrem = 0
    for n in script_names('ko'):
        p = os.path.join(out, n + '.scp'); code = bytearray(open(p, 'rb').read()); dirty = False
        for pc, op, b in scp.walk(code):
            if op == 0x22 and (struct.unpack_from('<I', b, 1)[0] & 0xffff, b[5]) in want: code[pc + 5] = scale(b[5]); dirty = True; nrem += 1
        if dirty: open(p, 'wb').write(bytes(code))
    return len(qs), nrem
