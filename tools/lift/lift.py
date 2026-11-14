"""Fase 2: lifter ARM/Thumb -> C (um bloco basico = uma funcao C que devolve o proximo PC).

Convencoes do codigo gerado (ver runtime/rt.h):
  - registradores do bloco sao variaveis locais r0..r14, carregadas de c->r[] se lidas antes de escritas e salvas ao sair se escritas;
  - flags N Z C V idem (se o bloco toca em flags, os 4 sao carregados/salvos);
  - o valor de retorno e o proximo PC; bit0 = 1 -> Thumb.
Instrucao nao suportada -> o bloco devolve PC_TRAP (nunca deve ser executado; o runtime registra o endereco).
"""
import re
import capstone as C
from capstone import arm_const as AC

REGNAME = {**{f'r{i}': i for i in range(13)}, 'sp': 13, 'lr': 14, 'pc': 15, 'ip': 12, 'fp': 11, 'sb': 9, 'sl': 10}
CC = {AC.ARM_CC_EQ: 'Z', AC.ARM_CC_NE: '!Z', AC.ARM_CC_HS: 'C', AC.ARM_CC_LO: '!C', AC.ARM_CC_MI: 'N', AC.ARM_CC_PL: '!N',
      AC.ARM_CC_VS: 'V', AC.ARM_CC_VC: '!V', AC.ARM_CC_HI: '(C && !Z)', AC.ARM_CC_LS: '(!C || Z)', AC.ARM_CC_GE: '(N == V)',
      AC.ARM_CC_LT: '(N != V)', AC.ARM_CC_GT: '(!Z && N == V)', AC.ARM_CC_LE: '(Z || N != V)'}
_cs = {True: C.Cs(C.CS_ARCH_ARM, C.CS_MODE_THUMB), False: C.Cs(C.CS_ARCH_ARM, C.CS_MODE_ARM)}
SAVE = '\x00SAVE\x00'


class Unsupported(Exception): pass


class BlockGen:
    def __init__(self, addr, thumb):
        self.addr, self.thumb = addr, thumb
        self.body, self.reads, self.writes = [], set(), set()
        self.flags = False            # o bloco toca nos flags
        self.cur = None               # instrucao em traducao

    # --- registradores --------------------------------------------------------------------------
    def reg(self, op):
        n = REGNAME[_cs[self.thumb].reg_name(op.reg) if hasattr(op, 'reg') else op]
        return n
    def R(self, n):
        if n == 15: return f'{self.pcval()}u'
        if n not in self.writes: self.reads.add(n)
        return f'r{n}'
    def W(self, n):
        if n == 15: raise Unsupported('escrita direta em pc')
        self.writes.add(n); return f'r{n}'
    def pcval(self):                  # valor lido de PC por uma instrucao
        i = self.cur
        return (i.address + 4) if self.thumb else (i.address + 8)
    def e(self, s): self.body.append('    ' + s)

    # --- operandos -------------------------------------------------------------------------------
    def val(self, op):
        if op.type == C.arm.ARM_OP_REG: return self.R(self.reg(op))
        if op.type == C.arm.ARM_OP_IMM: return f'{op.imm & 0xffffffff:#x}u'
        raise Unsupported('operando')
    def ea(self, op):                 # endereco efetivo de [base, index/#disp]
        m = op.mem
        base = REGNAME[_cs[self.thumb].reg_name(m.base)]
        if base == 15:
            b = f'{(self.pcval() & ~3):#x}u'
        else: b = self.R(base)
        if m.index:
            if m.scale != 1 or m.lshift: raise Unsupported('indice com deslocamento')
            return f'({b} + {self.R(REGNAME[_cs[self.thumb].reg_name(m.index)])})'
        if m.disp: return f'({b} + {m.disp & 0xffffffff:#x}u)'
        return b
    def nzcv(self): self.flags = True

    # --- saida (restaura registradores e flags) ---------------------------------------------------
    def ret(self, expr): self.e(f'{SAVE} return {expr};')


def ret_val(target, thumb): return f'{(target & ~1) | (1 if thumb else 0):#x}u'


