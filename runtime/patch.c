/* patch.c - correcoes do motor do jogo feitas no recomp (substituem rotinas do guest por C).
 * Um patch troca o bloco de entrada de uma funcao em gc_blocks[]; o codigo original continua intacto (e e chamado de dentro do patch quando precisa).
 *  - text_draw_wrapped (0x386c4): quebra de linha por PALAVRA (o original corta a palavra no ponto onde estoura a largura).
 *  - ACENTOS: a fonte do jogo so tem 0-9 A-Z a-z. Os bytes 0x80..0xA3 do texto passam intactos por text_to_utf16 (viram U+0080..U+00A3), entao usamos
 *    24 deles como letras acentuadas (tabela ACC abaixo; tools/translate/charset.py tem a mesma). text_draw_glyph desenha esses codigos com uma folha
 *    de glifos montada na inicializacao (letra base da propria fonte + sinal), e text_measure_width conta 6 px para eles (o original contaria 7).
 * Os enderecos sao do binary.mod original (nomes em GrandChase1-decomp). */
#include <stdlib.h>
#include <string.h>
#include "gc1.h"

#define A_TEXT_TO_UTF16   0x375ccu
#define A_TEXT_MEASURE    0x379d0u
#define A_TEXT_DRAW_GLYPH 0x37ee8u
#define A_TEXT_WRAPPED    0x386c4u
#define G_UTF16_BUF 0x163db3cu      /* ponteiro para o buffer UTF-16 (2000 bytes) */
#define G_UTF16_LEN 0x163dba8u
#define G_ESC_STATE 0x1629938u
#define G_ADV_SCALE 0x163db86u
#define NACC 24

static FILE *g_wraplog; static int g_patched;
static BlockFn orig_glyph, orig_measure;
void gc1_wraplog(FILE *f) { g_wraplog = f; }

static uint32_t thumb_bit(uint32_t a) { uint32_t i = (a - TEXT_BASE) >> 1; return (i < gc_text_halfwords && gc_mode[i] == 1) ? 1u : 0u; }
static uint32_t gcall(uint32_t a, int n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    uint32_t args[6] = {a0, a1, a2, a3, a4, a5}; return gc1_call(a | thumb_bit(a), n, args);
}
static int rd16(uint32_t buf, int off) { return (int)LD16(buf + (uint32_t)off); }

/* largura de um caractere como em text_measure_width (hangul 10, jamo 9, alfanumerico 6, espaco 4, resto 7) */
static int cw(int ch) {
    if ((unsigned)(ch - 0xac00) < 0x2ba4) return 10;
    if ((unsigned)(ch - 0x3131) < 0x33) return 9;
    if ((unsigned)(ch - 0x30) < 10 || (unsigned)(ch - 0x61) < 26 || (unsigned)(ch - 0x41) < 26) return 6;
    if (ch == 0x20) return 4;
    if ((unsigned)(ch - 0x80) < NACC) return 6;                        /* letras acentuadas (ver abaixo) */
    return 7;
}
static int is_color_esc(uint32_t buf, int i, int len) {       /* '@N' = troca de cor, largura 0 */
    if (rd16(buf, i) != 0x40 || i + 2 >= len) return 0;
    int n = rd16(buf, i + 2); return n == 0x31 || n == 0x32 || n == 0x33 || (n >= 0x35 && n <= 0x39);
}
static int word_width(uint32_t buf, int i, int len) {          /* largura da palavra que comeca em i (ate espaco, '|', '/' ou fim) */
    int w = 0;
    while (i < len) {
        int ch = rd16(buf, i);
        if (ch == 0x20 || ch == 0x7c || ch == 0x2f || ch == 0) break;
        if (is_color_esc(buf, i, len)) { i += 4; continue; }
        w += cw(ch); i += 2;
    }
    return w;
}
static void log_cut(uint32_t text) {
    if (!g_wraplog) return;
    static uint32_t seen[4096]; static int ns;
    uint32_t h = 2166136261u; for (const uint8_t *p = G + text; *p; p++) h = (h ^ *p) * 16777619u;
    for (int i = 0; i < ns; i++) if (seen[i] == h) return;
    if (ns < 4096) seen[ns++] = h;
    fprintf(g_wraplog, "t=%llu CORTE: ", (unsigned long long)gc1_get_time()); for (const uint8_t *p = G + text; *p && p - (G + text) < 300; p++) fputc(*p >= 0x20 && *p < 0x7f ? *p : '?', g_wraplog);
    fputc(10, g_wraplog); fflush(g_wraplog);
}

