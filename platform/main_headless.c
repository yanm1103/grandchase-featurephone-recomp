/* main_headless.c - roda o jogo sem janela, com tempo virtual e entrada roteirizada (para testes e traces).
 *   gc1_headless <pasta_recursos> <pasta_saves> [--frames N] [--trace arq] [--keys arq] [--dump prefixo] [--dump-every K]
 * arquivo de teclas: linhas "<ms_relativo> <codigo_tecla> <p|r>"  (p = pressionar, r = soltar) */
#include <stdlib.h>
#include <string.h>
#include "gc1.h"
#include "audio_policy.h"

static void out_music(uint64_t h, int on);
static void out_sfx(uint64_t h);
static const AudioOut g_aout = { out_music, out_sfx };
static const char *g_loadsnap;
static long g_smart_from = -1; static int g_smart_on;
static FILE *g_state; static uint64_t g_now, g_T0;
/* --state: uma linha por quadro com o estado interno do jogo (enderecos de docs/ESTADO.md), para eu testar sem jogar na mao */
static void log_state(const char *why) {
    if (!g_state) return;
    uint32_t pe = LD32(0x1634268), ps = pe ? LD32(pe + 0x28) : 0;
    fprintf(g_state, "t=%llu %s mode=%u map=%u", (unsigned long long)(g_now - g_T0), why, LD8(0x1400058), LD8(0x1400061));
    if (pe) fprintf(g_state, " x=%d y=%d dir=%u st=%u act=%u mv=%u run=%u dead=%u bgm=%u", (int16_t)LD16(pe + 0x14), (int16_t)LD16(pe + 0x16), LD8(pe + 1), LD8(pe + 5), LD8(pe + 8), LD8(0x1634b40), LD8(0x163407c), LD8(0x163615a), LD8(0x1636345));
    fprintf(g_state, " g=%u%u%u%u%u%u%u", LD8(0x163623bu), LD8(0x16343f2u), LD8(0x1500029u), LD8(0x1638608u), LD8(0x1633f10u), LD8(0x150000cu), LD8(0x163615au));
    if (ps) fprintf(g_state, " flags=%u%u%u%u", LD8(ps + 0xa), LD8(ps + 0xb), LD8(ps + 0xc), LD8(ps + 0xd));
    fprintf(g_state, "\n");
}
static const char *g_cov; static int g_flushes; static const char *g_dump; static int g_dump_every = 1;
static uint16_t g_last[240 * 320];

