"""Fase 1: CFG do binary.mod do Grand Chase 1 (ARM/Thumb) por recursive descent.

Produz blocos basicos (com as instrucoes do capstone) para o lifter. Um bloco termina em desvio, chamada (bl/blx),
retorno ou salto indireto; o endereco de retorno de uma chamada e sempre inicio de bloco (execucao por trampolim).

Uso:  python -I cfg.py          (imprime o relatorio de cobertura)
API:  build() -> Cfg(blocks={addr:Block}, ...)
Endereco com bit0 = Thumb nao e usado aqui: Block.thumb diz o modo; addr e sempre par.
"""
import os, sys, csv, json, struct, collections
import capstone as C
from capstone import arm_const as AC
from elftools.elf.elffile import ELFFile

HERE = os.path.dirname(os.path.abspath(__file__))
# binary.mod da versao PT (nao incluido no repositorio): variavel de ambiente GC1_BINMOD
BINMOD = os.environ.get('GC1_BINMOD')
if not BINMOD or not os.path.isfile(BINMOD):
    sys.exit('defina GC1_BINMOD com o caminho do binary.mod (jar PT extraido), ex.: GC1_BINMOD=/caminho/pt/jar/binary.mod')
FUNCS_TSV = os.path.join(HERE, 'functions.tsv')   # limites das funcoes (sementes do CFG), versionado
TEXT_BASE, DATA_BASE = 0x1000, 0x1400000
ARM_ENTRIES = {0x1000}          # unico codigo ARM (clet_register_entry); o resto e Thumb
RODATA = 0x40fc8              # a partir daqui so ha strings/tabelas (o codigo acaba antes)

_elf = ELFFile(open(BINMOD, 'rb'))
text = _elf.get_section_by_name('.text').data()
data = _elf.get_section_by_name('.data').data()
TEXT_END = TEXT_BASE + len(text)

_md = {True: C.Cs(C.CS_ARCH_ARM, C.CS_MODE_THUMB), False: C.Cs(C.CS_ARCH_ARM, C.CS_MODE_ARM)}
for m in _md.values(): m.detail = True


def rd32(a):
    if TEXT_BASE <= a < TEXT_END - 3: return struct.unpack_from('<I', text, a - TEXT_BASE)[0]
    if DATA_BASE <= a < DATA_BASE + len(data) - 3: return struct.unpack_from('<I', data, a - DATA_BASE)[0]
    return None


def decode_one(addr, thumb):
    """Decodifica UMA instrucao em addr (ou None)."""
    if not (TEXT_BASE <= addr < TEXT_END): return None
    off = addr - TEXT_BASE
    for i in _md[thumb].disasm(text[off:off + 4], addr, 1): return i
    return None


class Block:
    __slots__ = ('addr', 'thumb', 'insns', 'succ', 'kind', 'table')
    def __init__(self, addr, thumb): self.addr, self.thumb, self.insns, self.succ, self.kind, self.table = addr, thumb, [], [], '', None
    @property
    def end(self): i = self.insns[-1]; return i.address + i.size


class Cfg:
    def __init__(self): self.blocks = {}; self.insn_mode = {}; self.unresolved = []; self.bad = []; self.tables = {}; self.skipped = []


STUBS = {}
def scan_stubs():
    """Stubs de import de 16 bytes: str lr,[sp,#-4]! ; bl <resolvedor 0x409b4> ; .word tabela ; .word indice."""
    out = {}
    for base, blob in ((TEXT_BASE, text), (DATA_BASE, data)):
        for off in range(0, len(blob) - 15, 4):
            w0, w1, tbl, idx = struct.unpack_from('<IIII', blob, off)
            if w0 == 0xe52de004 and (w1 >> 24) == 0xeb:
                imm = w1 & 0xffffff
                if imm & 0x800000: imm -= 0x1000000
                tgt = base + off + 4 + 8 + imm * 4
                if tgt == 0x409b4: out[base + off] = (tbl, idx)
    return out


STUBS.update(scan_stubs())


def load_functions():
    fs = []
    for ln in open(FUNCS_TSV, encoding='utf8'):
        if ln.startswith('#') or not ln.strip(): continue
        a, sz = ln.split()
        a = int(a, 16)
        if a < DATA_BASE and a not in STUBS and a not in (0x409b4, 0x40f34): fs.append((a, int(sz), ''))
    return sorted(fs)


