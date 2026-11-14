/* main_sdl.c - plataforma SDL3 do recomp do Grand Chase 1: janela, teclado, gamepad, audio.
 *   gc1 [--res pasta] [--save pasta] [--audio pasta] [--scale N] [--fullscreen]
 * A pasta de recursos tem os arquivos do jogo (o jar PT extraido, com binary.mod). */
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#endif
#include "gc1.h"

static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex; static int g_touch_ui;
static int g_dirty, g_gw = 240, g_gh = 320; static const char *g_audio_dir = "audio";
static SDL_AudioDeviceID g_dev;
#include "gfx_filter.h"
static FILE *g_rec; static uint64_t g_rec_t0;      /* --record: grava as teclas como roteiro para o headless */

/* ---------------- video ---------------- */
void host_flush(const uint16_t *fb, int w, int h) {
    if (!tex) {
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        if (!g_touch_ui) SDL_SetRenderLogicalPresentation(ren, w, h, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    }
    static uint32_t rgba[240 * 320 * 2]; size_t np = (size_t)w * (size_t)h; if (np > sizeof rgba / sizeof *rgba) np = sizeof rgba / sizeof *rgba;
    for (size_t i = 0; i < np; i++) { uint32_t p = fb[i], r5 = p >> 11, g6 = (p >> 5) & 63, b5 = p & 31; rgba[i] = 0xff000000u | ((b5 << 3 | b5 >> 2) << 16) | ((g6 << 2 | g6 >> 4) << 8) | (r5 << 3 | r5 >> 2); }
    SDL_UpdateTexture(tex, NULL, rgba, w * 4);
    g_gw = w; g_gh = h; g_dirty = 1;
}

/* ---------------- audio: clipe MMF -> WAV pre-renderizado, casado pelo hash FNV-64 dos bytes ----------------
 * A politica (musica em loop + efeitos mixados por cima, sem reiniciar a musica a cada golpe) esta em runtime/audio_policy.c;
 * aqui so se toca: 1 fluxo de musica e 8 de efeitos, todos ligados ao dispositivo (o SDL mixa). */
#include "audio_policy.h"
/* decodificador Ogg Vorbis (stb_vorbis, dominio publico; incluido no fim deste arquivo porque suas macros colidem com nomes daqui):
 * a musica vai no APK em .ogg e e decodificada ao tocar */
extern int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *sample_rate, short **output);
typedef struct { uint64_t hash; Uint8 *wav; Uint32 len; SDL_AudioSpec spec; int fail, ogg; } Wav;
static Wav wavs[256]; static int nwavs;
static SDL_AudioStream *mus_st, *sfx_st[8]; static int sfx_next; static Wav *mus_w;
static float g_gain = 1.0f;
static void wav_unload(Wav *w) { if (!w->wav) return; if (w->ogg) free(w->wav); else SDL_free(w->wav); w->wav = NULL; w->len = 0; }
/* Procura <hash>.ogg (musicas, pasta de audio) e, se nao houver, <hash>.wav (efeitos, pequenos). Clipes grandes (musica) ja decodificados
 * ocupam ~10 MB cada: ao carregar um novo, os outros grandes (menos o que toca agora) saem da memoria e voltam a ser decodificados se preciso. */