/* (texto, x, y, alinhamento, largura) -> numero de linhas; mesmo contrato do original, mas quebrando por palavras */
static uint32_t hook_text_draw_wrapped(Cpu *c) {
    Cpu saved = *c;
    uint32_t text = c->r[0], x = c->r[1], y = c->r[2], align = c->r[3], lr = c->r[14]; int width = (int32_t)LD32(c->r[13]);
    gcall(A_TEXT_TO_UTF16, 1, text, 0, 0, 0, 0, 0);
    uint32_t buf = LD32(G_UTF16_BUF); int len = (int32_t)LD32(G_UTF16_LEN);
    int total = (int32_t)gcall(A_TEXT_MEASURE, 2, buf, (uint32_t)len, 0, 0, 0, 0);
    ST8(G_ESC_STATE, 0);
    int lines = 1, lx = 0, ly = 0, lyacc = 0;
    if (width < total) {
        int midword = 0;
        for (int i = 0; i < len; i += 2) {
            int ch = rd16(buf, i);
            if (ch == 0x7c || ch == 0x2f) {                                  /* quebra forcada */
                lx = 0; if (i == len - 2) break;
                lyacc += 15; lines++; ly = lyacc; continue;
            }
            if (ch == 0x20 && lx > 0 && i + 2 < len) {                       /* espaco: ve se a proxima palavra cabe */
                int ww = word_width(buf, i + 2, len);
                if (lx + 4 + ww > width) { lx = 0; lyacc += 15; lines++; ly = lyacc; continue; }   /* quebra aqui e engole o espaco */
            }
            lx = (int32_t)gcall(A_TEXT_DRAW_GLYPH, 6, buf, (uint32_t)i, x, y - 10, (uint32_t)lx, (uint32_t)ly);
            if (lx > width) {                                                /* palavra maior que a linha: cai no corte por caractere (como o original) */
                midword = 1; lx = 0; if (i == len - 2) break;
                lyacc += 15; lines++; ly = lyacc;
            }
        }
        if (midword) log_cut(text);
        else if (g_wraplog) {                                                /* o original cortaria alguma palavra no meio? (so diagnostico) */
            int ox = 0, i = 0; for (; i < len; i += 2) { int ch = rd16(buf, i); if (ch == 0x7c || ch == 0x2f) { ox = 0; continue; } ox += cw(ch); if (ox > width) { if (i + 2 < len && ch != 0x20 && rd16(buf, i + 2) != 0x20) log_cut(text); break; } }
        }
    } else {
        if (align == 1) x -= (uint32_t)total; else if (align == 2) x -= (uint32_t)(total >> 1);
        for (int i = 0; i < len; i += 2) lx = (int32_t)gcall(A_TEXT_DRAW_GLYPH, 6, buf, (uint32_t)i, x, y - 10, (uint32_t)lx, 0);
    }
    *c = saved; c->r[0] = (uint32_t)lines; return lr;
}


/* ---------------- acentos ---------------- */
#define A_BLIT_SIL 0x27a8u
#define G_LATIN_IMG 0x163db74u                 /* PbpImage 0x10: w,h(u16) | ultimo indice de paleta | paleta | pixels */
/* codigo (0x80+i): letra base, sinal (a agudo, g grave, c circunflexo, t til, d cedilha) */
static const struct { char base, mark; } ACC[NACC] = {
    {'a','a'},{'e','a'},{'i','a'},{'o','a'},{'u','a'},{'a','c'},{'e','c'},{'o','c'},{'a','t'},{'o','t'},{'a','g'},{'c','d'},
    {'A','a'},{'E','a'},{'I','a'},{'O','a'},{'U','a'},{'A','c'},{'E','c'},{'O','c'},{'A','t'},{'O','t'},{'A','g'},{'C','d'} };