def reloc_code_pointers():
    """alvos de relocacoes ABS32 que apontam para .text (impar = Thumb; par = rotulo de tabela/ARM)."""
    e = ELFFile(open(BINMOD, 'rb')); out = {}
    for sec, base, blob in (('.rel.text', TEXT_BASE, text), ('.rel.data', DATA_BASE, data)):
        for x in e.get_section_by_name(sec).iter_relocations():
            if x['r_info_type'] != 2: continue
            v = struct.unpack_from('<I', blob, x['r_offset'] - base)[0]
            if TEXT_BASE <= v < RODATA: out[v & ~1] = bool(v & 1)
    return out


def is_ret_or_indirect(i, thumb):
    m, ops = i.mnemonic, i.operands
    if m == 'bx': return 'bx_lr' if ops[0].reg == AC.ARM_REG_LR else 'bx'
    if m == 'pop' or m.startswith('ldm') or m.startswith('pop'):
        if any(o.type == C.arm.ARM_OP_REG and o.reg == AC.ARM_REG_PC for o in ops): return 'ret_pc'
    if m in ('mov', 'movs') and ops and ops[0].type == C.arm.ARM_OP_REG and ops[0].reg == AC.ARM_REG_PC: return 'jmp_reg'
    if m == 'ldr' and ops and ops[0].type == C.arm.ARM_OP_REG and ops[0].reg == AC.ARM_REG_PC: return 'jmp_ldr'
    if m == 'add' and ops and ops[0].type == C.arm.ARM_OP_REG and ops[0].reg == AC.ARM_REG_PC: return 'jmp_add'
    return ''


def branch_target(i):
    for o in i.operands:
        if o.type == C.arm.ARM_OP_IMM: return o.imm
    return None


def resolve_switch(cfg, jaddr, thumb):
    """Tabela de switch Thumb: ldr rB,[pc,#]; (cmp rI,#N antes do bhi); ldr rT,[rI, rB]; mov pc,rT.
    Acha o 'ldr rT,[rI,rB]' (indice em registrador) e o ultimo 'ldr rB,[pc,#]' antes dele (o compilador pode
    intercalar outros 'ldr r?,[pc,#]' entre a carga da tabela e o salto)."""
    back, a = [], jaddr
    for _ in range(20):
        a -= 2
        i = decode_one(a, True)
        if i is not None: back.append(i)          # do mais proximo para o mais distante
    ld = next((i for i in back if i.mnemonic == 'ldr' and i.operands[1].type == C.arm.ARM_OP_MEM
               and i.operands[1].mem.index != 0 and i.operands[1].mem.base != AC.ARM_REG_PC), None)
    if ld is None: return None
    rb = ld.operands[1].mem.base
    base = n = None
    for i in back:
        if i.address >= ld.address: continue
        if base is None and i.mnemonic == 'ldr' and i.operands[0].reg in (ld.operands[1].mem.base, ld.operands[1].mem.index)                 and i.operands[1].type == C.arm.ARM_OP_MEM and i.operands[1].mem.base == AC.ARM_REG_PC:
            lit = ((i.address + 4) & ~3) + i.operands[1].mem.disp
            v = rd32(lit)
            if v is not None and TEXT_BASE <= v < TEXT_END: base = v
        if n is None and i.mnemonic == 'cmp' and i.operands[1].type == C.arm.ARM_OP_IMM and 0 < i.operands[1].imm < 512:
            n = i.operands[1].imm + 1
    if base is None: return None
    ents = []
    for k in range(n if n else 64):
        v = rd32(base + 4 * k)
        if v is None or not (TEXT_BASE <= (v & ~1) < RODATA): break
        ents.append(v & ~1)
    return ents if ents else None