static Wav *get_wav(uint64_t h) {
    Wav *w = NULL;
    for (int i = 0; i < nwavs; i++) if (wavs[i].hash == h) { w = &wavs[i]; break; }
    if (w && w->fail) return NULL;
    if (w && w->wav) return w;
    if (!w) { if (nwavs >= 256) return NULL; w = &wavs[nwavs++]; memset(w, 0, sizeof *w); w->hash = h; }
    char path[600]; SDL_snprintf(path, sizeof path, "%s/%016llx.ogg", g_audio_dir, (unsigned long long)h);
    size_t fsz = 0; void *file = SDL_LoadFile(path, &fsz);
    if (file) {
        int ch = 0, rate = 0; short *pcm = NULL; int n = stb_vorbis_decode_memory((const unsigned char *)file, (int)fsz, &ch, &rate, &pcm);
        SDL_free(file);
        if (n <= 0 || !pcm) { w->fail = 1; SDL_Log("audio: falha ao decodificar %s", path); return NULL; }
        w->wav = (Uint8 *)pcm; w->len = (Uint32)n * (Uint32)ch * 2; w->ogg = 1; w->spec.format = SDL_AUDIO_S16; w->spec.channels = ch; w->spec.freq = rate;
        SDL_Log("audio: %016llx.ogg decodificado (%d canais, %d Hz, %.1f s)", (unsigned long long)h, ch, rate, (double)n / rate);
    } else {
        SDL_snprintf(path, sizeof path, "%s/%016llx.wav", g_audio_dir, (unsigned long long)h);
        if (!SDL_LoadWAV(path, &w->spec, &w->wav, &w->len)) { w->wav = NULL; w->fail = 1; SDL_Log("audio: sem clipe para %016llx", (unsigned long long)h); return NULL; }
        w->ogg = 0;
    }
    if (w->len > (1u << 20)) for (int i = 0; i < nwavs; i++) if (&wavs[i] != w && &wavs[i] != mus_w && wavs[i].len > (1u << 20)) wav_unload(&wavs[i]);
    return w;
}
static SDL_AudioStream *stream_for(SDL_AudioStream **slot, const SDL_AudioSpec *spec) {
    if (!g_dev) return NULL;
    if (!*slot) { *slot = SDL_CreateAudioStream(spec, NULL); if (*slot) SDL_BindAudioStream(g_dev, *slot); }
    else SDL_SetAudioStreamFormat(*slot, spec, NULL);
    if (*slot) SDL_SetAudioStreamGain(*slot, g_gain);
    return *slot;
}
static void out_music(uint64_t h, int on) {
    if (!on) { if (mus_st) SDL_ClearAudioStream(mus_st); mus_w = NULL; return; }
    Wav *w = get_wav(h); if (!w) { if (mus_st) SDL_ClearAudioStream(mus_st); mus_w = NULL; return; }
    if (!stream_for(&mus_st, &w->spec)) return;
    SDL_ClearAudioStream(mus_st); SDL_PutAudioStreamData(mus_st, w->wav, (int)w->len); mus_w = w;
}
static void out_sfx(uint64_t h) {
    Wav *w = get_wav(h); if (!w) return;
    SDL_AudioStream **slot = &sfx_st[sfx_next]; sfx_next = (sfx_next + 1) % 8;
    if (!stream_for(slot, &w->spec)) return;
    SDL_ClearAudioStream(*slot); SDL_PutAudioStreamData(*slot, w->wav, (int)w->len);
}
static const AudioOut g_audio_out = { out_music, out_sfx };
void host_audio_load(int id, const void *data, uint32_t size) { ap_load(id, data, size); }
void host_audio_play(int id, int loop) { ap_play(id, loop); }
void host_audio_stop(int id) { ap_stop(id); }
void host_audio_free(int id) { ap_free(id); }
void host_audio_volume(int v) {
    if (v < 0) v = 0; if (v > 100) v = 100; g_gain = v / 100.0f;
    if (mus_st) SDL_SetAudioStreamGain(mus_st, g_gain);
    for (int i = 0; i < 8; i++) if (sfx_st[i]) SDL_SetAudioStreamGain(sfx_st[i], g_gain);
}
void host_vibrate(int level, int ms) { (void)level; (void)ms; }
static void audio_pump(void) {
    ap_tick();
    if (mus_w && mus_st && SDL_GetAudioStreamQueued(mus_st) < (int)(mus_w->len / 2)) SDL_PutAudioStreamData(mus_st, mus_w->wav, (int)mus_w->len);
}

