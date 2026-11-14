/* cpu.c - trampolim de blocos, chamada de funcoes do guest e interceptacao de stubs de import. */
#include <stdlib.h>
#include <stdarg.h>
#include "gc1.h"

uint8_t *G;
Cpu gc_cpu;
uint8_t gc_cov[0x22000];                 /* 1 = bloco executado (cobertura) */
int gc1_cov_dump(const char *path) { FILE *f = fopen(path, "w"); if (!f) return 0; int n = 0; for (uint32_t i = 0; i < gc_text_halfwords && i < sizeof gc_cov; i++) if (gc_cov[i]) { fprintf(f, "%x\n", TEXT_BASE + 2 * i); n++; } fclose(f); return n; }

void gc1_hle_call(const GcStub *s, Cpu *c);       /* hle.c */

void gc1_fatal(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "gc1: erro fatal: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
    va_end(ap);
    if (gc1_trace) fflush(gc1_trace);
    exit(2);
}
void gc1_bad_access(uint32_t a, int sz, int wr, uint32_t pc) {
    gc1_fatal("acesso invalido %s de %d bytes em 0x%08x (r15 do ultimo bloco 0x%08x)", wr ? "escrita" : "leitura", sz, a, pc);
}

static const GcStub *find_stub(uint32_t a) {
    int lo = 0, hi = gc_nstubs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (gc_stubs[mid].addr == a) return &gc_stubs[mid];
        if (gc_stubs[mid].addr < a) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

uint32_t gc1_call(uint32_t addr, int nargs, const uint32_t *args) {
    Cpu *c = &gc_cpu;
    uint32_t sp0 = c->r[13];
    uint32_t sp = sp0 & ~7u;
    if (nargs > 4) { sp -= 4u * (nargs - 4); sp &= ~7u; for (int k = 4; k < nargs; k++) ST32(sp + 4u * (k - 4), args[k]); }
    c->r[13] = sp;
    for (int k = 0; k < 4 && k < nargs; k++) c->r[k] = args[k];
    c->r[14] = SENTINEL;
    uint32_t pc = addr;
    for (;;) {
        uint32_t a = pc & ~1u;
        if (a == (SENTINEL & ~1u)) break;
        uint32_t idx = (a - TEXT_BASE) >> 1;
        if (a >= TEXT_BASE && idx < gc_text_halfwords) {
            BlockFn fn = gc_blocks[idx];
            if (fn) {
                if (gc_mode[idx] != ((pc & 1u) ? 1 : 2)) gc1_fatal("salto para 0x%08x com modo ARM/Thumb trocado", pc);
                gc_cov[idx] = 1;
                pc = fn(c);
                if (pc == PC_TRAP) gc1_fatal("bloco com instrucao nao suportada em 0x%08x", a);
                continue;
            }
        }
        const GcStub *s = find_stub(a);
        if (s) { gc1_hle_call(s, c); pc = c->r[14]; continue; }
        gc1_fatal("PC 0x%08x fora de qualquer bloco ou stub (lr=0x%08x sp=0x%08x)", pc, c->r[14], c->r[13]);
    }
    uint32_t ret = c->r[0];
    c->r[13] = sp0;
    return ret;
}
