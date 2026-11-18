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
static BlockFn orig_glyph, orig_measure, orig_walk;
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

/* ---------------- mineracao (jazidas, objeto 0xe6) ----------------
 * resolve_chest_minigame (0xfcb4) sorteava UM item por minerada (roll_chest_loot). Agora a minerada solta 4 ou 5 itens (cada um sorteado na mesma tabela
 * e na mesma faixa da barra) e, se houver uma missao que exige item de mineracao em andamento, os primeiros itens sao os da missao
 * (Vamos Minerar: Bijou; mineracao de Cristal; Material Magico / Chama Apagada: Fragmento de Rubi). Cursor fora da barra boa (faixa -1) continua nao soltando nada. */
#define A_RESOLVE_CHEST 0xfcb4u
#define A_ROLL_CHEST    0xfa4cu
#define A_SPAWN_DROP    0xfb2cu
#define A_CLOSE_CHEST   0xfc54u
#define A_RAND_RANGE    0x36da8u
#define G_CHEST_BAR     0x1500054u
#define G_PLAYER_ENT    0x1634268u
#define G_CUR_QUEST     0x162b6e4u
#define G_ACT_QUESTS    0x162a9fcu
#define G_ACT_QCOUNT    0x162b794u
static const struct { uint32_t quest, item; } MINE_QUESTS[] = {
    {0x1f0001u, 0xe0cu},    /* Vamos Minerar (secundaria 1): 1 Bijou */
    {0x1f0064u, 0xe0au},    /* secundaria 100: 1 Cristal */
    {0x1e0040u, 0xe13u},    /* Material Magico (principal 64): 3 Fragmentos de Rubi */
    {0x1f002cu, 0xe13u},    /* Chama Apagada (secundaria 44): 2 Fragmentos de Rubi */
};
static int quest_active(uint32_t id) {
    if (LD32(G_CUR_QUEST) == id) return 1;
    uint32_t n = LD8(G_ACT_QCOUNT); if (n > 50) n = 50;
    for (uint32_t i = 0; i < n; i++) if (LD32(G_ACT_QUESTS + 4 * i) == id) return 1;
    return 0;
}
static uint32_t hook_resolve_chest(Cpu *c) {
    Cpu saved = *c; uint32_t lr = c->r[14];
    uint32_t pos = LD16(G_CHEST_BAR); int band;                    /* mesma conversao barra -> faixa do original */
    if (pos < 0x2a) band = -1;
    else if (pos < 0x3c) band = 0;
    else if (pos > 0x4a && pos < 0x4f) band = 2;
    else if (pos > 0x55) band = (pos > 0x5e) ? -1 : 0;
    else band = 1;
    if (band >= 0) {
        uint32_t ent = LD32(G_PLAYER_ENT); int px = (int16_t)LD16(ent + 0x14), py = (int16_t)LD16(ent + 0x16);
        static const int DX[5] = {0, -16, 16, 0, 0}, DY[5] = {0, 0, 0, 16, -16};
        uint32_t want[4]; int nw = 0;
        for (unsigned i = 0; i < sizeof MINE_QUESTS / sizeof *MINE_QUESTS; i++) if (quest_active(MINE_QUESTS[i].quest)) want[nw++] = MINE_QUESTS[i].item;
        int drops = 4 + (int)gcall(A_RAND_RANGE, 2, 0, 1, 0, 0, 0, 0);
        for (int i = 0; i < drops; i++) {
            uint32_t item = (i < nw) ? want[i] : gcall(A_ROLL_CHEST, 1, (uint32_t)band, 0, 0, 0, 0, 0);
            if (item) gcall(A_SPAWN_DROP, 3, item, (uint32_t)(px + DX[i]), (uint32_t)(py + DY[i]), 0, 0, 0);
        }
    }
    gcall(A_CLOSE_CHEST, 0, 0, 0, 0, 0, 0, 0);
    *c = saved; return lr;
}


