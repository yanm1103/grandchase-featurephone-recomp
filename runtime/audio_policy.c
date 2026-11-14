#include <string.h>
#include "audio_policy.h"
static AudioOut g_out; static uint64_t g_hash[64]; static uint64_t g_mus; static int g_mus_on, g_stop_pending;
uint64_t ap_fnv64(const void *p, uint32_t n) { const uint8_t *b = p; uint64_t h = 0xcbf29ce484222325ull; while (n--) h = (h ^ *b++) * 0x100000001b3ull; return h; }
void ap_init(const AudioOut *out) { g_out = *out; memset(g_hash, 0, sizeof g_hash); g_mus = 0; g_mus_on = 0; g_stop_pending = 0; }
void ap_load(int id, const void *data, uint32_t size) { if (id >= 0 && id < 64) g_hash[id] = ap_fnv64(data, size); }
void ap_play(int id, int loop) {
    if (id < 0 || id >= 64 || !g_hash[id]) return;
    uint64_t h = g_hash[id];
    if (loop) {
        g_stop_pending = 0;
        if (g_mus_on && g_mus == h) return;                 /* ja esta tocando: nao reinicia */
        g_mus = h; g_mus_on = 1; g_out.music(h, 1);
    } else {
        g_stop_pending = 0;                                  /* parar+carregar efeito = troca de clipe, a musica continua */
        g_out.sfx(h);
    }
}
void ap_stop(int id) { if (id >= 0 && id < 64 && g_mus_on && g_hash[id] == g_mus) g_stop_pending = 1; }
void ap_free(int id) { (void)id; }
void ap_tick(void) { if (g_stop_pending) { g_stop_pending = 0; g_mus_on = 0; g_out.music(g_mus, 0); } }
void ap_state_save(FILE *f) { fwrite(g_hash, sizeof g_hash, 1, f); fwrite(&g_mus, sizeof g_mus, 1, f); fwrite(&g_mus_on, sizeof g_mus_on, 1, f); }
void ap_state_load(FILE *f) {
    if (fread(g_hash, sizeof g_hash, 1, f) != 1 || fread(&g_mus, sizeof g_mus, 1, f) != 1 || fread(&g_mus_on, sizeof g_mus_on, 1, f) != 1) return;
    g_stop_pending = 0; g_out.music(g_mus, g_mus_on);          /* a plataforma recomeca a faixa que estava tocando (ou silencia) */
}