static uint64_t g_shot_t[64]; static int g_nshot, g_ishot;      /* --shots ms1,ms2,...: um PPM no primeiro quadro apos cada instante */
static void write_ppm(const char *name, const uint16_t *fb, int w, int h);
void host_flush(const uint16_t *fb, int w, int h) {
    g_flushes++; log_state("F");
    while (g_ishot < g_nshot && g_now - g_T0 >= g_shot_t[g_ishot]) { char nm[512]; snprintf(nm, sizeof nm, "%st%07llu.ppm", g_dump ? g_dump : "shot_", (unsigned long long)g_shot_t[g_ishot]); write_ppm(nm, fb, w, h); g_ishot++; }
    memcpy(g_last, fb, (size_t)w * h * 2);
    if (g_dump && g_dump_every > 0 && (g_flushes % g_dump_every) == 0) {
        char name[512]; snprintf(name, sizeof name, "%s%05d.ppm", g_dump, g_flushes);
        FILE *f = fopen(name, "wb"); if (!f) return;
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
            uint16_t p = fb[i]; uint8_t rgb[3] = {(uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
}
void host_audio_load(int clip, const void *data, uint32_t size) { ap_load(clip, data, size); if (g_state) fprintf(g_state, "t=%llu A load clip=%d size=%u\n", (unsigned long long)(g_now - g_T0), clip, size); if (gc1_trace) { uint64_t h = 0xcbf29ce484222325ull; const uint8_t *p = data; for (uint32_t i = 0; i < size; i++) h = (h ^ p[i]) * 0x100000001b3ull; fprintf(gc1_trace, "audio load clip=%d size=%u hash=%016llx\n", clip, size, (unsigned long long)h); } }
void host_audio_play(int clip, int loop) { ap_play(clip, loop); if (g_state) fprintf(g_state, "t=%llu A play clip=%d loop=%d\n", (unsigned long long)(g_now - g_T0), clip, loop); if (gc1_trace) fprintf(gc1_trace, "audio play clip=%d loop=%d\n", clip, loop); }
void host_audio_stop(int clip) { ap_stop(clip); if (g_state) fprintf(g_state, "t=%llu A stop clip=%d\n", (unsigned long long)(g_now - g_T0), clip); if (gc1_trace) fprintf(gc1_trace, "audio stop clip=%d\n", clip); }
void host_audio_free(int clip) { ap_free(clip); if (g_state) fprintf(g_state, "t=%llu A free clip=%d\n", (unsigned long long)(g_now - g_T0), clip); }
void host_audio_volume(int v) { if (gc1_trace) fprintf(gc1_trace, "audio volume %d\n", v); }
void host_vibrate(int l, int ms) { if (gc1_trace) fprintf(gc1_trace, "vibrate %d %d\n", l, ms); }

typedef struct { long ms; int key, press; uint32_t addr, size, val; char path[260]; } KeyEv;   /* press=3: salvar estado em path, 4: carregar */
#define KEYEV_UNUSED   /* press=2: poke (escreve val em G[addr], size bytes) */

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s <recursos> <saves> [--frames N] [--trace f] [--keys f] [--dump pfx] [--dump-every K]\n", argv[0]); return 1; }
    int frames = 60; const char *tr = NULL, *keys = NULL;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) tr = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) g_dump = argv[++i];
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) g_dump_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) { for (char *t = strtok(argv[++i], ","); t && g_nshot < 64; t = strtok(NULL, ",")) g_shot_t[g_nshot++] = strtoull(t, 0, 10); }
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) g_loadsnap = argv[++i];
        else if (!strcmp(argv[i], "--wraplog") && i + 1 < argc) gc1_wraplog(fopen(argv[++i], "w"));
        else if (!strcmp(argv[i], "--smart")) g_smart_from = 0;
        else if (!strcmp(argv[i], "--smart-from") && i + 1 < argc) g_smart_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) g_state = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "--cov") && i + 1 < argc) g_cov = argv[++i];
    }
    if (tr) gc1_trace = fopen(tr, "w");
    static KeyEv kev[4096]; int nk = 0, ki = 0;
    if (keys) {
        FILE *f = fopen(keys, "r"); char ln[256];
        while (f && fgets(ln, sizeof ln, f) && nk < 4096) {
            long ms; int k; char t; unsigned a, sz, v;
            char pth[260];
            if (sscanf(ln, "%ld S %259s", &ms, pth) == 2) { kev[nk] = (KeyEv){ms, 0, 3, 0, 0, 0, {0}}; strcpy(kev[nk++].path, pth); }
            else if (sscanf(ln, "%ld L %259s", &ms, pth) == 2) { kev[nk] = (KeyEv){ms, 0, 4, 0, 0, 0, {0}}; strcpy(kev[nk++].path, pth); }
            else if (sscanf(ln, "%ld P %x %u %i", &ms, &a, &sz, &v) == 4) kev[nk++] = (KeyEv){ms, 0, 2, a, sz, v, {0}};
            else if (sscanf(ln, "%ld C %x %u %i", &ms, &a, &sz, &v) == 4) kev[nk++] = (KeyEv){ms, 0, 5, a, sz, v, {0}};   /* chama funcao do guest: addr (Thumb) a0 a1 */
            else if (sscanf(ln, "%ld %d %c", &ms, &k, &t) == 3) kev[nk++] = (KeyEv){ms, k, t == 'p', 0, 0, 0, {0}};
        }
        if (f) fclose(f);
    }
    const uint64_t T0 = 1700000000000ull; uint64_t now = T0; g_T0 = T0; g_now = now;
    gc1_set_time(now); ap_init(&g_aout);
    int rc = gc1_boot(argv[1], argv[2]); if (rc) { fprintf(stderr, "boot falhou (%d)\n", rc); return rc; }
    if (g_loadsnap) { int r_ = gc1_snapshot_load(g_loadsnap); fprintf(stderr, "estado carregado %s: %d\n", g_loadsnap, r_); if (r_) return 4; } else gc1_start();
    while (g_flushes < frames && !gc1_should_exit()) {
        int64_t due = gc1_next_due(); int64_t nk_due = ki < nk ? (int64_t)(T0 + kev[ki].ms) : -1;
        int64_t next = due < 0 ? nk_due : (nk_due < 0 ? due : (nk_due < due ? nk_due : due));
        if (next < 0) { fprintf(stderr, "sem timers nem teclas pendentes (travou?) apos %d quadros\n", g_flushes); break; }
        if ((uint64_t)next > now) now = (uint64_t)next;
        gc1_set_time(now); g_now = now;
        if (!g_smart_on && g_smart_from >= 0 && (long)(now - T0) >= g_smart_from) { gc1_input_enable(1); g_smart_on = 1; }
        while (ki < nk && (uint64_t)(T0 + kev[ki].ms) <= now) { if (kev[ki].press == 3 || kev[ki].press == 4) { int r_ = kev[ki].press == 3 ? gc1_snapshot_save(kev[ki].path) : gc1_snapshot_load(kev[ki].path); fprintf(stderr, "estado %s %s: %d\n", kev[ki].press == 3 ? "salvo" : "carregado", kev[ki].path, r_); if (g_state) fprintf(g_state, "t=%llu STATE %s r=%d\n", (unsigned long long)(g_now - g_T0), kev[ki].press == 3 ? "save" : "load", r_); ki++; continue; }
            if (kev[ki].press == 5) { uint32_t ca[2] = {kev[ki].size, kev[ki].val}; gc1_call(kev[ki].addr | 1u, 2, ca); ki++; continue; }
            if (kev[ki].press == 2) { for (uint32_t b = 0; b < kev[ki].size; b++) G[kev[ki].addr + b] = (uint8_t)(kev[ki].val >> (8 * b)); ki++; continue; } gc1_input_key(kev[ki].key, kev[ki].press); if (g_state) { char w[32]; snprintf(w, sizeof w, "K%c%d", kev[ki].press ? 43 : 45, kev[ki].key); log_state(w); } ki++; }
        gc1_run_timers(); ap_tick();
    }
    fprintf(stderr, "ok: %d quadros, t=%llu ms virtuais\n", g_flushes, (unsigned long long)(now - T0));
    if (g_cov) { int n = gc1_cov_dump(g_cov); fprintf(stderr, "cobertura: %d blocos\n", n); }
    if (gc1_trace) fclose(gc1_trace);
    if (g_state) fclose(g_state);
    return 0;
}

static void write_ppm(const char *name, const uint16_t *fb, int w, int h) {
    FILE *f = fopen(name, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) { uint16_t p = fb[i]; uint8_t rgb[3] = {(uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}

/* o que a plataforma real tocaria depois da politica de audio (musica em loop + efeitos por cima) */
static void out_music(uint64_t h, int on) { if (g_state) fprintf(g_state, "t=%llu M music %s %016llx\n", (unsigned long long)(g_now - g_T0), on ? "on " : "off", (unsigned long long)h); }
static void out_sfx(uint64_t h) { if (g_state) fprintf(g_state, "t=%llu M sfx %016llx\n", (unsigned long long)(g_now - g_T0), (unsigned long long)h); }