def build():
    cfg = Cfg()
    funcs = load_functions()
    bodies = [(a, a + s) for a, s, _ in funcs]
    in_body = lambda x: any(a <= x < b for a, b in bodies)
    code_ranges = sorted(bodies)
    import bisect
    starts = [a for a, _ in code_ranges]
    def in_code(x):
        k = bisect.bisect_right(starts, x) - 1
        return k >= 0 and code_ranges[k][0] <= x < code_ranges[k][1]

    func_entries = {a for a, _, _ in funcs}
    work = collections.deque(); leaders = {}      # addr -> thumb
    leader_src = {}
    def add(addr, thumb, src=None):
        leader_src.setdefault(addr, src)
        if addr in leaders:
            if leaders[addr] != thumb: cfg.bad.append(('modo conflitante', hex(addr)))
            return
        if not (TEXT_BASE <= addr < RODATA): return
        # alvo de desvio de codigo ja decodificado e confiavel; sementes (entradas Ghidra / relocacoes) so se forem entrada de funcao
        if src is None and addr not in func_entries:
            cfg.skipped.append((hex(addr), hex(src or 0))); return
        leaders[addr] = thumb; work.append((addr, thumb))
    for a, s, n in funcs:
        add(a, a not in ARM_ENTRIES)
    for a, thumb in reloc_code_pointers().items():
        if thumb: add(a, True)                      # alvo Thumb de ponteiro de funcao
    add(0x40fb8, True)

    insns = {}                                      # addr -> (insn, thumb)
    while work:
        a, thumb = work.popleft()
        while True:
            if a in insns:
                # ja decodificado: so garante que seja inicio de bloco se alguem desviou para ca
                break
            i = decode_one(a, thumb)
            if i is None or i.id == 0:
                cfg.bad.append(('nao decodifica', hex(a), 'veio de', hex(leader_src.get(a) or 0))); break
            insns[a] = (i, thumb)
            nxt = a + i.size
            m = i.mnemonic
            cond = i.cc not in (AC.ARM_CC_AL, AC.ARM_CC_INVALID) if hasattr(i, 'cc') else False
            kind = is_ret_or_indirect(i, thumb)
            if kind == 'bx' and thumb:
                pv = insns.get(a - 2, (None, 0))[0]
                if pv is not None and pv.mnemonic == 'pop' and len(pv.operands) == 1 and pv.operands[0].reg == i.operands[0].reg: kind = 'ret_pc'
            if m in ('b', 'bl', 'blx') or (m.startswith('b') and i.group(C.CS_GRP_JUMP) and not kind and not m.startswith('bx') and m not in ('bic', 'bics', 'bkpt', 'bfi', 'bfc')):
                t = branch_target(i)
                if t is None:                          # blx rN
                    add(nxt, thumb, a); break
                if m == 'blx': add(t & ~1, not thumb, a)
                else: add(t, thumb, a)
                if m in ('bl', 'blx'): add(nxt, thumb, a)
                elif cond: add(nxt, thumb, a)
                break
            if kind in ('bx', 'jmp_reg', 'jmp_ldr', 'jmp_add'):
                if kind == 'jmp_reg':
                    ents = resolve_switch(cfg, a, thumb) if thumb else None
                    if ents:
                        cfg.tables[a] = ents
                        for t in ents: add(t, True, a)
                    else: cfg.unresolved.append((hex(a), kind, i.op_str))
                elif kind != 'bx': cfg.unresolved.append((hex(a), kind, i.op_str))
                if kind == 'bx': add(nxt, thumb, a)       # pode ser "mov lr,pc; bx rN": retorno cai em nxt
                break
            if kind in ('bx_lr', 'ret_pc'):
                if cond: add(nxt, thumb, a)
                break
            a = nxt
    # cria blocos: lideres = alvos coletados; um bloco vai ate o proximo lider/terminador
    bl = sorted(leaders)
    leader_set = set(bl)
    for a in bl:
        if a not in insns: continue
        thumb = leaders[a]; b = Block(a, thumb); p = a
        while p in insns:
            i, th = insns[p]; b.insns.append(i); p += i.size
            if p in leader_set: b.succ.append(p); break
            kind = is_ret_or_indirect(i, th); m = i.mnemonic
            if kind or m in ('b', 'bl', 'blx') or (i.group(C.CS_GRP_JUMP)):
                break
        cfg.blocks[a] = b
    cfg.insn_mode = {a: th for a, (i, th) in insns.items()}
    cfg.insns = {a: i for a, (i, th) in insns.items()}
    cfg.funcs = funcs
    return cfg


if __name__ == '__main__':
    cfg = build()
    ninsn = sum(len(b.insns) for b in cfg.blocks.values())
    print('blocos', len(cfg.blocks), '| instrucoes', ninsn, '| switch resolvidos', len(cfg.tables))
    print('saltos indiretos nao resolvidos:', len(cfg.unresolved)); [print('  ', u) for u in cfg.unresolved[:12]]
    print('falhas de decodificacao/modo:', len(cfg.bad)); [print('  ', u) for u in cfg.bad[:12]]
    # cobertura: bytes de cada funcao do Ghidra cobertos por instrucoes alcancadas
    tot = cov = 0; worst = []
    for a, s, n in cfg.funcs:
        c = 0; p = a
        for p in range(a, a + s):
            pass
        covered = sum(cfg.insns[x].size for x in cfg.insns if a <= x < a + s)
        tot += s; cov += covered
        if covered < s: worst.append((s - covered, hex(a), n, s))
    print(f'cobertura: {cov}/{tot} bytes ({100*cov/tot:.1f}%) das funcoes do Ghidra')
    worst.sort(reverse=True)
    for w in worst[:10]: print('  falta', w)