/* ---------------- entrada: teclado/gamepad -> codigos de tecla do WIPI ---------------- */
#include "keymap.h"
static int map_pad(SDL_GamepadButton b) {
    switch (b) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return -1; case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return -2;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return -3; case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return -4;
        case SDL_GAMEPAD_BUTTON_SOUTH: return -5; case SDL_GAMEPAD_BUTTON_WEST: return -6; case SDL_GAMEPAD_BUTTON_EAST: return -7;
        case SDL_GAMEPAD_BUTTON_NORTH: return 53; case SDL_GAMEPAD_BUTTON_START: return -5; case SDL_GAMEPAD_BUTTON_BACK: return -7;
        default: return 0;
    }
}
static uint64_t now_ms(void);
static void rec_key(int k, int press) { if (g_rec) { fprintf(g_rec, "%llu %d %c\n", (unsigned long long)(now_ms() - g_rec_t0), k, press ? 'p' : 'r'); fflush(g_rec); } }
static char g_toast[80]; static uint64_t g_toast_until, g_state_ok;
static int held[512];                  /* tecla do celular que cada tecla fisica apertou */
static int g_bg; static uint64_t g_bg_t0;   /* app em segundo plano (Android): o jogo nao ve esse intervalo */
static uint64_t g_skew, g_pause_t0;      /* tempo em que o jogo ficou pausado (tela de ajuda): o jogo nao ve esse intervalo */
#include "touch.h"
static uint64_t game_now(void);
static uint64_t now_ms(void) { SDL_Time t; SDL_GetCurrentTime(&t); return (uint64_t)t / 1000000ull; }
static uint64_t game_now(void) { return now_ms() - g_skew; }
static void release_held(void) { for (int i = 0; i < 512; i++) if (held[i]) { rec_key(held[i], 0); gc1_input_key(held[i], 0); held[i] = 0; } }
static const char *g_savedir;
/* Android: ao minimizar o SDL enfileira WILL_ENTER_BACKGROUND e ja bloqueia o app, entao a foto automatica tem que sair AQUI (observador de eventos, mesma thread do jogo) */
static int state_file(int save, const char *path, int quiet, const char *ok_msg);
static bool SDLCALL bg_watch(void *ud, SDL_Event *e) {
    (void)ud;
    if ((e->type == SDL_EVENT_WILL_ENTER_BACKGROUND || e->type == SDL_EVENT_TERMINATING) && !g_bg && g_savedir) {
        char path[620]; SDL_snprintf(path, sizeof path, "%s/auto.snap", g_savedir);
        gc1_set_time(game_now()); state_file(1, path, 1, "");
        g_bg = 1; g_bg_t0 = now_ms();
    }
    return true;
}
/* foto completa do jogo: save = 1 grava, 0 carrega. quiet: sem aviso na tela (autosave) */
static int state_file(int save, const char *path, int quiet, const char *ok_msg) {
    release_held();
    int r = save ? gc1_snapshot_save(path) : gc1_snapshot_load(path);
    if (!quiet) {
        SDL_snprintf(g_toast, sizeof g_toast, r ? (save ? "Falha ao salvar o estado" : (r == 1 ? "Nenhum estado salvo" : "Estado incompativel")) : ok_msg);
        g_toast_until = now_ms() + 1800; g_dirty = 1;
    }
    return r;
}

#ifdef __ANDROID__
/* Os arquivos do jogo vao dentro do APK (assets/game, assets/audio, lista em files.txt) e o runtime le com fopen: na primeira execucao
 * (ou quando stamp.txt muda) copia tudo para o armazenamento interno do app. A pasta de saves nunca e sobrescrita. */
static void android_extract(const char *base) {
    char stampf[600]; SDL_snprintf(stampf, sizeof stampf, "%s/stamp.txt", base);
    /* ATENCAO: no Android o SDL_LoadFile de caminho relativo tenta ANTES o armazenamento interno (<files>/<rel>) e so depois o APK. Por isso o carimbo do APK tem nome proprio
     * (apkstamp.txt, nunca existe no interno) e cada arquivo interno e apagado antes de ser lido de novo dos assets; senao a atualizacao copiava os arquivos velhos por cima deles. */
    size_t n1 = 0, n2 = 0; char *a = SDL_LoadFile("apkstamp.txt", &n1), *b = SDL_LoadFile(stampf, &n2);
    int same = a && b && n1 == n2 && !memcmp(a, b, n1); SDL_free(b);
    if (same) { SDL_free(a); return; }
    size_t nl = 0; char *list = SDL_LoadFile("files.txt", &nl); if (!list) { SDL_free(a); return; }
    int total = 0; for (size_t i = 0; i < nl; i++) if (list[i] == '\n') total++;
    int done = 0; char *p = list, *end = list + nl;
    while (p < end) {
        char *e = p; while (e < end && *e != '\n' && *e != '\r') e++;
        char rel[300]; size_t L = (size_t)(e - p); if (L > 0 && L < sizeof rel) {
            memcpy(rel, p, L); rel[L] = 0; size_t len = 0; char dst[900], dir[900]; SDL_snprintf(dst, sizeof dst, "%s/%s", base, rel); SDL_RemovePath(dst);
            void *d = SDL_LoadFile(rel, &len);
            if (d) { SDL_strlcpy(dir, dst, sizeof dir); char *sl = SDL_strrchr(dir, '/'); if (sl) { *sl = 0; SDL_CreateDirectory(dir); } SDL_SaveFile(dst, d, len); SDL_free(d); }
        }
        while (e < end && (*e == '\n' || *e == '\r')) e++;
        p = e; done++;
        if (done % 25 == 0 && ren) {
            char t[64]; SDL_snprintf(t, sizeof t, "Preparando arquivos... %d%%", total ? done * 100 / total : 100);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            int w = 0, h = 0; SDL_GetRenderOutputSize(ren, &w, &h); float k = SDL_max(1.0f, SDL_floorf(w / 280.0f));
            SDL_SetRenderScale(ren, k, k); SDL_RenderDebugText(ren, (w / k - (float)SDL_strlen(t) * 8) / 2, h / k / 2, t); SDL_SetRenderScale(ren, 1, 1); SDL_RenderPresent(ren);
        }
    }
    SDL_SaveFile(stampf, a, n1); SDL_free(a); SDL_free(list);
}
#endif