/* ---------------- andar na diagonal ----------------
 * O jogo so tem 4 direcoes (um sprite por direcao), entao a diagonal e so de movimento: o rosto continua na direcao principal (a ultima apertada) e,
 * com uma direcao perpendicular segurada, o jogador tambem anda no outro eixo. player_update_walk_anim (0x23fe4) anda 8 px (5 em tile lento) por quadro
 * no eixo principal; aqui, num ciclo de 3 quadros: quadro 0 = os dois eixos, 1 = so o principal, 2 = so o secundario (2 passos por eixo a cada 3 quadros:
 * velocidade ~94% da reta em vez de 141%, e sempre multiplo de 8 px, como o jogo espera para o alinhamento das caminhadas de cena).
 * A colisao do eixo secundario repete o que o jogo faz no principal: tile 8 px a frente (get_tile_type_ahead; 1 e 0xb = parede), monstros
 * (monster_blocks_player_move) e minerio/bau (object_blocks_player_move + can_open_chest). So anda (modo 1), nunca correndo, e so livre (acao 0). GC1_NODIAG=1 desliga. */
#define A_WALK_ANIM     0x23fe4u
#define A_TILE_AHEAD    0xce10u
#define A_MON_BLOCKS    0xc31cu
#define A_OBJ_BLOCKS    0xc50cu
#define A_CAN_OPEN      0xc5c0u
#define G_MOVE_MODE     0x1634b40u
#define G_ENT_LIST      0x1634bf8u
#define G_ENT_COUNT     0x150004cu
#define G_SLOW_TILE     0x150006au
static int g_nodiag, g_diag_tick, g_diagdbg;
static int diag_free(uint32_t ent, int sec) {
    int x = (int16_t)LD16(ent + 0x14), y = (int16_t)LD16(ent + 0x16);
    uint32_t t = gcall(A_TILE_AHEAD, 2, (uint32_t)x, (uint32_t)y, 0, 0, 0, 0) & 0xff;
    if (t == 1 || t == 0xb) return 0;
    int n = (int)LD16(G_ENT_COUNT); if (n > 0x226) n = 0x226;
    for (int i = 0; i < n; i++) {
        uint32_t e = LD32(G_ENT_LIST + 4u * (uint32_t)i); if (!e) continue;
        uint32_t kind = LD8(e + 3);
        if (kind == 1) { if (LD8(e + 5) != 4 && (gcall(A_MON_BLOCKS, 2, e, 1, 0, 0, 0, 0) & 0xff)) return 0; }
        else if (kind == 0 && LD8(e + 2) == 0xe6) { if ((gcall(A_OBJ_BLOCKS, 2, e, 1, 0, 0, 0, 0) & 0xff) && (gcall(A_CAN_OPEN, 1, (uint32_t)i, 0, 0, 0, 0, 0) & 0xff)) return 0; }
    }
    (void)sec; return 1;
}
static uint32_t hook_walk_anim(Cpu *c) {
    Cpu saved = *c; uint32_t lr = c->r[14];
    uint32_t ent = LD32(G_PLAYER_ENT); int sec = -1, p = 0;
    if (ent && !g_nodiag && LD8(G_MOVE_MODE) == 1 && LD8(ent + 8) == 0 && LD8(ent + 5) != 9) { p = (int)LD8(ent + 1); if (p < 4) sec = gc1_diag_secondary(p); }
    uint32_t i0 = (A_WALK_ANIM - TEXT_BASE) >> 1; BlockFn hook = gc_blocks[i0]; gc_blocks[i0] = orig_walk;
    if (sec < 0) { g_diag_tick = 0; gcall(A_WALK_ANIM, 0, 0, 0, 0, 0, 0, 0); gc_blocks[i0] = hook; *c = saved; return lr; }
    int phase = g_diag_tick % 3; g_diag_tick++;
    uint32_t fl = LD32(ent + 0x28) + 0xau + (uint32_t)p, keep = LD8(fl);
    if (phase == 2) ST8(fl, 0);                                    /* quadro so do eixo secundario: o principal nao anda */
    gcall(A_WALK_ANIM, 0, 0, 0, 0, 0, 0, 0); gc_blocks[i0] = hook;
    if (phase == 2) ST8(fl, keep);
    if (phase != 1) {
        ST8(ent + 1, (uint32_t)sec);                               /* as rotinas de colisao do jogo olham a direcao do jogador */
        if (diag_free(ent, sec)) {
            int step = LD8(G_SLOW_TILE) ? 5 : 8;
            if (sec == 0) ST16(ent + 0x16, LD16(ent + 0x16) + (uint32_t)step); else if (sec == 3) ST16(ent + 0x16, LD16(ent + 0x16) - (uint32_t)step);
            else if (sec == 1) ST16(ent + 0x14, LD16(ent + 0x14) + (uint32_t)step); else ST16(ent + 0x14, LD16(ent + 0x14) - (uint32_t)step);
        }
        ST8(ent + 1, (uint32_t)p);
    }
    if (g_diagdbg) {                                               /* depuracao: o jogador nunca deveria ficar em cima de parede */
        uint32_t cm = LD32(0x1633f44u), tl = LD32(cm + 0xc); int w = (int)LD8(cm + 8);
        int tx = (int16_t)LD16(ent + 0x14) >> 4, ty = (int16_t)LD16(ent + 0x16) >> 4; uint32_t t = LD8(tl + 4u * (uint32_t)(ty * w + tx) + 2) & 0x1f;
        if (t == 1 || t == 0xb) fprintf(stderr, "DIAGDBG parede x=%d y=%d tile=%d,%d tipo=%u prim=%d sec=%d\n", (int)(int16_t)LD16(ent + 0x14), (int)(int16_t)LD16(ent + 0x16), tx, ty, t, p, sec);
    }
    *c = saved; return lr;
}

