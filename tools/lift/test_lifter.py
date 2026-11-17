"""Portao da Fase 2: teste de propriedade do lifter contra o Unicorn.

Para cada instrucao do binary.mod (todas as encodings distintas + todas as instancias PC-relativas + amostra de desvios)
gera um bloco de 1 instrucao, roda N estados aleatorios (registradores, flags) no Unicorn e no C lifted e compara:
r0-r14, N Z C V, proximo PC (com bit0 = Thumb) e CRC da regiao de memoria de teste.

Uso:  build/venv/Scripts/python tools/lift/test_lifter.py [estados_por_instrucao] [--keep]
"""
import os, sys, struct, random, subprocess, zlib, collections, shutil, time
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import cfg as K, lift as L
import capstone as C
from capstone import arm_const as AC
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_PROT_ALL
from unicorn.arm_const import *

NSTATE = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 8
OUT = os.path.join(R, 'build', 'lifttest'); os.makedirs(OUT, exist_ok=True)
REG_BASE, REG_SIZE = 0x2000000, 0x4000
rnd = random.Random(1234)
mem0 = bytes(rnd.randrange(256) for _ in range(REG_SIZE))

# --- seleciona instrucoes --------------------------------------------------------------------------
cfg = K.build()
chosen, seen, branch_cnt = [], set(), collections.Counter()
for a, i in sorted(cfg.insns.items()):
    thumb = cfg.insn_mode[a]; m = i.mnemonic
    pcrel = (i.mnemonic in ('ldr', 'adr') and any(o.type == C.arm.ARM_OP_MEM and o.mem.base == AC.ARM_REG_PC for o in i.operands)) or m == 'adr'
    isbr = m.startswith('b') and m not in ('bic', 'bics') or m == 'blx'
    if isbr:
        k = (m, thumb)
        if branch_cnt[k] >= 40: continue
        branch_cnt[k] += 1
        chosen.append(a); continue
    key = (bytes(i.bytes), thumb)
    if pcrel: chosen.append(a); continue
    if key in seen: continue
    seen.add(key); chosen.append(a)
print('instrucoes a testar:', len(chosen), 'x', NSTATE, 'estados')

class B:  # bloco de 1 instrucao
    def __init__(s, i, th): s.addr, s.thumb, s.insns = i.address, th, [i]
    @property
    def end(s): return s.addr + s.insns[0].size

funcs, unsupported = [], []
for a in chosen:
    i = cfg.insns[a]; th = cfg.insn_mode[a]
    src = L.lift_block(B(i, th))
    if 'PC_TRAP' in src: unsupported.append((hex(a), i.mnemonic, i.op_str)); continue
    funcs.append((a, th, src))
print('nao suportadas:', len(unsupported), unsupported[:12])

# --- gera o C --------------------------------------------------------------------------------------
c_path = os.path.join(OUT, 'gen_test.c')
with open(c_path, 'w') as f:
    f.write('#include "rt.h"\n#include <stdio.h>\n#include <stdlib.h>\nuint8_t *G;\n')
    for a, th, src in funcs: f.write(src)
    f.write('typedef struct { uint32_t addr; BlockFn fn; } Ent;\nstatic Ent tbl[] = {\n')
    for a, th, src in funcs: f.write(f'  {{0x{a:x}u, b_{a:x}}},\n')
    f.write('};\n')
    f.write(r'''
static uint32_t crc_tab[256];
static void crc_init(void){ for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int k=0;k<8;k++) c = c&1 ? 0xedb88320u ^ (c>>1) : c>>1; crc_tab[i]=c; } }
static uint32_t crc32(const uint8_t *p, size_t n){ uint32_t c=0xffffffffu; while(n--) c = crc_tab[(c^*p++)&0xff] ^ (c>>8); return c^0xffffffffu; }
int main(int argc, char **argv){
  crc_init();
  G = calloc(MEM_SIZE, 1);
  FILE *f = fopen(argv[1], "rb"); fread(G + 0x1000, 1, 0x43000, f); fclose(f);          /* text */
  f = fopen(argv[2], "rb"); fread(G + 0x1400000, 1, 0x1000, f); fclose(f);              /* data */
  static uint8_t mem0[0x4000]; f = fopen(argv[3], "rb"); fread(mem0, 1, 0x4000, f); fclose(f);
  f = fopen(argv[4], "rb"); FILE *o = fopen(argv[5], "wb");
  uint32_t rec[21];
  while (fread(rec, 4, 21, f) == 21) {
    int lo = 0, hi = (int)(sizeof tbl / sizeof tbl[0]) - 1; BlockFn fn = 0;
    while (lo <= hi) { int mid = (lo + hi) / 2; if (tbl[mid].addr == rec[0]) { fn = tbl[mid].fn; break; } if (tbl[mid].addr < rec[0]) lo = mid + 1; else hi = mid - 1; }
    Cpu c; memset(&c, 0, sizeof c);
    for (int k = 0; k < 15; k++) c.r[k] = rec[2 + k];
    c.N = rec[17]; c.Z = rec[18]; c.C = rec[19]; c.V = rec[20];
    memcpy(G + 0x2000000, mem0, 0x4000);
    uint32_t pc = fn ? fn(&c) : 0xdeadbeef;
    uint32_t out[21];
    for (int k = 0; k < 15; k++) out[k] = c.r[k];
    out[15] = c.N; out[16] = c.Z; out[17] = c.C; out[18] = c.V; out[19] = pc; out[20] = crc32(G + 0x2000000, 0x4000);
    fwrite(out, 4, 21, o);
  }
  fclose(o); return 0;
}
''')
# text.bin tem 0x432a8; o harness le 0x43000 (suficiente: instrucoes < 0x44000)
open(os.path.join(OUT, 'text.bin'), 'wb').write(K.text)
open(os.path.join(OUT, 'data.bin'), 'wb').write(K.data[:0x1000].ljust(0x1000, b'\0'))
open(os.path.join(OUT, 'mem0.bin'), 'wb').write(mem0)

