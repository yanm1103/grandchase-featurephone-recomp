/* hle.c - reimplementacao em C das chamadas ao aparelho (WIPI-C do LGT + libc) usadas pelo Grand Chase 1.
 * A semantica segue o emulador WIE (ref/wie: wie-lgt/src/runtime, wie-wipi-c/src/api). Os 67 imports realmente
 * referenciados pelo jogo estao aqui; os outros 83 stubs do binario nao sao chamados (se forem, o runtime para). */
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include "gc1.h"
#include "audio_policy.h"

FILE *gc1_trace;
void gc1_fatal(const char *fmt, ...);

/* ---------------------------------------------------------------------------------------------------- */
/* estado do runtime                                                                                      */
/* ---------------------------------------------------------------------------------------------------- */
static char g_resdir[512], g_savedir[512];
static uint64_t g_now;                       /* ms */
static int g_exit;
static uint32_t g_clet[6];                   /* start, pause, resume, destroy, paint, handleEvent */
static uint32_t g_disp_w = 240, g_disp_h = 320, g_annunc;
static uint32_t g_fb_buf, g_fb_handle;       /* framebuffer da tela (guest) */

static inline uint32_t arg(Cpu *c, int n) { return n < 4 ? c->r[n] : LD32(c->r[13] + 4u * (n - 4)); }
static inline const char *gstr(uint32_t a) { return (const char *)(G + a); }

/* ---------------------------------------------------------------------------------------------------- */
/* alocador do heap do guest: first-fit com coalescencia; cabecalho de 8 bytes ([tamanho][0]) antes do payload */
/* ---------------------------------------------------------------------------------------------------- */
typedef struct { uint32_t addr, size; uint8_t used; } Blk;     /* addr/size do bloco inteiro (com cabecalho) */
static Blk *blk; static int nblk, capblk;

static void heap_init(void) {
    capblk = 4096; blk = (Blk *)malloc(sizeof(Blk) * capblk);
    blk[0].addr = HEAP_BASE; blk[0].size = HEAP_END - HEAP_BASE; blk[0].used = 0; nblk = 1;
}
static uint32_t heap_alloc(uint32_t size) {
    uint32_t need = ((size + 8 + 7) & ~7u); if (need < 16) need = 16;
    for (int i = 0; i < nblk; i++) {
        if (blk[i].used || blk[i].size < need) continue;
        if (blk[i].size >= need + 16) {
            if (nblk == capblk) { capblk *= 2; blk = (Blk *)realloc(blk, sizeof(Blk) * capblk); }
            memmove(&blk[i + 2], &blk[i + 1], sizeof(Blk) * (nblk - i - 1)); nblk++;
            blk[i + 1].addr = blk[i].addr + need; blk[i + 1].size = blk[i].size - need; blk[i + 1].used = 0;
            blk[i].size = need;
        }
        blk[i].used = 1;
        ST32(blk[i].addr, size); ST32(blk[i].addr + 4, 0);
        return blk[i].addr + 8;
    }
    gc1_fatal("heap esgotado ao alocar %u bytes", size);
    return 0;
}
static void heap_free(uint32_t p) {
    if (!p) return;
    uint32_t a = p - 8;
    int lo = 0, hi = nblk - 1, i = -1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (blk[m].addr == a) { i = m; break; } if (blk[m].addr < a) lo = m + 1; else hi = m - 1; }
    if (i < 0 || !blk[i].used) { if (gc1_trace) fprintf(gc1_trace, "! free invalido %08x\n", p); return; }
    blk[i].used = 0;
    if (i + 1 < nblk && !blk[i + 1].used) { blk[i].size += blk[i + 1].size; memmove(&blk[i + 1], &blk[i + 2], sizeof(Blk) * (nblk - i - 2)); nblk--; }
    if (i > 0 && !blk[i - 1].used) { blk[i - 1].size += blk[i].size; memmove(&blk[i], &blk[i + 1], sizeof(Blk) * (nblk - i - 1)); nblk--; }
}

/* ---------------------------------------------------------------------------------------------------- */
/* timers (fila ordenada por vencimento, estavel)                                                         */
/* ---------------------------------------------------------------------------------------------------- */
typedef struct { uint32_t ptr_timer, fn, param, arg0; uint64_t due; uint64_t seq; } Timer;
static Timer tmr[256]; static int ntmr; static uint64_t tmr_seq;

static void timer_cancel(uint32_t ptr_timer) {
    for (int i = 0; i < ntmr; i++) if (tmr[i].ptr_timer == ptr_timer && ptr_timer) { memmove(&tmr[i], &tmr[i + 1], sizeof(Timer) * (ntmr - i - 1)); ntmr--; return; }
}
static int timer_pending(uint32_t ptr_timer) { for (int i = 0; i < ntmr; i++) if (tmr[i].ptr_timer == ptr_timer) return 1; return 0; }
static void timer_add(uint32_t ptr_timer, uint32_t fn, uint32_t param, uint32_t arg0, uint64_t due) {
    if (ntmr == 256) gc1_fatal("fila de timers cheia");
    tmr[ntmr++] = (Timer){ptr_timer, fn, param, arg0, due, tmr_seq++};
}
static uint64_t g_raw, g_off;      /* relogio da plataforma e deslocamento (o save state continua o relogio do jogo de onde parou) */
void gc1_set_time(uint64_t ms) { g_raw = ms; g_now = ms - g_off; }
int64_t gc1_next_due(void) {
    if (!ntmr) return -1;
    uint64_t d = tmr[0].due; for (int i = 1; i < ntmr; i++) if (tmr[i].due < d) d = tmr[i].due;
    return (int64_t)(d + g_off);        /* no relogio da plataforma */
}
static int g_smart; static void smart_tick(void);
int gc1_run_timers(void) {
    int n = 0;
    for (;;) {
        int best = -1;
        for (int i = 0; i < ntmr; i++)
            if (tmr[i].due <= g_now && (best < 0 || tmr[i].due < tmr[best].due || (tmr[i].due == tmr[best].due && tmr[i].seq < tmr[best].seq))) best = i;
        if (best < 0) break;
        Timer t = tmr[best]; memmove(&tmr[best], &tmr[best + 1], sizeof(Timer) * (ntmr - best - 1)); ntmr--;
        if (gc1_trace) fprintf(gc1_trace, "timer fn=%08x p0=%08x p1=%08x\n", t.fn, t.ptr_timer ? t.ptr_timer : t.arg0, t.param);
        uint32_t a[2] = {t.ptr_timer ? t.ptr_timer : t.arg0, t.param};
        gc1_call(t.fn, 2, a); n++;
        if (g_exit) break;
    }
    if (n && g_smart) smart_tick();
    return n;
}
uint32_t gc1_guest_alloc(uint32_t n) { return heap_alloc(n); }
uint64_t gc1_get_time(void) { return g_now; }
int gc1_should_exit(void) { return g_exit; }