int main(int argc, char **argv) {
    const char *res = "game", *save = "save", *selftest = NULL, *selftest_state = NULL, *load_state = NULL; int nohelp = 0, selftest_about = 0; int scale = 3, full = 0, raw_input = 0, st_n = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--res") && i + 1 < argc) res = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "--audio") && i + 1 < argc) g_audio_dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--raw-input")) raw_input = 1;
        else if (!strcmp(argv[i], "--touch")) g_touch_ui = 1;                         /* controles de toque (no PC o mouse vira dedo) */
        else if (!strcmp(argv[i], "--selftest-shot") && i + 1 < argc) { selftest = argv[++i]; nohelp = 1; }
        else if (!strcmp(argv[i], "--selftest-state") && i + 1 < argc) selftest_state = argv[++i];   /* F5, depois F9 (eventos sinteticos), captura BMP e sai */
        else if (!strcmp(argv[i], "--selftest-about") && i + 1 < argc) { selftest = argv[++i]; selftest_about = 1; }   /* igual ao --selftest-help, mas abre a tela Sobre */
        else if (!strcmp(argv[i], "--selftest-help") && i + 1 < argc) selftest = argv[++i];   /* abre a ajuda, salva uma captura BMP e sai (para teste automatico) */
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_state = argv[++i];     /* comeca de um save state (ex.: build/pt/states/cidade.snap) */
        else if (!strcmp(argv[i], "--fullscreen")) full = 1;        else if (!strcmp(argv[i], "--record") && i + 1 < argc) g_rec = fopen(argv[++i], "w");
    }
    SDL_CreateDirectory(save);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
#ifdef __ANDROID__
    g_touch_ui = 1; scale = 1; full = 0;     /* janela normal: barra de status e de navegacao do Android continuam visiveis */
    static char rbuf[600], sbuf[600], abuf[600]; const char *abase = SDL_GetAndroidInternalStoragePath();
    SDL_snprintf(rbuf, sizeof rbuf, "%s/game", abase); SDL_snprintf(sbuf, sizeof sbuf, "%s/save", abase); SDL_snprintf(abuf, sizeof abuf, "%s/audio", abase);
    res = rbuf; save = sbuf; g_audio_dir = abuf; SDL_CreateDirectory(save);
#endif
    keymap_load(save); touch_cfg_load(save); g_savedir = save; SDL_AddEventWatch(bg_watch, NULL);
    if (g_touch_ui) SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    win = SDL_CreateWindow("Grand Chase 1 (recomp)", g_touch_ui ? 405 : 240 * scale, g_touch_ui ? 880 : 320 * scale, SDL_WINDOW_RESIZABLE | (full ? SDL_WINDOW_FULLSCREEN : 0));
    ren = win ? gfx_create_renderer(win) : NULL;
    if (!ren) { SDL_Log("janela/renderer: %s", SDL_GetError()); return 1; }
#ifdef __ANDROID__
    android_extract(abase);
#endif
    gfx_init(ren, save);
    g_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL); ap_init(&g_audio_out);
    gc1_set_time(game_now());
    int rc = gc1_boot(res, save);
    if (rc) {
        char msg[900];
        SDL_snprintf(msg, sizeof msg, "Nao consegui iniciar o jogo (codigo %d).\n\nPasta de recursos usada: %s\nEla precisa conter o binary.mod do jar PT (e os .pbp, .mmf etc.).\n\nUse: gc1.exe --res <pasta>", rc, res);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Grand Chase 1", msg, win);
        return rc;
    }
    gc1_start(); g_rec_t0 = now_ms(); if (!raw_input) gc1_input_enable(1);
    if (load_state) { gc1_set_time(game_now()); state_file(0, load_state, 1, ""); }