/* ---------------- loja do Dennis (NPC 56, tipo de loja 0xa) ----------------
 * No original ele abria o mercado entre jogadores (rede). Agora o script dele usa o menu de loja (Comprar/Vender/Conversar/Presentear/Sair, igual ao da Marsha;
 * tools/translate/dennis.py) e shop_rebuild_stock (0x315ac) monta o estoque dele: as curas e receitas que nenhuma loja/drop entrega (ids abaixo; abas Cura e
 * Suprimentos, ate 40 por aba), os 5 bilhetes de desafio de chefe (que so saiam dos baus) e os 6 pacotes de armadura. Os precos que estavam zerados sao fixados em
 * ConstData.br por dennis.py. Os outros NPCs seguem o codigo original. */
#define A_SHOP_REBUILD  0x315acu
#define A_SHOP_ADD      0x308e8u
#define G_NPC_SHOP_TYPE 0x162ba6bu
#define G_SHOP_HEAL     0x162b418u
#define G_SHOP_UTIL     0x162dcf0u
static BlockFn orig_shop;
static const uint16_t DENNIS_STOCK[] = {
    0xd07, 0xd15, 0xd16, 0xd17, 0xd18, 0xd1a, 0xd1b, 0xd1c, 0xd1d, 0xd1e,                      /* cura: capsula de SP, comidas, curas de status */
    0xe05, 0xe06, 0xe07, 0xe08, 0xe09,                                                          /* bilhetes dos 5 ultimos chefes da expedicao */
    0xe4f, 0xe50, 0xe51, 0xe52, 0xe53, 0xe54,                                                   /* pacotes de armadura (4 pecas de um set) */
    0xe2a, 0xe4b, 0xe4c, 0xe4d,                                                                 /* receitas */
};
/* Itens que nenhum drop, loja ou baú entregava (eram da loja de cash/mercado ou conteudo cortado; docs/INVESTIGACAO_REDE_ITENS.md, secao 2.2) entram nas lojas de
 * armadura (todas as cidades) e de pets: a loja de armaduras tem 5 abas (Elmo, Armadura, Capa, Luvas, Bota; 40 vagas cada) e o desenho dela serve a rosto e colares/aneis
 * (que vao nas abas Elmo e Capa). Os precos ja estao no ConstData. */