/* ---------------------------------------------------------------------------------------------------- */
/* sprintf do WIPI (subconjunto do WIE: %d %u %x %c %s %%, flag 0, largura, l/ll)                        */
/* ---------------------------------------------------------------------------------------------------- */
static int wipi_sprintf(char *out, int cap, const char *fmt, const uint32_t *args, int nargs) {
    int n = 0, ai = 0;
#define PUT(ch) do { if (n < cap - 1) out[n] = (ch); n++; } while (0)
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUT(*p); continue; }
        const char *spec = p; int zero = 0, width = 0, longs = 0; p++;
        for (;; p++) {
            char ch = *p;
            if (!ch) { for (const char *q = spec; q < p; q++) PUT(*q); p--; break; }
            if (ch == '%') { PUT('%'); break; }
            if (ch == 'l') { longs++; continue; }
            if (ch == '0' && width == 0 && !zero) { zero = 1; continue; }
            if (ch >= '0' && ch <= '9') { width = width * 10 + (ch - '0'); if (width > 4096) width = 4096; continue; }
            if (ch == 'd' || ch == 'u' || ch == 'x') {
                uint64_t raw;
                if (longs >= 2) { uint64_t lo = ai < nargs ? args[ai++] : 0, hi = ai < nargs ? args[ai++] : 0; raw = (hi << 32) | lo; }
                else raw = ai < nargs ? args[ai++] : 0;
                char tmp[40];
                if (ch == 'd') { int64_t v = longs >= 2 ? (int64_t)raw : (int64_t)(int32_t)(uint32_t)raw; snprintf(tmp, sizeof tmp, zero ? "%0*lld" : "%*lld", width, (long long)v); }
                else if (ch == 'u') snprintf(tmp, sizeof tmp, zero ? "%0*llu" : "%*llu", width, (unsigned long long)raw);
                else snprintf(tmp, sizeof tmp, zero ? "%0*llx" : "%*llx", width, (unsigned long long)raw);
                for (char *q = tmp; *q; q++) PUT(*q);
                break;
            }
            if (ch == 's') {
                uint32_t sp = ai < nargs ? args[ai++] : 0;
                if (!sp) { for (const char *q = "(null)"; *q; q++) PUT(*q); }
                else for (const char *q = gstr(sp); *q; q++) PUT(*q);
                break;
            }
            if (ch == 'c') { PUT((char)(ai < nargs ? args[ai++] : 0)); break; }
            for (const char *q = spec; q <= p; q++) PUT(*q);      /* especificador nao suportado: emite cru */
            break;
        }
    }
    if (cap > 0) out[n < cap ? n : cap - 1] = 0;
    return n;
}

/* ---------------------------------------------------------------------------------------------------- */
/* arquivos: saves em savedir (leitura/escrita) + recursos em resdir (somente leitura)                     */
/* ---------------------------------------------------------------------------------------------------- */
#define OPEN_RO 1
#define OPEN_WO 2
#define OPEN_WT 4
typedef struct { int used; char name[128]; int mode; int pos; } FileH;
static FileH files[64];

static int valid_name(const char *n) {
    size_t l = strlen(n);
    if (!l || l >= 128 || n[0] == '/' || n[0] == '\\' || strstr(n, "..") || strchr(n, ':') || strchr(n, '\\')) return 0;
    return 1;
}
static void path_of(char *dst, size_t cap, const char *dir, const char *name) { snprintf(dst, cap, "%s/%s", dir, name); }
static long file_size(const char *path) { FILE *f = fopen(path, "rb"); if (!f) return -1; fseek(f, 0, SEEK_END); long s = ftell(f); fclose(f); return s; }
/* acha o arquivo: savedir primeiro (persistente), depois resdir (empacotado). devolve tamanho ou -1 */
static long find_file(const char *name, char *path, size_t cap, int *persistent) {
    path_of(path, cap, g_savedir, name); long s = file_size(path); if (s >= 0) { *persistent = 1; return s; }
    path_of(path, cap, g_resdir, name); s = file_size(path); *persistent = 0; return s;
}

/* ---------------------------------------------------------------------------------------------------- */
/* clips de audio                                                                                         */
/* ---------------------------------------------------------------------------------------------------- */
static struct { uint32_t ptr; int used; } clips[64];
static int clip_id(uint32_t ptr) { for (int i = 0; i < 64; i++) if (clips[i].used && clips[i].ptr == ptr) return i; return -1; }

/* ---------------------------------------------------------------------------------------------------- */
/* implementacoes (um por import)                                                                         */
/* ---------------------------------------------------------------------------------------------------- */
#define H(name) static uint32_t h_##name(Cpu *c)