#ifdef __ANDROID__
    { char path[620]; SDL_snprintf(path, sizeof path, "%s/auto.snap", save); gc1_set_time(game_now());
      if (state_file(0, path, 1, "") == 0) { SDL_snprintf(g_toast, sizeof g_toast, "Continuando de onde parou"); g_toast_until = now_ms() + 2200; g_dirty = 1; }
      SDL_RemovePath(path); }      /* a foto automatica so vale UMA vez (app morto em segundo plano); se ficasse, uma abertura futura voltaria a um ponto velho, por cima do que o jogador salvou depois */
#endif
    SDL_Gamepad *pad = NULL;
    int nj = 0; SDL_JoystickID *ids = SDL_GetGamepads(&nj); if (nj > 0) pad = SDL_OpenGamepad(ids[0]); SDL_free(ids);
    int run = 1;
    while (run && !gc1_should_exit()) {
        gc1_set_time(game_now());
        SDL_Event e;
        int64_t due = gc1_next_due(); int timeout = 8;
        if (due >= 0) { int64_t d = due - (int64_t)game_now(); timeout = d < 0 ? 0 : d > 8 ? 8 : (int)d; }
        if (g_help) timeout = 16;
        int have = SDL_WaitEventTimeout(&e, timeout);
        while (have) {
            switch (e.type) {
                case SDL_EVENT_QUIT: run = 0; break;
                case SDL_EVENT_DID_ENTER_FOREGROUND:
                    if (g_bg) { g_skew += now_ms() - g_bg_t0; g_bg = 0; g_dirty = 1; char ap[620]; SDL_snprintf(ap, sizeof ap, "%s/auto.snap", save); SDL_RemovePath(ap); }   /* voltou: o app nao morreu, a foto ficou velha */
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.repeat) break;
                    if (g_help && g_help_page == 1) {                                        /* Sobre: qualquer tecla fecha */
                        g_help = 0; g_help_page = 0; g_skew += now_ms() - g_pause_t0; g_dirty = 1; break;
                    }
                    if (e.key.key == SDLK_F1 || e.key.key == SDLK_F3) {                      /* F1 = teclas, F3 = sobre (as duas pausam o jogo) */
                        int pg = e.key.key == SDLK_F3;
                        if (g_help && g_help_page != pg) { g_help_page = pg; g_help_wait = 0; g_dirty = 1; break; }
                        g_help_page = pg;
                        g_help = !g_help; g_help_wait = 0; g_dirty = 1;
                        if (g_help) g_pause_t0 = now_ms(); else g_skew += now_ms() - g_pause_t0;
                        if (g_help) for (int i = 0; i < 512; i++) if (held[i]) { rec_key(held[i], 0); gc1_input_key(held[i], 0); held[i] = 0; }
                        break;
                    }
                    if (!g_help && e.key.key == SDLK_F2) {                                  /* filtro de imagem: NORMAL / LCD / CRT ... */
                        gfx_filter_next(); SDL_snprintf(g_toast, sizeof g_toast, "Filtro: %s", gfx_filter_name()); g_toast_until = now_ms() + 1500; g_dirty = 1; break;
                    }
                    if (!g_help && (e.key.key == SDLK_F5 || e.key.key == SDLK_F9)) {      /* save state: foto completa do jogo, em qualquer lugar */
                        char path[620]; SDL_snprintf(path, sizeof path, "%s/estado.snap", save);
                        if (e.key.key == SDLK_F5) state_file(1, path, 0, "Estado salvo (F9 carrega)"); else state_file(0, path, 0, "Estado carregado");
                        break;
                    }
                    if (g_help) { help_key(e.key.key); g_dirty = 1; break; }
                    if (e.key.key == SDLK_F11) { SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break; }
                    { int k = map_key(e.key.key); if (k) { held[(e.key.scancode) & 511] = k; gc1_set_time(game_now()); rec_key(k, 1); gc1_input_key(k, 1); } }
                    break;
                case SDL_EVENT_KEY_UP:
                    { int k = held[(e.key.scancode) & 511]; if (k) { held[(e.key.scancode) & 511] = 0; gc1_set_time(game_now()); rec_key(k, 0); gc1_input_key(k, 0); } }
                    break;
                case SDL_EVENT_FINGER_DOWN:
                    if (g_touch_ui && g_help) { g_help = 0; g_help_page = 0; g_skew += now_ms() - g_pause_t0; g_dirty = 1; }      /* toque fecha o Sobre */
                    else if (g_touch_ui) { gc1_set_time(game_now()); touch_event(e.tfinger.fingerID, e.tfinger.x, e.tfinger.y, 1); } break;
                case SDL_EVENT_FINGER_MOTION: if (g_touch_ui) touch_event(e.tfinger.fingerID, e.tfinger.x, e.tfinger.y, 2); break;
                case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: if (g_touch_ui) touch_event(e.tfinger.fingerID, e.tfinger.x, e.tfinger.y, 0); break;
                case SDL_EVENT_GAMEPAD_ADDED: if (!pad) pad = SDL_OpenGamepad(e.gdevice.which); break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN: { int k = map_pad((SDL_GamepadButton)e.gbutton.button); if (k) { rec_key(k, 1); gc1_input_key(k, 1); } } break;
                case SDL_EVENT_GAMEPAD_BUTTON_UP: { int k = map_pad((SDL_GamepadButton)e.gbutton.button); if (k) { rec_key(k, 0); gc1_input_key(k, 0); } } break;
                default: break;
            }
            have = SDL_PollEvent(&e);
        }
        gc1_set_time(game_now());
        if (g_state_req == 3) { gfx_filter_next(); g_state_req = 0; g_dirty = 1; }           /* botao FILTRO da gaveta (toque) */
        if (g_state_req == 4) { g_state_req = 0; g_help = 1; g_help_page = 1; g_help_wait = 0; g_pause_t0 = now_ms(); g_dirty = 1; }     /* botao SOBRE da gaveta */
        { char nb[96]; if (gc1_net_notice(nb, sizeof nb)) { SDL_snprintf(g_toast, sizeof g_toast, "%s", nb); g_toast_until = now_ms() + 6000; g_dirty = 1; } }   /* cupom ganho no bau */
        if (g_state_req) {
            char path[620]; SDL_snprintf(path, sizeof path, "%s/estado.snap", save);
            if (g_state_req == 1) state_file(1, path, 0, "Estado salvo"); else state_file(0, path, 0, "Estado carregado");
            g_state_req = 0;
        }
        if (!g_help) gc1_run_timers();
        audio_pump();
        if (selftest_state && tex) {
            ++st_n; SDL_Event ke; SDL_zero(ke); ke.type = SDL_EVENT_KEY_DOWN; ke.key.down = true;
            if (st_n == 40) { ke.key.key = SDLK_F5; SDL_PushEvent(&ke); }
            if (st_n == 80) { ke.key.key = SDLK_F9; SDL_PushEvent(&ke); }
            if (st_n == 90) { SDL_Surface *sf = SDL_RenderReadPixels(ren, NULL); if (sf) { SDL_SaveBMP(sf, selftest_state); SDL_DestroySurface(sf); } SDL_Log("selftest-state: %s", g_toast); run = 0; }
        }
        if (selftest && tex) {
            if (++st_n == 30) { g_help = !nohelp; g_help_page = selftest_about; g_dirty = 1; }
            if (st_n == 32) { SDL_Surface *sf = SDL_RenderReadPixels(ren, NULL); if (sf) { SDL_SaveBMP(sf, selftest); SDL_DestroySurface(sf); } run = 0; }
        }
        if (g_dirty && tex) { g_dirty = 0; SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
            if (g_touch_ui) { touch_layout(); gfx_draw_game(ren, tex, &g_game_rect, g_gw, g_gh); touch_draw(ren); } else gfx_draw_game(ren, tex, NULL, g_gw, g_gh);
            if (g_help && g_help_page == 1) about_draw(ren); else if (g_help && !g_touch_ui) help_draw(ren);
            if (now_ms() < g_toast_until) { SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(ren, 0, 0, 0, 200); float tk = g_touch_ui ? SDL_max(1.0f, SDL_floorf(g_scr_w / 240.0f)) : 1.0f; SDL_SetRenderScale(ren, tk, tk); SDL_FRect bx = {0, 0, (g_touch_ui ? g_scr_w : 240) / tk, 14}; SDL_RenderFillRect(ren, &bx); SDL_SetRenderDrawColor(ren, 255, 235, 120, 255); SDL_RenderDebugText(ren, 6, 3, g_toast); SDL_SetRenderScale(ren, 1, 1); g_dirty = 1; }
            SDL_RenderPresent(ren); }
    }
    if (g_rec) fclose(g_rec);
    SDL_Quit();
    return 0;
}

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
