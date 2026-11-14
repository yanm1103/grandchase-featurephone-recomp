"""Gera gen/*.c a partir do binary.mod: um bloco C por bloco basico, tabelas de despacho e tabela de stubs de import.

Uso: python -I emit.py [pasta_saida=gen]
Saidas:
  gen/blocks_NN.c   blocos (funcoes b_XXXXXX) + gc1_reg_NN() que registra em gc_blocks[]/gc_mode[]
  gen/tables.c      gc_blocks[], gc_mode[], gc_stubs[], gc1_register_all(), GC1_TEXT_FNV
  gen/report.txt    estatisticas e lista de blocos nao suportados
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import cfg as K, lift as L

R = os.path.dirname(os.path.dirname(HERE))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(R, 'gen')
os.makedirs(OUT, exist_ok=True)
SHARD = 2500
TEXT_HALF = (len(K.text) + 3) // 2 + 2


def fnv64(b):
    h = 0xcbf29ce484222325
    for x in b: h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h


cfg = K.build()
blocks = [cfg.blocks[a] for a in sorted(cfg.blocks)]
trap, nshard = [], 0
for old in os.listdir(OUT):
    if old.startswith('blocks_') and old.endswith('.c'): os.remove(os.path.join(OUT, old))
for s in range(0, len(blocks), SHARD):
    chunk = blocks[s:s + SHARD]
    with open(os.path.join(OUT, f'blocks_{nshard:02d}.c'), 'w', newline='\n') as f:
        f.write('/* gerado por tools/lift/emit.py - nao edite */\n#include "rt.h"\n')
        f.write('extern BlockFn gc_blocks[]; extern uint8_t gc_mode[];\n')
        regs = []
        for b in chunk:
            src = L.lift_block(b)
            if 'PC_TRAP' in src: trap.append((b.addr, b.insns[0].mnemonic))
            f.write(src)
            regs.append(f'  gc_blocks[{(b.addr - K.TEXT_BASE) >> 1}] = b_{b.addr:x}; gc_mode[{(b.addr - K.TEXT_BASE) >> 1}] = {1 if b.thumb else 2};')
        f.write(f'void gc1_reg_{nshard:02d}(void) {{\n' + '\n'.join(regs) + '\n}\n')
    nshard += 1

with open(os.path.join(OUT, 'tables.c'), 'w', newline='\n') as f:
    f.write('/* gerado por tools/lift/emit.py - nao edite */\n#include "gc1.h"\n')
    f.write(f'BlockFn gc_blocks[{TEXT_HALF}];\nuint8_t gc_mode[{TEXT_HALF}];\nconst uint32_t gc_text_halfwords = {TEXT_HALF};\n')
    f.write(f'const uint64_t GC1_TEXT_FNV = 0x{fnv64(K.text):x}ull;\nconst uint32_t GC1_TEXT_SIZE = {len(K.text)};\n')
    f.write(f'const uint64_t GC1_CODE_FNV = 0x{fnv64(K.text[:K.RODATA - K.TEXT_BASE]):x}ull;
const uint32_t GC1_CODE_SIZE = {K.RODATA - K.TEXT_BASE};
')
    f.write('const GcStub gc_stubs[] = {\n')
    for a in sorted(K.STUBS):
        t, i = K.STUBS[a]
        f.write(f'  {{0x{a:x}u, 0x{t:x}u, 0x{i:x}u}},\n')
    f.write(f'}};\nconst int gc_nstubs = {len(K.STUBS)};\n')
    for k in range(nshard): f.write(f'void gc1_reg_{k:02d}(void);\n')
    f.write('void gc1_register_all(void) {\n' + ''.join(f'  gc1_reg_{k:02d}();\n' for k in range(nshard)) + '}\n')

with open(os.path.join(OUT, 'report.txt'), 'w') as f:
    f.write(f'blocos {len(blocks)} em {nshard} arquivos; instrucoes {sum(len(b.insns) for b in blocks)}\n')
    f.write(f'stubs {len(K.STUBS)}; switch resolvidos {len(cfg.tables)}\n')
    f.write(f'blocos com instrucao nao suportada (PC_TRAP): {len(trap)}\n')
    for a, m in trap: f.write(f'  {a:#x} {m}\n')
print(open(os.path.join(OUT, 'report.txt')).read()[:600])