H(SetDisplayProperty) {                      /* tabela 0x1f8/0x16: (ptr_display, property, value, size) */
    uint32_t prop = c->r[1], val = c->r[2], size = c->r[3];
    if (size != 0) return 0;
    if (prop == 0x64) g_disp_w = val; else if (prop == 0x65) g_disp_h = val; else if (prop == 0x7f) g_annunc = val;
    return 0;
}
H(CletRegister) {
    for (int k = 0; k < 6; k++) g_clet[k] = LD32(c->r[0] + 4u * k);
    g_annunc = c->r[1];
    if (gc1_trace) fprintf(gc1_trace, "clet start=%08x pause=%08x resume=%08x destroy=%08x paint=%08x event=%08x\n", g_clet[0], g_clet[1], g_clet[2], g_clet[3], g_clet[4], g_clet[5]);
    return 0;
}
/* --- memoria --- */
H(Alloc)  { return heap_alloc(c->r[0]); }
H(Calloc) { uint32_t p = heap_alloc(c->r[0]); memset(G + p, 0, c->r[0]); return p; }
H(Free)   { heap_free(c->r[0]); return c->r[0]; }
/* --- tempo / timers --- */
H(CurrentTime) { c->r[1] = (uint32_t)(g_now >> 32); return (uint32_t)g_now; }
H(DefTimer)    { timer_cancel(c->r[0]); ST32(c->r[0], c->r[1]); return 0; }
H(SetTimer) {
    uint32_t pt = c->r[0], lo = c->r[1], hi = c->r[2], param = c->r[3];
    if (timer_pending(pt)) return (uint32_t)-5;
    timer_add(pt, LD32(pt), param, 0, g_now + (((uint64_t)hi << 32) | lo));
    return 0;
}
H(UnsetTimer) { timer_cancel(c->r[0]); return 0; }
H(Exit) { g_exit = 1; return 0; }
/* --- propriedades do sistema --- */
H(GetSystemProperty) {
    const char *id = gstr(c->r[0]); const char *v;
    if (!strcmp(id, "RSSILEVEL")) v = "30"; else if (!strcmp(id, "BATTERYLEVEL")) v = "100"; else if (!strcmp(id, "PHONEMODEL")) v = "Emulator";
    else if (!strcmp(id, "PHONENUMBER")) v = ""; else if (!strcmp(id, "MIN")) v = "01000000000";
    else if (!strncmp(id, "ANNUN_", 6) || !strcmp(id, "AIRPLANE_MODE") || !strcmp(id, "ROAMING_AREA") || !strcmp(id, "DS_LOCK") || !strcmp(id, "CURRENTCH")) v = "0";
    else return (uint32_t)-9;
    if (strlen(v) + 1 > c->r[2]) return (uint32_t)-18;
    strcpy((char *)G + c->r[1], v); return 0;
}
H(SetSystemProperty) { return 0; }
/* --- recursos empacotados --- */
H(GetResourceId) {
    const char *name = gstr(c->r[0]); char path[640]; int pers;
    long sz = valid_name(name) ? find_file(name, path, sizeof path, &pers) : -1;
    if (sz < 0) { if (c->r[1]) ST32(c->r[1], 0); return (uint32_t)-12; }
    uint32_t h = heap_alloc((uint32_t)strlen(name) + 1);
    strcpy((char *)G + h, name);
    ST32(c->r[1], (uint32_t)sz);
    return h;
}
H(GetResource) {
    int32_t id = (int32_t)c->r[0]; uint32_t buf = c->r[1], cap = c->r[2];
    if (id < 0) return (uint32_t)-9;
    char path[640]; int pers; long sz = find_file(gstr((uint32_t)id), path, sizeof path, &pers);
    if (sz < 0) gc1_fatal("recurso ausente: %s", gstr((uint32_t)id));
    if ((uint32_t)sz > cap) return (uint32_t)-1;
    FILE *f = fopen(path, "rb"); size_t got = fread(G + buf, 1, (size_t)sz, f); fclose(f);
    if ((long)got != sz) gc1_fatal("leitura curta do recurso %s", path);
    return 0;
}
/* Destinos pequenos (buffers de pilha do guest) para formatos que levam nomes em PT, mais compridos que os coreanos para os quais o jogo foi feito:
 * "%s%s" (prefixo "+N " + nome do item; 32 bytes em varias telas de inventario/venda/melhoria) e "Local: %s %s %s %s" (4 nomes de mapa; 100 bytes).
 * O texto e cortado (a traducao ja limita nomes de item a 27 bytes: tools/translate/audit_len.py); sem isso o excesso pisa no quadro da pilha. */
