/* cpu_unicorn.c - backend de REFERENCIA: executa o binary.mod no Unicorn (emulador ARM independente) com o mesmo HLE e a
 * mesma memoria do guest (G mapeada direto no Unicorn). Serve para validar o lifter por igualdade de traces. */
#include <stdlib.h>
#include <stdarg.h>
#include <unicorn/unicorn.h>
#include "gc1.h"

uint8_t *G;
Cpu gc_cpu;
void gc1_hle_call(const GcStub *s, Cpu *c);

void gc1_fatal(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "gc1(ref): erro fatal: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap);
    if (gc1_trace) fflush(gc1_trace);
    exit(2);
}
void gc1_bad_access(uint32_t a, int sz, int wr, uint32_t pc) { gc1_fatal("acesso invalido do HLE em 0x%08x", a); }

static uc_engine *uc;
static const int REG[15] = {UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
                            UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR};
static int g_done; static uint32_t g_next_pc; static int g_hle_stop;

static const GcStub *find_stub(uint32_t a) {
    int lo = 0, hi = gc_nstubs - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (gc_stubs[m].addr == a) return &gc_stubs[m]; if (gc_stubs[m].addr < a) lo = m + 1; else hi = m - 1; }
    return NULL;
}
static uint32_t rd(int r) { uint32_t v = 0; uc_reg_read(uc, r, &v); return v; }
static void wr(int r, uint32_t v) { uc_reg_write(uc, r, &v); }

static void on_code(uc_engine *u, uint64_t addr, uint32_t size, void *ud) {
    const GcStub *s = find_stub((uint32_t)addr);
    if (!s) return;
    for (int k = 0; k < 15; k++) gc_cpu.r[k] = rd(REG[k]);
    gc1_hle_call(s, &gc_cpu);
    wr(UC_ARM_REG_R0, gc_cpu.r[0]); wr(UC_ARM_REG_R1, gc_cpu.r[1]);
    g_next_pc = gc_cpu.r[14]; g_hle_stop = 1;           /* retorna para LR (bit0 = Thumb) */
    uc_emu_stop(uc);                                   /* o laco externo reinicia no novo PC */
}
static bool on_invalid(uc_engine *u, uc_mem_type type, uint64_t addr, int size, int64_t value, void *ud) {
    gc1_fatal("Unicorn: acesso de memoria invalido (tipo %d) em 0x%llx, pc=0x%08x", (int)type, (unsigned long long)addr, rd(UC_ARM_REG_PC));
    return false;
}

static void uc_setup(void) {
    if (uc) return;
    uc_err e = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc); if (e) gc1_fatal("uc_open: %s", uc_strerror(e));
    e = uc_mem_map_ptr(uc, 0, MEM_SIZE, UC_PROT_ALL, G); if (e) gc1_fatal("uc_mem_map_ptr: %s", uc_strerror(e));
    uc_hook h;
    uc_hook_add(uc, &h, UC_HOOK_CODE, (void *)on_code, NULL, 0x1400030, 0x1400470);
    uint32_t lo = 0xffffffffu, hi = 0;
    for (int i = 0; i < gc_nstubs; i++) if (gc_stubs[i].addr < DATA_BASE) { if (gc_stubs[i].addr < lo) lo = gc_stubs[i].addr; if (gc_stubs[i].addr > hi) hi = gc_stubs[i].addr; }
    uc_hook_add(uc, &h, UC_HOOK_CODE, (void *)on_code, NULL, lo, hi);
    uc_hook_add(uc, &h, UC_HOOK_MEM_INVALID, (void *)on_invalid, NULL, 1, 0);
    wr(UC_ARM_REG_SP, gc_cpu.r[13]);
}

uint32_t gc1_call(uint32_t addr, int nargs, const uint32_t *args) {
    uc_setup();
    uint32_t sp0 = rd(UC_ARM_REG_SP), sp = sp0 & ~7u;
    if (nargs > 4) { sp -= 4u * (nargs - 4); sp &= ~7u; for (int k = 4; k < nargs; k++) ST32(sp + 4u * (k - 4), args[k]); }
    wr(UC_ARM_REG_SP, sp);
    for (int k = 0; k < 4 && k < nargs; k++) wr(REG[k], args[k]);
    wr(UC_ARM_REG_LR, SENTINEL);
    uint32_t pc = addr;                                  /* bit0 = Thumb */
    for (;;) {
        if ((pc & ~1u) == (SENTINEL & ~1u)) break;
        g_hle_stop = 0;
        uc_err e = uc_emu_start(uc, pc, SENTINEL & ~1u, 0, 0);       /* begin com bit0 seleciona Thumb/ARM */
        if (e) gc1_fatal("uc_emu_start: %s (pc=0x%08x)", uc_strerror(e), rd(UC_ARM_REG_PC));
        if (g_hle_stop) { pc = g_next_pc; continue; }
        break;                                                        /* parou no sentinela */
    }
    uint32_t ret = rd(UC_ARM_REG_R0);
    wr(UC_ARM_REG_SP, sp0);
    return ret;
}
