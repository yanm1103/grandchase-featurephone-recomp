/* rt.h - ABI do codigo gerado pelo lifter (ARM/Thumb -> C) do Grand Chase 1.
 *
 * Um "bloco" e uma funcao C que executa um trecho linear de instrucoes e devolve o proximo PC
 * (bit0 = 1 -> Thumb). O trampolim em cpu.c chama blocos ate o PC ser um endereco especial (stub de import, sentinela...).
 * Memoria do guest: um array plano G; endereco guest a = G[a]. x86 e little-endian como o ARM do jogo.
 */
#ifndef GC1_RT_H
#define GC1_RT_H
#include <stdint.h>
#include <string.h>

typedef struct Cpu {
    uint32_t r[16];                 /* r0-r12, sp=r13, lr=r14; r15 nao e usado (PC e o valor de retorno do bloco) */
    uint8_t N, Z, C, V;
} Cpu;

typedef uint32_t (*BlockFn)(Cpu *);

extern uint8_t *G;                  /* memoria plana do guest */
#define MEM_SIZE 0x4000000u         /* 64 MiB */

#ifdef GC1_CHECK_MEM
void gc1_bad_access(uint32_t a, int sz, int wr, uint32_t pc);
#define CHK(a, sz, wr) do { if ((uint32_t)(a) > MEM_SIZE - (sz)) gc1_bad_access((a), (sz), (wr), 0); } while (0)
#else
#define CHK(a, sz, wr) ((void)0)
#endif

static inline uint32_t LD32(uint32_t a) { uint32_t v; CHK(a, 4, 0); memcpy(&v, G + a, 4); return v; }
static inline uint32_t LD16(uint32_t a) { uint16_t v; CHK(a, 2, 0); memcpy(&v, G + a, 2); return v; }
static inline uint32_t LD8(uint32_t a)  { CHK(a, 1, 0); return G[a]; }
static inline void ST32(uint32_t a, uint32_t v) { CHK(a, 4, 1); memcpy(G + a, &v, 4); }
static inline void ST16(uint32_t a, uint32_t v) { uint16_t x = (uint16_t)v; CHK(a, 2, 1); memcpy(G + a, &x, 2); }
static inline void ST8(uint32_t a, uint32_t v)  { CHK(a, 1, 1); G[a] = (uint8_t)v; }

/* --- flags: N Z C V sao variaveis locais do bloco (uint8_t) ---------------------------------------- */
#define SETNZ(res)  do { N = (uint8_t)((uint32_t)(res) >> 31); Z = ((uint32_t)(res) == 0); } while (0)
/* res = a + b + cin ; atualiza N Z C V */
#define ADDF(res, a, b, cin) do { uint32_t _a = (a), _b = (b); uint64_t _t = (uint64_t)_a + _b + (cin); \
        (res) = (uint32_t)_t; C = (uint8_t)(_t >> 32); V = (uint8_t)(((~(_a ^ _b)) & (_a ^ (uint32_t)_t)) >> 31); SETNZ((uint32_t)_t); } while (0)
/* res = a - b - !cin  (= a + ~b + cin) */
#define SUBF(res, a, b, cin) do { uint32_t _a = (a), _b = (b); uint64_t _t = (uint64_t)_a + (uint32_t)~_b + (cin); \
        (res) = (uint32_t)_t; C = (uint8_t)(_t >> 32); V = (uint8_t)(((_a ^ _b) & (_a ^ (uint32_t)_t)) >> 31); SETNZ((uint32_t)_t); } while (0)

/* deslocamentos com carry (so atualizam C; N/Z o chamador faz com SETNZ) --- n = quantidade (0..255) */
#define LSLC(res, v, n) do { uint32_t _v = (v), _n = (n); \
        if (_n == 0) (res) = _v; else if (_n < 32) { C = (uint8_t)((_v >> (32 - _n)) & 1); (res) = _v << _n; } \
        else if (_n == 32) { C = (uint8_t)(_v & 1); (res) = 0; } else { C = 0; (res) = 0; } } while (0)
#define LSRC(res, v, n) do { uint32_t _v = (v), _n = (n); \
        if (_n == 0) (res) = _v; else if (_n < 32) { C = (uint8_t)((_v >> (_n - 1)) & 1); (res) = _v >> _n; } \
        else if (_n == 32) { C = (uint8_t)(_v >> 31); (res) = 0; } else { C = 0; (res) = 0; } } while (0)
#define ASRC(res, v, n) do { uint32_t _v = (v), _n = (n); \
        if (_n == 0) (res) = _v; else if (_n < 32) { C = (uint8_t)(((int32_t)_v >> (_n - 1)) & 1); (res) = (uint32_t)((int32_t)_v >> _n); } \
        else { C = (uint8_t)(_v >> 31); (res) = (uint32_t)((int32_t)_v >> 31); } } while (0)
#define RORC(res, v, n) do { uint32_t _v = (v), _n = (n); \
        if (_n == 0) (res) = _v; else { uint32_t _m = _n & 31; if (_m == 0) { C = (uint8_t)(_v >> 31); (res) = _v; } \
        else { C = (uint8_t)((_v >> (_m - 1)) & 1); (res) = (_v >> _m) | (_v << (32 - _m)); } } } while (0)

/* PC "especial" devolvido por um bloco que nao pode ser executado (instrucao nao suportada) */
#define PC_TRAP 0xfffffff1u

#endif