static size_t sprintk_cap(uint32_t fmt) { return fmt == 0x41ec8 ? 31 : fmt == 0x41e8c ? 99 : 0; }
H(Sprintk) {
    uint32_t a[6] = {arg(c, 2), arg(c, 3), arg(c, 4), arg(c, 5), arg(c, 6), arg(c, 7)};
    char tmp[4096]; int n = wipi_sprintf(tmp, sizeof tmp, gstr(c->r[1]), a, 6);
    size_t m = (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1;
    size_t cap = sprintk_cap(c->r[1]); if (cap && m > cap) m = cap;
    memcpy(G + c->r[0], tmp, m); G[c->r[0] + m] = 0;
    return (uint32_t)n;
}
/* --- arquivos --- */
H(FsOpen) {
    const char *name = gstr(c->r[0]); int mode = (int32_t)c->r[1];
    if (!valid_name(name)) return (uint32_t)-3;
    char path[640]; int pers; long sz = find_file(name, path, sizeof path, &pers);
    if (sz < 0 && mode == OPEN_RO) return (uint32_t)-12;
    if (mode == OPEN_WT || sz < 0 || !pers) {
        if (mode != OPEN_RO && (mode == OPEN_WT || sz < 0)) {
            path_of(path, sizeof path, g_savedir, name);
            FILE *f = fopen(path, "wb"); if (!f) return (uint32_t)-1; fclose(f);
        }
    }
    for (int i = 0; i < 64; i++) if (!files[i].used) {
        files[i].used = 1; strncpy(files[i].name, name, 127); files[i].name[127] = 0; files[i].mode = mode; files[i].pos = 0;
        return (uint32_t)(i + 1);
    }
    return (uint32_t)-1;
}
static FileH *fh(uint32_t fd) { if (fd < 1 || fd > 64 || !files[fd - 1].used) return NULL; return &files[fd - 1]; }
H(FsClose) { FileH *f = fh(c->r[0]); if (!f) return (uint32_t)-2; f->used = 0; return 0; }
H(FsRead) {
    FileH *f = fh(c->r[0]); int32_t len = (int32_t)c->r[2];
    if (!f) return (uint32_t)-2;
    if (f->mode == OPEN_WO || f->mode == OPEN_WT) return (uint32_t)-24;
    if (len < 0) return (uint32_t)-1;
    if (len == 0) return 0;
    char path[640]; int pers; long sz = find_file(f->name, path, sizeof path, &pers);
    if (sz < 0) return (uint32_t)-12;
    long cnt = sz - f->pos; if (cnt > len) cnt = len;
    if (cnt <= 0) return (uint32_t)-23;
    FILE *fp = fopen(path, "rb"); if (!fp) return (uint32_t)-1;
    fseek(fp, f->pos, SEEK_SET); size_t got = fread(G + c->r[1], 1, (size_t)cnt, fp); fclose(fp);
    f->pos += (int)got; return (uint32_t)got;
}
H(FsWrite) {
    FileH *f = fh(c->r[0]); int32_t len = (int32_t)c->r[2];
    if (!f) return (uint32_t)-2;
    if (f->mode == OPEN_RO) return (uint32_t)-24;
    if (len < 0) return (uint32_t)-1;
    char path[640]; path_of(path, sizeof path, g_savedir, f->name);
    FILE *fp = fopen(path, "r+b"); if (!fp) fp = fopen(path, "w+b"); if (!fp) return (uint32_t)-1;
    fseek(fp, f->pos, SEEK_SET); size_t w = fwrite(G + c->r[1], 1, (size_t)len, fp); fclose(fp);
    if (w == 0 && len != 0) return (uint32_t)-1;
    f->pos += (int)w; return (uint32_t)w;
}
H(FsFileAttribute) {
    const char *name = gstr(c->r[0]);
    if (!valid_name(name)) return (uint32_t)-3;
    char path[640]; int pers; long sz = find_file(name, path, sizeof path, &pers);
    if (sz < 0) return (uint32_t)-12;
    ST32(c->r[1], 0); ST32(c->r[1] + 4, 0); ST32(c->r[1] + 8, (uint32_t)sz);
    return 0;
}
H(FsRemove) {
    const char *name = gstr(c->r[0]);
    if (!valid_name(name)) return (uint32_t)-3;
    char path[640]; path_of(path, sizeof path, g_savedir, name);
    return remove(path) == 0 ? 0 : (uint32_t)-12;
}
/* --- gráficos --- */
H(GetScreenFramebuffer) {
    if (c->r[0] != 0) gc1_fatal("GetScreenFramebuffer: tipo %u nao suportado", c->r[0]);
    if (!g_fb_handle) {
        g_fb_buf = heap_alloc(g_disp_w * g_disp_h * 2); memset(G + g_fb_buf, 0, g_disp_w * g_disp_h * 2);
        g_fb_handle = heap_alloc(16); memset(G + g_fb_handle, 0, 16);
    }
    return g_fb_handle;
}
H(GetFramebufferPointer) { return g_fb_buf; }
H(GetFramebufferWidth)   { return g_disp_w; }
H(GetFramebufferHeight)  { return g_disp_h - (g_annunc ? 0 : 0); }
H(InitContext) {
    uint32_t p = c->r[0];
    static const uint32_t init[14] = {0, 0, 0x7fff, 0x7fff, 0, 0x00ffffff, 255, 0, 255, 0, 0, 0, 0, 0};
    for (int k = 0; k < 14; k++) ST32(p + 4u * k, init[k]);
    return 0;
}
H(DrawLine) {
    uint32_t ctx = arg(c, 5);
    int x1 = (int)c->r[1], y1 = (int)c->r[2], x2 = (int)c->r[3], y2 = (int)arg(c, 4);
    int cx1 = (int32_t)LD32(ctx), cy1 = (int32_t)LD32(ctx + 4), cx2 = (int32_t)LD32(ctx + 8), cy2 = (int32_t)LD32(ctx + 12);
    uint32_t fg = LD32(ctx + 16); int ox = (int32_t)LD32(ctx + 48), oy = (int32_t)LD32(ctx + 52);
    x1 += ox; x2 += ox; y1 += oy; y2 += oy;
    if (cx2 > (int)g_disp_w - 1) cx2 = (int)g_disp_w - 1; if (cy2 > (int)g_disp_h - 1) cy2 = (int)g_disp_h - 1;
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1, dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x1 >= cx1 && x1 <= cx2 && y1 >= cy1 && y1 <= cy2 && x1 >= 0 && y1 >= 0) ST16(g_fb_buf + 2u * ((uint32_t)y1 * g_disp_w + (uint32_t)x1), fg);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err; if (e2 >= dy) { err += dy; x1 += sx; } if (e2 <= dx) { err += dx; y1 += sy; }
    }
    return 0;
}
uint64_t gc1_frame_crc(void) {
    uint64_t h = 0xcbf29ce484222325ull; const uint8_t *p = G + g_fb_buf; size_t n = (size_t)g_disp_w * g_disp_h * 2;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
H(FlushLcd) {
    if (c->r[0] != 0) gc1_fatal("FlushLcd: tipo %u nao suportado", c->r[0]);
    if (gc1_trace) fprintf(gc1_trace, "flush crc=%016llx\n", (unsigned long long)gc1_frame_crc());
    host_flush((const uint16_t *)(G + g_fb_buf), (int)g_disp_w, (int)g_disp_h);
    return 0;
}
H(GetPixelFromRgb) {
    uint32_t r = c->r[0] & 0xff, g = c->r[1] & 0xff, b = c->r[2] & 0xff;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
H(GetDisplayInfo) {
    if (c->r[0] != 0) gc1_fatal("GetDisplayInfo: tipo %u nao suportado", c->r[0]);
    uint32_t o = c->r[1], v[9] = {16, 16, g_disp_w, g_disp_h, g_disp_w * 2, 1, 0xf800, 0x001f, 0x07e0};   /* red, blue, green (ordem do WIE) */
    for (int k = 0; k < 9; k++) ST32(o + 4u * k, v[k]);
    return 1;
}
/* --- metodos de entrada (IM) --- */
static uint32_t im_modes;
H(GetSupportModeCount) { return 4; }
H(GetSupportedModes) {
    if (!im_modes) {
        static const char *m[4] = {"EN/L", "EN/S", "KO", "N123"};
        uint32_t tab = 5 * 4, tot = tab; for (int k = 0; k < 4; k++) tot += (uint32_t)strlen(m[k]) + 1;
        im_modes = heap_alloc(tot); uint32_t sp = im_modes + tab;
        for (int k = 0; k < 4; k++) { ST32(im_modes + 4u * k, sp); strcpy((char *)G + sp, m[k]); sp += (uint32_t)strlen(m[k]) + 1; }
        ST32(im_modes + 16, 0);
    }
    return im_modes;
}
H(SetCurrentMode) { return (uint32_t)-1; }
H(HandleInput) {
    uint32_t key = c->r[0]; int32_t type = (int32_t)c->r[1]; uint32_t pc = c->r[2], pcs = c->r[3], pcmp = arg(c, 4), pcmps = arg(c, 5);
    int32_t cap = (int32_t)LD32(pcs); int32_t size = (type == 2 && key >= '0' && key <= '9') ? (cap < 0 ? 0 : cap > 4 ? 4 : cap) : 0;
    if (size > 0) memcpy(G + pc, "temp", (size_t)size);
    ST32(pcs, (uint32_t)size); ST32(pcmps, 0); (void)pcmp;
    return size > 0;
}
/* --- audio --- */
H(ClipCreate) {
    int id = -1; for (int i = 0; i < 64; i++) if (!clips[i].used) { id = i; break; }
    if (id < 0) return 0;
    uint32_t p = heap_alloc(64); memset(G + p, 0, 64); clips[id].ptr = p; clips[id].used = 1; return p;
}
H(ClipFree) { int id = clip_id(c->r[0]); if (c->r[0] == 0) return 0; if (id >= 0) { host_audio_free(id); clips[id].used = 0; } heap_free(c->r[0]); return 0; }
H(ClipPutData) {
    int id = clip_id(c->r[0]); if (id < 0 || !c->r[0]) return (uint32_t)-1;
    host_audio_load(id, G + c->r[1], c->r[2]); return c->r[2];
}
H(ClipAllocPlayer) { return 0; }
H(ClipFreePlayer)  { return 0; }
H(Play) { int id = clip_id(c->r[0]); if (id >= 0) host_audio_play(id, c->r[1] != 0); return 0; }
H(Stop) { int id = clip_id(c->r[0]); if (id >= 0) host_audio_stop(id); return 0; }
H(SetVolume) { host_audio_volume((int32_t)c->r[0]); return 0; }
H(Vibrator)  { host_vibrate((int32_t)c->r[0], (int32_t)c->r[1]); return 0; }
H(SetMuteState) { return 0; }
H(BackLight) { return 0; }
H(Unk514) { return 0; }
/* --- rede: o servico original nao existe mais; um servidor falso responde localmente (runtime/netsim.h).
 * Com GC1_NONET=1 todas as chamadas falham como no WIE. --- */
#include <time.h>
#include "netsim.h"
static uint8_t ns_req[2048], ns_resp[1024]; static int ns_reqn, ns_respn, ns_respoff;
H(Connect) { timer_add(0, c->r[0], c->r[1], ns_nonet ? 0xffffffffu : 0, g_now + 1); return 0; }
H(NetClose) { return 0; }
H(UnkSocket) { return ns_nonet ? (uint32_t)-1 : 5; }
H(NetFail) { return (uint32_t)-1; }
H(SocketConnect) { if (ns_nonet) return (uint32_t)-1; ns_reqn = ns_respn = ns_respoff = 0; timer_add(0, c->r[3], 0, c->r[0], g_now + 1); return 0; }
H(SocketWrite) {
    int32_t len = (int32_t)c->r[2]; if (ns_nonet) return (uint32_t)-1; if (len < 0) return (uint32_t)-1;
    int take = len; if (ns_reqn + take > (int)sizeof ns_req) take = (int)sizeof ns_req - ns_reqn;
    memcpy(ns_req + ns_reqn, G + c->r[1], (size_t)take); ns_reqn += take;
    if (ns_reqn >= 4 && ns_reqn >= (ns_req[0] | (ns_req[1] << 8)) + 4) { ns_respn = ns_respond(ns_req, ns_reqn, ns_resp); ns_respoff = 0; ns_reqn = 0; }
    return (uint32_t)len;
}
H(SocketRead) {
    if (ns_nonet) return (uint32_t)-1;
    int left = ns_respn - ns_respoff; if (left <= 0) return (uint32_t)-0x13;
    int n = (int)c->r[2] < left ? (int)c->r[2] : left; memcpy(G + c->r[1], ns_resp + ns_respoff, (size_t)n); ns_respoff += n; return (uint32_t)n;
}
H(SocketClose) { if (ns_nonet) return (uint32_t)-1; ns_reqn = ns_respn = ns_respoff = 0; return 0; }
H(SetNetCallback) { return ns_nonet ? (uint32_t)-1 : 0; }
H(Htons) { uint32_t v = c->r[0] & 0xffff; return ((v & 0xff) << 8) | (v >> 8); }
/* --- libc --- */
H(Atoi)    { return (uint32_t)atoi(gstr(c->r[0])); }
H(Strcpy)  { strcpy((char *)G + c->r[0], gstr(c->r[1])); return c->r[0]; }
H(Strncpy) { strncpy((char *)G + c->r[0], gstr(c->r[1]), c->r[2]); return c->r[0]; }
H(Strcmp)  { return (uint32_t)strcmp(gstr(c->r[0]), gstr(c->r[1])); }
H(Strncmp) { return (uint32_t)strncmp(gstr(c->r[0]), gstr(c->r[1]), c->r[2]); }
H(Strlen)  { return (uint32_t)strlen(gstr(c->r[0])); }
H(Memcpy)  { memcpy(G + c->r[0], G + c->r[1], c->r[2]); return c->r[0]; }
H(Memmove) { memmove(G + c->r[0], G + c->r[1], c->r[2]); return c->r[0]; }
H(Memset)  { memset(G + c->r[0], (int)c->r[1], c->r[2]); return c->r[0]; }

typedef struct { uint32_t table, index; const char *name; uint32_t (*fn)(Cpu *); } HleDef;
static const HleDef defs[] = {
    {0x1f8, 0x16, "SetDisplayProperty", h_SetDisplayProperty},
    {0x1fb, 0x03, "CletRegister", h_CletRegister},
    {0x1fb, 0x32, "GetFramebufferPointer", h_GetFramebufferPointer}, {0x1fb, 0x33, "GetFramebufferWidth", h_GetFramebufferWidth},
    {0x1fb, 0x34, "GetFramebufferHeight", h_GetFramebufferHeight},
    {0x1fb, 0x65, "Sprintk", h_Sprintk}, {0x1fb, 0x68, "Exit", h_Exit},
    {0x1fb, 0x75, "Alloc", h_Alloc}, {0x1fb, 0x76, "Calloc", h_Calloc}, {0x1fb, 0x77, "Free", h_Free},
    {0x1fb, 0x7a, "DefTimer", h_DefTimer}, {0x1fb, 0x7b, "SetTimer", h_SetTimer}, {0x1fb, 0x7c, "UnsetTimer", h_UnsetTimer},
    {0x1fb, 0x7d, "CurrentTime", h_CurrentTime}, {0x1fb, 0x7e, "GetSystemProperty", h_GetSystemProperty}, {0x1fb, 0x7f, "SetSystemProperty", h_SetSystemProperty},
    {0x1fb, 0x80, "GetResourceId", h_GetResourceId}, {0x1fb, 0x81, "GetResource", h_GetResource},
    {0x1fb, 0xca, "GetScreenFramebuffer", h_GetScreenFramebuffer}, {0x1fb, 0xcd, "InitContext", h_InitContext}, {0x1fb, 0xd1, "DrawLine", h_DrawLine},
    {0x1fb, 0xde, "FlushLcd", h_FlushLcd}, {0x1fb, 0xdf, "GetPixelFromRgb", h_GetPixelFromRgb}, {0x1fb, 0xe1, "GetDisplayInfo", h_GetDisplayInfo},
    {0x1fb, 0x12c, "GetSupportModeCount", h_GetSupportModeCount}, {0x1fb, 0x12d, "GetSupportedModes", h_GetSupportedModes},
    {0x1fb, 0x12e, "SetCurrentMode", h_SetCurrentMode}, {0x1fb, 0x130, "HandleInput", h_HandleInput},
    {0x1fb, 0x190, "FsOpen", h_FsOpen}, {0x1fb, 0x191, "FsRead", h_FsRead}, {0x1fb, 0x192, "FsWrite", h_FsWrite}, {0x1fb, 0x193, "FsClose", h_FsClose},
    {0x1fb, 0x195, "FsFileAttribute", h_FsFileAttribute}, {0x1fb, 0x196, "FsRemove", h_FsRemove},
    {0x1fb, 0x258, "Connect", h_Connect}, {0x1fb, 0x259, "NetClose", h_NetClose}, {0x1fb, 0x25b, "SocketConnect", h_SocketConnect},
    {0x1fb, 0x25c, "SocketWrite", h_SocketWrite}, {0x1fb, 0x25d, "SocketRead", h_SocketRead}, {0x1fb, 0x25e, "SocketClose", h_SocketClose},
    {0x1fb, 0x265, "SetReadCallback", h_SetNetCallback}, {0x1fb, 0x266, "SetWriteCallback", h_SetNetCallback}, {0x1fb, 0x385, "Htons", h_Htons},
    {0x1fb, 0x4b0, "ClipCreate", h_ClipCreate}, {0x1fb, 0x4b1, "ClipFree", h_ClipFree}, {0x1fb, 0x4b3, "ClipPutData", h_ClipPutData},
    {0x1fb, 0x4ba, "Play", h_Play}, {0x1fb, 0x4bd, "Stop", h_Stop}, {0x1fb, 0x4c0, "SetVolume", h_SetVolume}, {0x1fb, 0x4c1, "Vibrator", h_Vibrator},
    {0x1fb, 0x4c5, "ClipAllocPlayer", h_ClipAllocPlayer}, {0x1fb, 0x4c6, "ClipFreePlayer", h_ClipFreePlayer}, {0x1fb, 0x4d1, "SetMuteState", h_SetMuteState},
    {0x1fb, 0x514, "Unk514", h_Unk514}, {0x1fb, 0x578, "BackLight", h_BackLight}, {0x1fb, 0x7d0, "UnkSocket", h_UnkSocket},
    {0x1, 0x3fb, "Atoi", h_Atoi}, {0x1, 0x405, "Strcpy", h_Strcpy}, {0x1, 0x406, "Strncpy", h_Strncpy}, {0x1, 0x409, "Strcmp", h_Strcmp},
    {0x1, 0x40a, "Strncmp", h_Strncmp}, {0x1, 0x411, "Strlen", h_Strlen}, {0x1, 0x414, "Memcpy", h_Memcpy}, {0x1, 0x415, "Memmove", h_Memmove},
    {0x1, 0x418, "Memset", h_Memset},
};

void gc1_hle_call(const GcStub *s, Cpu *c) {
    for (size_t i = 0; i < sizeof defs / sizeof defs[0]; i++) {
        if (defs[i].table != s->table || defs[i].index != s->index) continue;
        uint32_t a0 = c->r[0], a1 = c->r[1], a2 = c->r[2], a3 = c->r[3];
        uint32_t ret = defs[i].fn(c);
        if (gc1_trace) fprintf(gc1_trace, "api %s %08x %08x %08x %08x -> %08x\n", defs[i].name, a0, a1, a2, a3, ret);
        c->r[0] = ret;
        return;
    }
    gc1_fatal("import nao implementado: tabela 0x%x indice 0x%x (stub 0x%08x)", s->table, s->index, s->addr);
}

/* ---------------------------------------------------------------------------------------------------- */
/* boot e eventos                                                                                         */
/* ---------------------------------------------------------------------------------------------------- */
static uint64_t fnv64(const uint8_t *p, size_t n) { uint64_t h = 0xcbf29ce484222325ull; while (n--) h = (h ^ *p++) * 0x100000001b3ull; return h; }

int gc1_boot(const char *resdir, const char *savedir) {
    snprintf(g_resdir, sizeof g_resdir, "%s", resdir); snprintf(g_savedir, sizeof g_savedir, "%s", savedir);
    ns_init(savedir);
    #ifdef _WIN32
    G = (uint8_t *)_aligned_malloc(MEM_SIZE, 4096);
#else
    { void *pm = NULL; if (posix_memalign(&pm, 4096, MEM_SIZE)) pm = NULL; G = (uint8_t *)pm; }
#endif
    if (!G) return 1; memset(G, 0, MEM_SIZE);
    gc1_register_all(); heap_init();
    /* carrega binary.mod (ELF ARM): copia .text e .data para o endereco de cada secao */
    char path[640]; snprintf(path, sizeof path, "%s/binary.mod", resdir);
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "gc1: nao achei %s\n", path); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *e = (uint8_t *)malloc((size_t)n); if (fread(e, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 2; } fclose(f);
    if (n < 0x34 || memcmp(e, "\177ELF", 4)) { fprintf(stderr, "gc1: binary.mod nao e ELF\n"); return 2; }
    uint32_t shoff, shnum = 0, shstr; uint16_t shentsize;
    memcpy(&shoff, e + 0x20, 4); memcpy(&shentsize, e + 0x2e, 2); { uint16_t t; memcpy(&t, e + 0x30, 2); shnum = t; memcpy(&t, e + 0x32, 2); shstr = t; }
    uint32_t strtab_off; memcpy(&strtab_off, e + shoff + shstr * shentsize + 0x10, 4);
    int got_text = 0;
    for (uint32_t i = 0; i < shnum; i++) {
        const uint8_t *sh = e + shoff + i * shentsize; uint32_t name, type, addr, off, size;
        memcpy(&name, sh, 4); memcpy(&type, sh + 4, 4); memcpy(&addr, sh + 12, 4); memcpy(&off, sh + 16, 4); memcpy(&size, sh + 20, 4);
        const char *nm = (const char *)e + strtab_off + name;
        if (!strcmp(nm, ".text") || !strcmp(nm, ".data")) {
            if ((uint64_t)addr + size > MEM_SIZE) return 2;
            memcpy(G + addr, e + off, size);
            if (!strcmp(nm, ".text")) { got_text = 1; if (size != GC1_TEXT_SIZE || fnv64(e + off, GC1_CODE_SIZE) != GC1_CODE_FNV) { fprintf(stderr, "gc1: este binary.mod nao e o que foi recompilado (codigo do .text diferente)\n"); return 3; } }
        }
    }
    free(e); if (!got_text) return 2;
    memset(&gc_cpu, 0, sizeof gc_cpu); gc_cpu.r[13] = STACK_TOP;
    /* sequencia do WIE (init.rs): entry(+1) com InitParam1/2; le ptr_init_struct; chama fn_init */
    uint32_t p1 = heap_alloc(0x218), p2 = heap_alloc(16); memset(G + p1, 0, 0x218); memset(G + p2, 0, 16);
    uint32_t a[3] = {p1, p2, 0}; gc1_call(0x40fb8u | 1u, 3, a);
    uint32_t is = LD32(p1 + 0x214), fn_init = LD32(is + 4);
    if (gc1_trace) fprintf(gc1_trace, "boot init_struct=%08x fn_init=%08x\n", is, fn_init);
    gc1_call(fn_init, 0, NULL);
    gc1_patches_install();
    return 0;
}
void gc1_start(void)  { if (gc1_trace) fprintf(gc1_trace, "call startClet\n"); gc1_call(g_clet[0], 0, NULL); }
void gc1_paint(void)  { if (gc1_trace) fprintf(gc1_trace, "call paintClet\n"); gc1_call(g_clet[4], 0, NULL); }
void gc1_event(uint32_t type, uint32_t p1, uint32_t p2) {
    if (gc1_trace) fprintf(gc1_trace, "event %u %d %d\n", type, (int)p1, (int)p2);
    uint32_t a[3] = {type, p1, p2}; gc1_call(g_clet[5], 3, a);
}
void gc1_key(int key, int pressed) { gc1_event(pressed ? 502u : 503u, (uint32_t)key, 0); }

/* ---------------- camada de entrada "inteligente" (gc1_input_key) ------------------------------------------------
 * O jogo foi feito para o teclado de celular, onde so se aperta uma tecla por vez: soltar QUALQUER tecla no gameplay chama
 * stop_player_movement, e apertar uma direcao enquanto ele ataca/esta em acao e ignorado. Num teclado de PC isso da
 * "aperto a direcao e ele nao anda / anda um pouco e para". Esta camada, que fica entre a plataforma e gc1_key():
 *  - guarda as direcoes seguradas (a ultima apertada manda);
 *  - soltar uma tecla que NAO e direcao, com uma direcao segurada, nao e repassado ao jogo (so servia para parar);
 *  - soltar uma direcao com outra ainda segurada: para e, em seguida, anda na outra;
 *  - se o jogo ignorou o aperto (ex.: estava atacando), reaperta quando ele ficar livre.
 * Os re-apertos zeram last_move_dir (0x1636040) para nao virarem o "toque duplo = correr" do jogo. */
#define A_GAME_MODE 0x1400058u
#define A_MOVE_MODE 0x1634b40u
#define A_LAST_DIR  0x1636040u
#define A_PLAYER    0x1634268u
#define A_DBL_TAP   0x163617cu   /* quadros restantes para o toque duplo (correr) */
static int g_held[8], g_nheld;
static int dir_of(int k) { switch (k) { case -1: case 0x32: return 3; case -2: case 0x38: return 0; case -3: case 0x34: return 2; case -4: case 0x36: return 1; default: return -1; } }
static int free_gameplay(void) {
    if (LD8(A_GAME_MODE) != 2) return 0;
    if (LD8(0x163623bu) || LD8(0x16343f2u) || LD8(0x1500029u) || LD8(0x1638608u) || LD8(0x1633f10u) || LD8(0x150000cu) || LD8(0x163615au)) return 0;
    uint32_t pe = LD32(A_PLAYER); if (!pe || LD8(pe + 5) == 9) return 0;
    return 1;
}
static void held_remove(int k) { for (int i = 0; i < g_nheld; i++) if (g_held[i] == k) { memmove(&g_held[i], &g_held[i + 1], sizeof(int) * (g_nheld - i - 1)); g_nheld--; return; } }
static void repress(int k) { ST8(A_LAST_DIR, 0xff); gc1_key(k, 1); }
static void smart_tick(void) {
    if (!g_nheld || !free_gameplay()) return;
    uint32_t pe = LD32(A_PLAYER);
    if (LD8(A_MOVE_MODE) == 0 && LD8(pe + 8) == 0) repress(g_held[g_nheld - 1]);
}
/* Diagonal: devolve a direcao (0 baixo, 1 direita, 2 esquerda, 3 cima) de uma tecla segurada PERPENDICULAR a `primary` (a mais recente), ou -1.
 * So vale se a direcao `primary` tambem esta segurada. Usado pelo gancho de andar (patch.c) para somar um passo no outro eixo. */
int gc1_diag_secondary(int primary) {
    if (!g_smart || g_nheld < 2) return -1;
    int has = 0; for (int i = 0; i < g_nheld; i++) if (dir_of(g_held[i]) == primary) has = 1;
    if (!has) return -1;
    int pv = (primary == 0 || primary == 3);
    for (int i = g_nheld - 1; i >= 0; i--) { int d = dir_of(g_held[i]); if (d >= 0 && (d == 0 || d == 3) != pv) return d; }
    return -1;
}
void gc1_input_enable(int on) { g_smart = on; g_nheld = 0; }
void gc1_input_key(int key, int pressed) {
    if (!g_smart) { gc1_key(key, pressed); return; }
    int d = dir_of(key) >= 0;
    if (pressed) { if (d) { held_remove(key); if (g_nheld < 8) g_held[g_nheld++] = key; } gc1_key(key, 1); return; }
    if (d) {
        held_remove(key); gc1_key(key, 0);
        if (free_gameplay()) ST8(A_DBL_TAP, 6);          /* janela do toque duplo conta a partir da soltura (o jogo conta 4 quadros desde o aperto) */
        if (g_nheld && free_gameplay()) repress(g_held[g_nheld - 1]);
    } else if (!(g_nheld && free_gameplay())) gc1_key(key, 0);
}

/* ---------------- save state: foto completa do jogo (memoria do guest + estado do runtime) ----------------------------
 * Formato: "GC1S" v1, cabecalho com o estado do runtime, depois as paginas de 4 KiB nao zeradas (indice u32 + dados), fim = indice 0xffffffff.
 * Nao inclui os arquivos de save do jogo em disco (a pasta de saves) nem o que a plataforma esta tocando; o audio e retomado pela politica. */
#define SNAP_MAGIC 0x53314347u
typedef struct {
    uint32_t magic, version; uint64_t code_fnv; uint64_t now; int32_t exit_; uint32_t clet[6], disp_w, disp_h, annunc, fb_buf, fb_handle, im_modes;
    int32_t nblk, ntmr; uint64_t tmr_seq; Cpu cpu;
} SnapHdr;
int gc1_snapshot_save(const char *path) {
    FILE *f = fopen(path, "wb"); if (!f) return 1;
    SnapHdr h; memset(&h, 0, sizeof h);
    h.magic = SNAP_MAGIC; h.version = 1; h.code_fnv = GC1_CODE_FNV; h.now = g_now; h.exit_ = g_exit; memcpy(h.clet, g_clet, sizeof g_clet);
    h.disp_w = g_disp_w; h.disp_h = g_disp_h; h.annunc = g_annunc; h.fb_buf = g_fb_buf; h.fb_handle = g_fb_handle; h.im_modes = im_modes;
    h.nblk = nblk; h.ntmr = ntmr; h.tmr_seq = tmr_seq; h.cpu = gc_cpu;
    fwrite(&h, sizeof h, 1, f); fwrite(blk, sizeof(Blk), (size_t)nblk, f); fwrite(tmr, sizeof(Timer), (size_t)ntmr, f);
    fwrite(files, sizeof files, 1, f); fwrite(clips, sizeof clips, 1, f); ap_state_save(f);
    static const uint8_t zero[4096];
    for (uint32_t pg = 0; pg < MEM_SIZE / 4096; pg++) if (memcmp(G + (size_t)pg * 4096, zero, 4096)) { fwrite(&pg, 4, 1, f); fwrite(G + (size_t)pg * 4096, 4096, 1, f); }
    uint32_t end = 0xffffffffu; fwrite(&end, 4, 1, f);
    int bad = ferror(f); fclose(f); return bad;
}
int gc1_snapshot_load(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 1;
    SnapHdr h; if (fread(&h, sizeof h, 1, f) != 1 || h.magic != SNAP_MAGIC || h.version != 1) { fclose(f); return 2; }
    if (h.code_fnv != GC1_CODE_FNV || h.nblk < 1 || h.ntmr < 0 || h.ntmr > 256) { fclose(f); return 3; }     /* foto de outro binario */
    Blk *nb = malloc(sizeof(Blk) * (size_t)(h.nblk < 4096 ? 4096 : h.nblk)); Timer nt[256];
    if (!nb || fread(nb, sizeof(Blk), (size_t)h.nblk, f) != (size_t)h.nblk || fread(nt, sizeof(Timer), (size_t)h.ntmr, f) != (size_t)h.ntmr) { free(nb); fclose(f); return 2; }
    FileH nf[64]; typeof(clips) nc;
    if (fread(nf, sizeof nf, 1, f) != 1 || fread(nc, sizeof nc, 1, f) != 1) { free(nb); fclose(f); return 2; }
    ap_state_load(f);
    memset(G, 0, MEM_SIZE);
    for (;;) { uint32_t pg; if (fread(&pg, 4, 1, f) != 1 || pg == 0xffffffffu) break; if (pg >= MEM_SIZE / 4096 || fread(G + (size_t)pg * 4096, 4096, 1, f) != 1) { fclose(f); return 2; } }
    fclose(f);
    free(blk); blk = nb; nblk = h.nblk; capblk = h.nblk < 4096 ? 4096 : h.nblk;
    memcpy(tmr, nt, sizeof(Timer) * (size_t)h.ntmr); ntmr = h.ntmr; tmr_seq = h.tmr_seq;
    memcpy(files, nf, sizeof files); memcpy(clips, nc, sizeof clips);
    g_exit = h.exit_; memcpy(g_clet, h.clet, sizeof g_clet); g_disp_w = h.disp_w; g_disp_h = h.disp_h; g_annunc = h.annunc; g_fb_buf = h.fb_buf; g_fb_handle = h.fb_handle; im_modes = h.im_modes;
    gc_cpu = h.cpu; g_nheld = 0;
    gc1_patches_reset();
    g_off = g_raw - h.now; g_now = h.now;               /* o relogio do jogo continua de onde estava */
    return 0;
}