def lift_insn(g, i, thumb):
    """Traduz uma instrucao. Devolve True se terminou o bloco (ja emitiu o return)."""
    g.cur = i; m = i.mnemonic; ops = i.operands; n = len(ops)
    if i.cc not in (AC.ARM_CC_AL, AC.ARM_CC_INVALID) and not (m.startswith('b') and m not in ('bic', 'bics', 'bl', 'blx', 'bx')):
        raise Unsupported('instrucao condicional (nao desvio)')
    T = C.arm.ARM_OP_REG
    a = i.address
    nxt = a + i.size
    # ---- desvios ----
    if m == 'b' or (m.startswith('b') and i.cc not in (AC.ARM_CC_AL, AC.ARM_CC_INVALID) and m not in ('bic', 'bics', 'bl', 'blx', 'bx')):
        t = ops[0].imm
        if i.cc in (AC.ARM_CC_AL, AC.ARM_CC_INVALID):
            g.ret(ret_val(t, thumb)); return True
        g.nzcv(); g.e(f'if ({CC[i.cc]}) {{ {SAVE} return {ret_val(t, thumb)}; }}')
        g.ret(ret_val(nxt, thumb)); return True
    if m == 'bl':
        g.writes.add(14); g.e(f'r14 = {(nxt | 1) if thumb else nxt:#x}u;'); g.ret(ret_val(ops[0].imm, thumb)); return True
    if m == 'blx' and ops[0].type == C.arm.ARM_OP_IMM:
        g.writes.add(14); g.e(f'r14 = {(nxt | 1) if thumb else nxt:#x}u;'); g.ret(ret_val(ops[0].imm, not thumb)); return True
    if m == 'blx':
        g.e(f'{{ uint32_t t = {g.R(g.reg(ops[0]))};'); g.writes.add(14); g.e(f'  r14 = {(nxt | 1) if thumb else nxt:#x}u;')
        g.ret('t'); g.e('}'); return True
    if m == 'bx':
        g.ret(g.R(g.reg(ops[0]))); return True
    # ---- pilha / blocos de registradores ----
    if m in ('push', 'pop') or (m in ('ldm', 'stm') and True):
        pass
    if m == 'push':
        regs = sorted(g.reg(o) for o in ops)
        g.e(f'{{ uint32_t a = {g.R(13)} - {4 * len(regs)}u;'); sp = g.W(13); g.e(f'  {sp} = a;')
        for k, r in enumerate(regs): g.e(f'  ST32(a + {4 * k}u, {g.R(r) if r != 15 else str(a + (4 if thumb else 8)) + "u"});')
        g.e('}'); return False
    if m == 'pop':
        regs = sorted(g.reg(o) for o in ops)
        g.e(f'{{ uint32_t a = {g.R(13)};')
        for k, r in enumerate(regs):
            if r == 15: g.e(f'  uint32_t newpc = LD32(a + {4 * k}u);')
            else: g.e(f'  {g.W(r)} = LD32(a + {4 * k}u);')
        g.e(f'  {g.W(13)} = a + {4 * len(regs)}u;')
        if 15 in regs: g.ret('newpc'); g.e('}'); return True
        g.e('}'); return False
    if m in ('ldm', 'stm', 'ldmia', 'stmia', 'ldmdb', 'stmdb', 'stmfd', 'ldmfd'):
        base = g.reg(ops[0]); regs = sorted(g.reg(o) for o in ops[1:])
        db = m in ('ldmdb', 'stmdb', 'stmfd')
        g.e(f'{{ uint32_t a = {g.R(base)}{(" - %du" % (4 * len(regs))) if db else ""};')
        for k, r in enumerate(regs):
            if m.startswith('ld'):
                if r == 15: g.e(f'  uint32_t newpc = LD32(a + {4 * k}u);')
                else: g.e(f'  {g.W(r)} = LD32(a + {4 * k}u);')
            else: g.e(f'  ST32(a + {4 * k}u, {g.R(r)});')
        wb = i.writeback and base not in regs
        if wb: g.e(f'  {g.W(base)} = {g.R(base)} {"-" if db else "+"} {4 * len(regs)}u;')
        if m.startswith('ld') and 15 in regs: g.ret('newpc'); g.e('}'); return True
        g.e('}'); return False
    # ---- movimentacao ----
    if m in ('mov', 'movs'):
        rd = g.reg(ops[0])
        if ops[1].type == C.arm.ARM_OP_REG and g.reg(ops[1]) == 15 and not thumb:
            src = f'{a + 8:#x}u'
        else: src = g.val(ops[1])
        if rd == 15:
            g.ret(f'(({src}) & ~1u) | 1u'); return True
        g.e(f'{g.W(rd)} = {src};')
        if i.update_flags: g.nzcv(); g.e(f'SETNZ({g.R(rd)});')
        return False
    if m == 'mvns':
        rd = g.reg(ops[0]); src = g.val(ops[1]); g.e(f'{g.W(rd)} = ~{src};'); g.nzcv(); g.e(f'SETNZ({g.R(rd)});'); return False
    if m == 'adr':
        g.e(f'{g.W(g.reg(ops[0]))} = {((a + 4) & ~3) + ops[1].imm:#x}u;'); return False
    # ---- aritmetica ----
    if m in ('add', 'adds', 'sub', 'subs'):
        add = m.startswith('add')
        if n == 3: rd, x, y = g.reg(ops[0]), g.val(ops[1]), g.val(ops[2])
        else: rd = g.reg(ops[0]); x, y = g.R(rd), g.val(ops[1])
        if i.update_flags:
            g.nzcv(); t = 'tmp'
            g.e(f'{{ uint32_t tmp; {"ADDF" if add else "SUBF"}(tmp, {x}, {y}, {"0" if add else "1"}); {g.W(rd)} = tmp; }}')
        else:
            g.e(f'{g.W(rd)} = {x} {"+" if add else "-"} {y};')
        return False
    if m in ('adcs', 'sbcs'):
        rd = g.reg(ops[0]); g.nzcv()
        g.e(f'{{ uint32_t tmp; {"ADDF" if m == "adcs" else "SUBF"}(tmp, {g.R(rd)}, {g.val(ops[1])}, C); {g.W(rd)} = tmp; }}'); return False
    if m == 'rsbs':
        rd = g.reg(ops[0]); g.nzcv(); g.e(f'{{ uint32_t tmp; SUBF(tmp, 0u, {g.val(ops[1])}, 1); {g.W(rd)} = tmp; }}'); return False
    if m in ('cmp', 'cmn'):
        g.nzcv(); g.e(f'{{ uint32_t tmp; {"SUBF" if m == "cmp" else "ADDF"}(tmp, {g.val(ops[0])}, {g.val(ops[1])}, {"1" if m == "cmp" else "0"}); (void)tmp; }}'); return False
    if m == 'tst':
        g.nzcv(); g.e(f'{{ uint32_t tmp = {g.val(ops[0])} & {g.val(ops[1])}; SETNZ(tmp); }}'); return False
    if m in ('ands', 'orrs', 'eors', 'bics'):
        rd = g.reg(ops[0]); x, y = g.R(rd), g.val(ops[1])
        op = {'ands': f'{x} & {y}', 'orrs': f'{x} | {y}', 'eors': f'{x} ^ {y}', 'bics': f'{x} & ~{y}'}[m]
        g.e(f'{g.W(rd)} = {op};'); g.nzcv(); g.e(f'SETNZ({g.R(rd)});'); return False
    if m == 'muls':
        rd = g.reg(ops[0]); x, y = g.val(ops[1]), g.val(ops[2]); g.e(f'{g.W(rd)} = {x} * {y};'); g.nzcv(); g.e(f'SETNZ({g.R(rd)});'); return False
    if m in ('lsls', 'lsrs', 'asrs', 'rors'):
        rd = g.reg(ops[0]); mac = {'lsls': 'LSLC', 'lsrs': 'LSRC', 'asrs': 'ASRC', 'rors': 'RORC'}[m]; g.nzcv()
        if n == 3 and ops[2].type == C.arm.ARM_OP_IMM:
            src, amt = g.val(ops[1]), f'{ops[2].imm}u'
        else:
            src = g.R(rd) if n == 2 else g.val(ops[1]); amt = f'({g.val(ops[-1])} & 0xffu)'
        g.e(f'{{ uint32_t tmp; {mac}(tmp, {src}, {amt}); {g.W(rd)} = tmp; SETNZ(tmp); }}'); return False
    # ---- memoria ----
    LD = {'ldr': 'LD32({a})', 'ldrb': 'LD8({a})', 'ldrh': 'LD16({a})', 'ldrsb': '(uint32_t)(int32_t)(int8_t)LD8({a})', 'ldrsh': '(uint32_t)(int32_t)(int16_t)LD16({a})'}
    ST = {'str': 'ST32({a}, {v})', 'strb': 'ST8({a}, {v})', 'strh': 'ST16({a}, {v})'}
    if m in LD:
        rd = g.reg(ops[0]); ea = g.ea(ops[1])
        if rd == 15: raise Unsupported('ldr pc')
        g.e(f'{g.W(rd)} = {LD[m].format(a=ea)};'); return False
    if m in ST:
        g.e(ST[m].format(a=g.ea(ops[1]), v=g.R(g.reg(ops[0]))) + ';'); return False
    raise Unsupported(m)