#define G_SHOP_TAB      0x1638544u
#define G_SHOP_EQUIP    0x1633754u
#define G_SHOP_PET      0x1632aa0u
static const uint16_t ARMOR_EXTRA_TAB0[] = { 0x209, 0x213, 0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10a, 0x10b, 0x10c, 0x10d, 0x10e, 0x10f, 0x110, 0x111, 0x112, 0x113 };
static const uint16_t ARMOR_EXTRA_TAB1[] = { 0x709, 0x70c, 0x714, 0x719 };
static const uint16_t ARMOR_EXTRA_TAB2[] = { 0x81a, 0x81b, 0x302, 0x303, 0x304, 0x312, 0x313, 0x314, 0x315 };
static const uint16_t ARMOR_EXTRA_TAB3[] = { 0x909, 0x90f, 0x914 };
static const uint16_t ARMOR_EXTRA_TAB4[] = { 0xa09, 0xa0f, 0xa14 };
static const uint16_t PET_EXTRA[] = { 0xb01, 0xb03, 0xb05 };
static void shop_add_missing(uint32_t base, const uint16_t *ids, unsigned n) {      /* soma ao estoque (40 vagas de 0x28 bytes, id no primeiro u16) o que ainda nao esta la */
    for (unsigned k = 0; k < n; k++) {
        int has = 0; for (uint32_t i = 0; i < 40; i++) if (LD16(base + 0x28 * i) == ids[k]) { has = 1; break; }
        if (!has) gcall(A_SHOP_ADD, 4, ids[k], 1, 0xffffffffu, 0, 0, 0);
    }
}
static uint32_t hook_shop_rebuild(Cpu *c) {
    Cpu saved = *c; uint32_t lr = c->r[14];
    if (LD8(G_NPC_SHOP_TYPE) == 0x0a) {
        for (uint32_t i = 0; i < 40; i++) { ST16(G_SHOP_HEAL + 0x10 * i, 0); ST16(G_SHOP_UTIL + 0x10 * i, 0); }
        for (unsigned i = 0; i < sizeof DENNIS_STOCK / sizeof *DENNIS_STOCK; i++) gcall(A_SHOP_ADD, 4, DENNIS_STOCK[i], 1, 0xffffffffu, 0, 0, 0);
    } else {
        uint32_t i0 = (A_SHOP_REBUILD - TEXT_BASE) >> 1; BlockFn hook = gc_blocks[i0]; gc_blocks[i0] = orig_shop;
        gcall(A_SHOP_REBUILD, 0, 0, 0, 0, 0, 0, 0); gc_blocks[i0] = hook;
        uint32_t type = LD8(G_NPC_SHOP_TYPE), tab = LD8(G_SHOP_TAB);
        if (type == 4) {
#define EXTRA(n) shop_add_missing(G_SHOP_EQUIP, ARMOR_EXTRA_TAB##n, sizeof ARMOR_EXTRA_TAB##n / sizeof *ARMOR_EXTRA_TAB##n)
            switch (tab) { case 0: EXTRA(0); break; case 1: EXTRA(1); break; case 2: EXTRA(2); break; case 3: EXTRA(3); break; case 4: EXTRA(4); break; default: break; }
#undef EXTRA
        } else if (type == 5) shop_add_missing(G_SHOP_PET, PET_EXTRA, sizeof PET_EXTRA / sizeof *PET_EXTRA);
        if (getenv("GC1_SHOPDBG") && (type == 4 || type == 5)) {        /* depuracao: lista o estoque montado */
            uint32_t base = type == 4 ? G_SHOP_EQUIP : G_SHOP_PET; fprintf(stderr, "SHOPDBG tipo=%u aba=%u:", type, tab);
            for (uint32_t i = 0; i < 40; i++) if (LD16(base + 0x28 * i)) fprintf(stderr, " %x", LD16(base + 0x28 * i));
            fputc(10, stderr);
        }
    }
    *c = saved; return lr;
}