#define CELL_H 14                              /* 3 linhas acima (sinal de 2 linhas + folga) + 9 da letra + 2 de cedilha */
static uint32_t g_acc_img[4];                  /* os 4 argumentos de imagem de blit_silhouette_clipped para a folha de acentos */
static const char *MARK_TOP[3][2] = { /* a */ {"...#.", "..#.."}, /* g */ {".#...", "..#.."}, /* c */ {"..#..", ".#.#."} };
static const char *MARK_TIL[2] = { ".##.#", "#..#." };
static const char *MARK_CED[2] = { "..#..", ".#..." };
static void build_accent_sheet(void) {
    uint32_t pix = LD32(G_LATIN_IMG + 12); if (!pix) return;
    uint32_t sheet = gc1_guest_alloc(5 * CELL_H * NACC); uint8_t *dst = G + sheet; memset(dst, 0, 5 * CELL_H * NACC);
    for (int k = 0; k < NACC; k++) {
        char b = ACC[k].base; int gi = (b >= 'a') ? (b - 'a' + 36) : (b - 'A' + 10); const uint8_t *src = G + pix + gi * 9 * 5;
        uint8_t *cell = dst + k * 5 * CELL_H;
        for (int r = 0; r < 9; r++) for (int x = 0; x < 5; x++) cell[(r + 3) * 5 + x] = src[r * 5 + x];
        if (b == 'i') for (int x = 0; x < 5; x++) cell[(0 + 3) * 5 + x] = 0;               /* 'i' perde o pingo: o acento ocupa o lugar */
        int top = (b >= 'a') ? 2 : 0;                                                          /* sinal em 2 linhas com 1 de folga ate a letra: minusculas (altura-x comeca na linha 2 da letra) na linha -1/0; maiusculas acima da letra */
        const char *r0 = NULL, *r1 = NULL;
        switch (ACC[k].mark) { case 'a': r0 = MARK_TOP[0][0]; r1 = MARK_TOP[0][1]; break; case 'g': r0 = MARK_TOP[1][0]; r1 = MARK_TOP[1][1]; break;
                               case 'c': r0 = MARK_TOP[2][0]; r1 = MARK_TOP[2][1]; break; case 't': r0 = MARK_TIL[0]; r1 = MARK_TIL[1]; break; default: break; }
        if (r0) for (int x = 0; x < 5; x++) { if (r0[x] == '#') cell[(top - 0) * 5 + x] = 1; if (r1[x] == '#') cell[(top + 1) * 5 + x] = 1; }
        if (ACC[k].mark == 'd') for (int x = 0; x < 5; x++) { if (MARK_CED[0][x] == '#') cell[(3 + 9) * 5 + x] = 1; if (MARK_CED[1][x] == '#') cell[(3 + 10) * 5 + x] = 1; }
    }
    g_acc_img[0] = 5u | ((uint32_t)(CELL_H * NACC) << 16); g_acc_img[1] = LD32(G_LATIN_IMG + 4); g_acc_img[2] = LD32(G_LATIN_IMG + 8); g_acc_img[3] = sheet;
}
/* (buffer, indice, x0, y0, cursor, linha) -> novo cursor. So interceptamos os codigos acentuados; o resto segue para o original sem custo. */
static uint32_t hook_text_draw_glyph(Cpu *c) {
    uint32_t buf = c->r[0]; int idx = (int32_t)c->r[1];
    int ch = rd16(buf, idx);
    if ((unsigned)(ch - 0x80) >= NACC || LD8(G_ESC_STATE) == 1 || LD8(G_ESC_STATE) == 3) return orig_glyph(c);
    if (!g_acc_img[3]) { build_accent_sheet(); if (!g_acc_img[3]) return orig_glyph(c); }      /* a fonte so existe depois de startClet: monta na primeira vez */
    Cpu saved = *c; uint32_t x0 = c->r[2], y0 = c->r[3], cur = LD32(c->r[13]), line = LD32(c->r[13] + 4), lr = c->r[14];
    uint32_t args[10] = { g_acc_img[0], g_acc_img[1], g_acc_img[2], g_acc_img[3], x0 + cur, y0 + line + 1 - 3, 5, CELL_H, 0, (uint32_t)((ch - 0x80) * CELL_H) };
    gc1_call(A_BLIT_SIL | thumb_bit(A_BLIT_SIL), 10, args);
    *c = saved; c->r[0] = cur + 6u * LD8(G_ADV_SCALE); return lr;
}
/* text_measure_width: o original conta 7 px para os codigos 0x80..0xA3; aqui sao letras (6 px) */
static uint32_t hook_text_measure(Cpu *c) {
    uint32_t buf = c->r[0]; int len = (int32_t)c->r[1]; Cpu saved = *c; uint32_t lr = c->r[14];
    uint32_t i0 = (A_TEXT_MEASURE - TEXT_BASE) >> 1; BlockFn hook = gc_blocks[i0]; gc_blocks[i0] = orig_measure;
    uint32_t w = gcall(A_TEXT_MEASURE, 2, buf, (uint32_t)len, 0, 0, 0, 0); gc_blocks[i0] = hook;
    int n = 0; for (int i = 0; i + 1 < len; i += 2) if ((unsigned)(rd16(buf, i) - 0x80) < NACC) n++;
    *c = saved; c->r[0] = w - (uint32_t)n * LD8(G_ADV_SCALE); return lr;
}

static BlockFn install(uint32_t addr, BlockFn fn) { uint32_t i = (addr - TEXT_BASE) >> 1; BlockFn o = NULL; if (i < gc_text_halfwords && gc_blocks[i]) { o = gc_blocks[i]; gc_blocks[i] = fn; } return o; }
void gc1_patches_reset(void) { g_acc_img[3] = 0; }          /* depois de carregar um save state: a folha de acentos (no heap do guest) pode nao existir mais */
void gc1_patches_install(void) {
    if (g_patched) return; g_patched = 1;
    if (getenv("GC1_NOPATCH")) return;                       /* depuracao: roda o motor original */
    install(A_TEXT_WRAPPED, hook_text_draw_wrapped);
    orig_glyph = install(A_TEXT_DRAW_GLYPH, hook_text_draw_glyph);
    orig_measure = install(A_TEXT_MEASURE, hook_text_measure);
}