def lift_block(b, cfg_insn_mode=None):
    """b: cfg.Block -> texto C da funcao do bloco."""
    g = BlockGen(b.addr, b.thumb)
    done = False
    try:
        for i in b.insns:
            if lift_insn(g, i, b.thumb): done = True; break
    except Unsupported as ex:
        g2 = BlockGen(b.addr, b.thumb)
        g2.e(f'/* nao suportado: {b.insns[0].address:#x}: {ex} */'); g2.ret('PC_TRAP')
        g, done = g2, True
    if not done:
        g.ret(ret_val(b.end, b.thumb))
    # prologo / epilogo
    pro = []
    for r in sorted(g.reads): pro.append(f'    uint32_t r{r} = c->r[{r}];')
    for r in sorted(g.writes - g.reads): pro.append(f'    uint32_t r{r};')
    if g.flags: pro.append('    uint8_t N = c->N, Z = c->Z, C = c->C, V = c->V;')
    save = ' '.join(f'c->r[{r}] = r{r};' for r in sorted(g.writes))
    if g.flags: save += ' c->N = N; c->Z = Z; c->C = C; c->V = V;'
    text = '\n'.join(g.body).replace(SAVE, save)
    return f'uint32_t b_{b.addr:x}(Cpu *c) {{\n' + '\n'.join(pro) + ('\n' if pro else '') + text + '\n}\n'