/* ---------------- nome do monstro: buffer de 20 bytes ----------------
 * load_monster_name (0x14420) copia o nome inteiro para monster_name_buf (0x162b731, 20 bytes) sem checar o tamanho. O coreano cabia; em PT "Capitao Arqueiro
 * Esqueleto" (26) pisava em dialog_active (0x162b745), save_slot_index (+1) e boss_warning_active (+2): a caixa de aviso nao fechava mais e os saves iam para o slot errado.
 * Aqui os bytes depois do buffer sao guardados e devolvidos, e o nome e cortado em 19 letras + NUL. */
#define A_MONSTER_NAME 0x14420u
#define G_MONSTER_NAME 0x162b731u
static BlockFn orig_monname;
static uint32_t hook_monster_name(Cpu *c) {
    Cpu saved = *c; uint32_t lr = c->r[14]; uint8_t after[256];
    memcpy(after, G + G_MONSTER_NAME + 20, sizeof after);
    uint32_t i0 = (A_MONSTER_NAME - TEXT_BASE) >> 1; BlockFn hook = gc_blocks[i0]; gc_blocks[i0] = orig_monname;
    gcall(A_MONSTER_NAME, 1, saved.r[0], 0, 0, 0, 0, 0); gc_blocks[i0] = hook;
    memcpy(G + G_MONSTER_NAME + 20, after, sizeof after); G[G_MONSTER_NAME + 19] = 0;
    *c = saved; return lr;
}

/* ---------------- curva de EXP ----------------
 * calc_exp_to_next_level (0x96f0) devolve o EXP para subir do nivel L: g(L)-g(L-1), g(L) = ((500L + 2500L/11) * L^2 / 10) / 2 (~109*L^2).
 * O EXP dos monstros e linear (~40L + 0.29L^2), entao no original o numero de mortes por nivel sobe sem parar (55 no Lv25, 100 no Lv50, 170 no Lv120).
 * Aqui o custo e limitado a G_XP_KILLS mortes de um monstro do mesmo nivel (padrao 25): min(original, kills * (40L + 0.29L^2)). So afeta o jogador (o pet usa
 * exp_required_for_level) e nao muda o EXP de missoes/monstros. GC1_XPKILLS=n troca o alvo; 0 devolve a curva original. */
#define A_EXP_TO_NEXT 0x96f0u
#define G_PLAYER_LEVEL 0x1632a1du
#define G_EXP_TO_NEXT  0x162c438u
static int g_xpkills = 25, g_xpon;
static int64_t exp_g(int64_t l) { return (((500 * l + (l * 2500) / 11) * l * l) / 10) >> 1; }
static uint32_t exp_for_level(uint32_t l) {
    int64_t orig = exp_g(l) - exp_g((int64_t)l - 1);
    if (g_xpkills > 0) { int64_t cap = (int64_t)g_xpkills * (4000 * (int64_t)l + 29 * (int64_t)l * l) / 100; if (cap < orig) orig = cap; }
    return (uint32_t)orig;
}
static uint32_t hook_exp_to_next(Cpu *c) {
    uint32_t lr = c->r[14]; c->r[0] = exp_for_level(LD8(G_PLAYER_LEVEL)); return lr;
}
/* gain_exp_and_level_up (0x114fc) so sobe ate 2 niveis por chamada e, no 2o nivel, deixa o EXP que sobra com o SINAL TROCADO (player_exp = need - resto):
 * com a curva original quase nunca acontecia (precisa de ganho > 1 nivel), com niveis baratos e comum (missao/chefe valem 1-2 niveis). Aqui o original roda,
 * o sinal e corrigido e, se ainda sobrar EXP para outro nivel (inclusive EXP acumulado antes da curva nova), ele roda de novo com ganho 0 ate esvaziar. */