# --- estados aleatorios + Unicorn -------------------------------------------------------------------
def rand_reg(sp=False):
    k = rnd.random()
    if sp: return REG_BASE + 0x1800 + rnd.randrange(0, 0x400, 4)
    if k < 0.40: return REG_BASE + 0x400 + rnd.randrange(0, 0x2800, 4)
    if k < 0.50: return REG_BASE + 0x400 + rnd.randrange(0, 0x2800)
    if k < 0.78: return rnd.randrange(0, 300) if rnd.random() < 0.8 else (-rnd.randrange(1, 300)) & 0xffffffff
    return rnd.getrandbits(32)

uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
uc.mem_map(0x1000, 0x44000, UC_PROT_ALL); uc.mem_write(0x1000, K.text)
uc.mem_map(0x1400000, 0x2000, UC_PROT_ALL); uc.mem_write(0x1400000, K.data[:0x1000])
uc.mem_map(REG_BASE, REG_SIZE, UC_PROT_ALL)
REGS = [UC_ARM_REG_R0 + k for k in range(13)] + [UC_ARM_REG_SP, UC_ARM_REG_LR]
cases, expect, skipped = [], [], 0
t0 = time.time()
for a, th, src in funcs:
    i = cfg.insns[a]
    for s in range(NSTATE):
        regs = [rand_reg() for _ in range(13)] + [rand_reg(sp=True), rand_reg()]
        fl = [rnd.randrange(2) for _ in range(4)]
        uc.mem_write(REG_BASE, mem0)
        for r, v in zip(REGS, regs): uc.reg_write(r, v)
        cp = uc.reg_read(UC_ARM_REG_CPSR) & 0x0fffffff & ~0x20
        uc.reg_write(UC_ARM_REG_CPSR, cp | (fl[0] << 31) | (fl[1] << 30) | (fl[2] << 29) | (fl[3] << 28))
        try:
            uc.emu_start(a | (1 if th else 0), 0, count=1)
        except UcError:
            skipped += 1; continue
        out = [uc.reg_read(r) for r in REGS]
        cps = uc.reg_read(UC_ARM_REG_CPSR); pc = uc.reg_read(UC_ARM_REG_PC)
        nflags = [(cps >> 31) & 1, (cps >> 30) & 1, (cps >> 29) & 1, (cps >> 28) & 1]
        npc = (pc & ~1) | (1 if cps & 0x20 else 0)
        crc = zlib.crc32(bytes(uc.mem_read(REG_BASE, REG_SIZE)))
        cases.append((a, th, regs, fl)); expect.append((out, nflags, npc, crc))
print(f'casos validos {len(cases)} (descartados por acesso invalido no Unicorn: {skipped}) em {time.time()-t0:.0f}s')
with open(os.path.join(OUT, 'cases.bin'), 'wb') as f:
    for (a, th, regs, fl) in cases: f.write(struct.pack('<21I', a, th, *regs, *fl))

# --- compila e executa o C ----------------------------------------------------------------------------
env_sh = os.path.join(R, 'build', 'env.sh')
cmd = f'source "{env_sh}" && cc -O1 -w -I "{R}/runtime" "{c_path}" -o "{OUT}/gen_test.exe"'
t0 = time.time()
r = subprocess.run(['C:/Program Files/Git/bin/bash.exe', '-c', cmd], capture_output=True, text=True)
if r.returncode: print(r.stdout[-2000:], r.stderr[-3000:]); sys.exit(1)
print(f'compilado em {time.time()-t0:.0f}s')
subprocess.run([os.path.join(OUT, 'gen_test.exe')] + [os.path.join(OUT, n) for n in ('text.bin', 'data.bin', 'mem0.bin', 'cases.bin', 'result.bin')], check=True)
res = open(os.path.join(OUT, 'result.bin'), 'rb').read()
assert len(res) == 84 * len(cases), (len(res), len(cases))

# --- compara -----------------------------------------------------------------------------------------------
bad = collections.defaultdict(list); good = collections.Counter()
for k, (a, th, regs, fl) in enumerate(cases):
    w = struct.unpack_from('<21I', res, 84 * k)
    got = (list(w[0:15]), list(w[15:19]), w[19], w[20]); exp = expect[k]
    i = cfg.insns[a]
    if got == exp: good[i.mnemonic] += 1; continue
    why = []
    if got[0] != exp[0]: why.append('regs ' + ','.join(f'r{n}:{got[0][n]:#x}!={exp[0][n]:#x}' for n in range(15) if got[0][n] != exp[0][n]))
    if got[1] != exp[1]: why.append(f'flags {got[1]}!={exp[1]}')
    if got[2] != exp[2]: why.append(f'pc {got[2]:#x}!={exp[2]:#x}')
    if got[3] != exp[3]: why.append('mem')
    bad[i.mnemonic].append((hex(a), i.mnemonic + ' ' + i.op_str, '; '.join(why), regs, fl))
print('\nOK por mnemonico:', dict(sorted(good.items())))
nbad = sum(len(v) for v in bad.values())
print(f'DIVERGENCIAS: {nbad} em {len(bad)} mnemonicos' if bad else 'DIVERGENCIAS: 0')
for m, v in sorted(bad.items()):
    print(f'-- {m}: {len(v)} divergencias; exemplo:'); a, t, why, regs, fl = v[0]; print('   ', a, t, '|', why[:300], '| flags_in', fl)
sys.exit(1 if bad or unsupported and False else 0)