#define A_GAIN_EXP    0x114fcu
#define G_PLAYER_EXP  0x162c434u
static BlockFn orig_gain;
static uint32_t hook_gain_exp(Cpu *c) {
    Cpu saved = *c; uint32_t lr = c->r[14];
    uint32_t i0 = (A_GAIN_EXP - TEXT_BASE) >> 1; BlockFn hook = gc_blocks[i0]; gc_blocks[i0] = orig_gain;
    gcall(A_GAIN_EXP, 2, saved.r[0], saved.r[1], 0, 0, 0, 0);
    for (int k = 0; k < 16; k++) {
        int32_t e = (int32_t)LD32(G_PLAYER_EXP);
        if (e < 0) { e = -e; ST32(G_PLAYER_EXP, (uint32_t)e); }
        if (LD8(G_PLAYER_LEVEL) >= 250 || LD32(G_EXP_TO_NEXT) == 0 || (uint32_t)e < LD32(G_EXP_TO_NEXT)) break;
        gcall(A_GAIN_EXP, 2, 0xffffffffu, 0, 0, 0, 0, 0);
    }
    gc_blocks[i0] = hook; *c = saved; return lr;
}
static void refresh_exp_to_next(void) {                      /* foto de estado antiga: a barra de EXP guarda o valor da curva velha */
    uint32_t l = LD8(G_PLAYER_LEVEL);
    if (g_xpon && l >= 1 && l < 250 && LD32(G_EXP_TO_NEXT) != 0) ST32(G_EXP_TO_NEXT, exp_for_level(l));
}

static BlockFn install(uint32_t addr, BlockFn fn) { uint32_t i = (addr - TEXT_BASE) >> 1; BlockFn o = NULL; if (i < gc_text_halfwords && gc_blocks[i]) { o = gc_blocks[i]; gc_blocks[i] = fn; } return o; }
/* Fotos de estado gravadas enquanto o nome do monstro estourava o buffer (ver hook_monster_name) trazem flags e slot de save corrompidos; limpa o que e so flag/slot. */
static void sanitize_overflow(void) {
    if (LD8(0x162b745) > 1) ST8(0x162b745, 0);                   /* dialog_active */
    if (LD8(0x162b746) > 2) ST8(0x162b746, 0);                   /* save_slot_index */
    for (uint32_t a = 0x162b747; a <= 0x162b749; a++) if (LD8(a) > 1) ST8(a, 0);   /* boss_warning_active, screen_shake_active, screen_overlay_active */
    if (LD8(0x162b748) == 0) ST8(0x162b74a, 0);                  /* duracao do tremor (so vale com o tremor ligado) */
}
void gc1_patches_reset(void) { g_acc_img[3] = 0; sanitize_overflow(); refresh_exp_to_next(); }          /* depois de carregar um save state: a folha de acentos (no heap do guest) pode nao existir mais */
void gc1_patches_install(void) {
    if (g_patched) return; g_patched = 1;
    if (getenv("GC1_NOPATCH")) return;                       /* depuracao: roda o motor original */
    install(A_TEXT_WRAPPED, hook_text_draw_wrapped);
    orig_glyph = install(A_TEXT_DRAW_GLYPH, hook_text_draw_glyph);
    orig_measure = install(A_TEXT_MEASURE, hook_text_measure);
    install(A_RESOLVE_CHEST, hook_resolve_chest);
    g_nodiag = getenv("GC1_NODIAG") != NULL; g_diagdbg = getenv("GC1_DIAGDBG") != NULL;
    orig_walk = install(A_WALK_ANIM, hook_walk_anim);
    orig_shop = install(A_SHOP_REBUILD, hook_shop_rebuild);
    orig_monname = install(A_MONSTER_NAME, hook_monster_name);
    if (getenv("GC1_XPKILLS")) g_xpkills = atoi(getenv("GC1_XPKILLS"));
    g_xpon = install(A_EXP_TO_NEXT, hook_exp_to_next) != NULL;
    orig_gain = install(A_GAIN_EXP, hook_gain_exp);
}
