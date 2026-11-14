/* gfx_filter.h - filtros de imagem (shaders) do quadro do jogo: NORMAL, LCD, CRT, xBRZ e ScaleFX (shaders do libretro / Miki's World).
 * Usa o renderer GPU do SDL3 (SDL_CreateGPURenderer, Vulkan + SPIR-V) com fragment shaders customizados (SDL_GPURenderState) so no quadrado do jogo;
 * o resto da tela (controles de toque, ajuda, avisos) continua sendo desenhado normalmente. Se o dispositivo GPU nao puder ser criado, cai no renderer comum
 * e so existe o filtro NORMAL. Shaders em platform/shaders/*.frag (README.md la explica a cadeia do ScaleFX); tools/build_shaders.py gera shaders_spv.h (versionado).
 * LCD/CRT/xBRZ: um passe direto na janela. ScaleFX: 5 passes em texturas-alvo (4 do tamanho do jogo em half float + 1 em 3x) e o resultado vai para a tela com filtro linear.
 * Variaveis de ambiente: GC1_NOGPU=1 forca o renderer comum; GC1_GPULOG=1 mostra o log do driver. O filtro escolhido fica em <saves>/filtro.txt (nome). */
#ifndef GC1_GFX_FILTER_H
#define GC1_GFX_FILTER_H
#include <SDL3/SDL_vulkan.h>
#include "shaders_spv.h"

enum { FLTR_NORMAL, FLTR_LCD, FLTR_CRT, FLTR_XBRZ, FLTR_SCALEFX, FLTR_COUNT };
static const char *const g_flt_name[FLTR_COUNT] = { "NORMAL", "LCD", "CRT", "XBRZ", "SCALEFX" };
static SDL_GPUDevice *g_gpu;
static SDL_GPURenderState *g_flt_state[FLTR_COUNT];            /* LCD, CRT, XBRZ: um passe */
static SDL_GPUShader *g_sfx_shader[5]; static SDL_GPURenderState *g_sfx_state[5];
static SDL_Texture *g_sfx_tex[5]; static int g_sfx_ready, g_sfx_failed;      /* g_sfx_tex[0..3]: gw x gh (half float); [4]: 3x */
static int g_avail[FLTR_COUNT], g_flt_n = 1, g_filter;          /* g_avail[k] = filtro k utilizavel; g_filter = enumerador atual */
static char g_flt_file[620];

static SDL_Renderer *gfx_create_renderer(SDL_Window *w) {
    SDL_Renderer *r = NULL;
    if (SDL_getenv("GC1_GPULOG")) SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_VERBOSE);
    if (!SDL_getenv("GC1_NOGPU")) {
        /* o renderer do SDL so precisa do basico: desliga recursos opcionais que GPUs de celular (Mali) podem nao ter, senao o Vulkan "nao acha dispositivo adequado" */
        SDL_PropertiesID p = SDL_CreateProperties();
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetStringProperty(p, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan");
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(p, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
        g_gpu = SDL_CreateGPUDeviceWithProperties(p);
        SDL_DestroyProperties(p);
        if (g_gpu) { r = SDL_CreateGPURenderer(g_gpu, w); if (!r) { SDL_Log("renderer GPU: %s", SDL_GetError()); SDL_DestroyGPUDevice(g_gpu); g_gpu = NULL; } }
        else SDL_Log("dispositivo GPU (Vulkan): %s", SDL_GetError());
    }
    if (!r) r = SDL_CreateRenderer(w, NULL);
    return r;
}
static SDL_GPUShader *gfx_make_shader(const unsigned char *code, size_t n, int samplers, int ubufs) {
    SDL_GPUShaderCreateInfo si; SDL_zero(si);
    si.code = code; si.code_size = n; si.entrypoint = "main"; si.format = SDL_GPU_SHADERFORMAT_SPIRV; si.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    si.num_samplers = (Uint32)samplers; si.num_uniform_buffers = (Uint32)ubufs;
    SDL_GPUShader *sh = SDL_CreateGPUShader(g_gpu, &si);
    if (!sh) SDL_Log("shader: %s", SDL_GetError());
    return sh;
}
static SDL_GPURenderState *gfx_make_state(SDL_Renderer *r, SDL_GPUShader *sh, SDL_GPUTexture *extra) {
    if (!sh) return NULL;
    SDL_GPURenderStateCreateInfo ci; SDL_zero(ci); ci.fragment_shader = sh;
    SDL_GPUTextureSamplerBinding sb; SDL_zero(sb);
    if (extra) {                                                  /* textura adicional (binding 1): amostrador NEAREST + CLAMP */
        SDL_GPUSamplerCreateInfo sc; SDL_zero(sc);
        sc.min_filter = sc.mag_filter = SDL_GPU_FILTER_NEAREST; sc.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sc.address_mode_u = sc.address_mode_v = sc.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sb.sampler = SDL_CreateGPUSampler(g_gpu, &sc); sb.texture = extra; ci.num_sampler_bindings = 1; ci.sampler_bindings = &sb;
    }
    SDL_GPURenderState *st = SDL_CreateGPURenderState(r, &ci);
    if (!st) SDL_Log("render state: %s", SDL_GetError());
    return st;
}
static void gfx_save_filter(void) { FILE *f = fopen(g_flt_file, "w"); if (f) { fprintf(f, "%s\n", g_flt_name[g_filter]); fclose(f); } }
static void gfx_init(SDL_Renderer *r, const char *savedir) {
    SDL_snprintf(g_flt_file, sizeof g_flt_file, "%s/filtro.txt", savedir);
    g_avail[FLTR_NORMAL] = 1;
    if (!g_gpu) return;
    g_flt_state[FLTR_LCD] = gfx_make_state(r, gfx_make_shader(spv_lcd, sizeof spv_lcd, 1, 1), NULL);
    g_flt_state[FLTR_CRT] = gfx_make_state(r, gfx_make_shader(spv_crt, sizeof spv_crt, 1, 1), NULL);
    g_flt_state[FLTR_XBRZ] = gfx_make_state(r, gfx_make_shader(spv_xbrz, sizeof spv_xbrz, 1, 1), NULL);
    static const unsigned char *const sc[5] = { spv_scalefx0, spv_scalefx1, spv_scalefx2, spv_scalefx3, spv_scalefx4 };
    static const size_t sn[5] = { sizeof spv_scalefx0, sizeof spv_scalefx1, sizeof spv_scalefx2, sizeof spv_scalefx3, sizeof spv_scalefx4 };
    int ok = 1; for (int i = 0; i < 5; i++) { g_sfx_shader[i] = gfx_make_shader(sc[i], sn[i], (i == 2 || i == 4) ? 2 : 1, 0); if (!g_sfx_shader[i]) ok = 0; }
    for (int k = FLTR_LCD; k <= FLTR_XBRZ; k++) g_avail[k] = g_flt_state[k] != NULL;
    g_avail[FLTR_SCALEFX] = ok;                                   /* os estados do ScaleFX precisam das texturas: criados na primeira vez que o filtro e usado */
    g_flt_n = 0; for (int k = 0; k < FLTR_COUNT; k++) g_flt_n += g_avail[k];
    FILE *f = fopen(g_flt_file, "r"); char nm[32];
    if (f) { if (fscanf(f, "%31s", nm) == 1) for (int k = 0; k < FLTR_COUNT; k++) if (g_avail[k] && !SDL_strcasecmp(nm, g_flt_name[k])) g_filter = k; fclose(f); }
    SDL_Log("filtros de imagem: %d disponiveis, atual %s", g_flt_n, g_flt_name[g_filter]);
}
static const char *gfx_filter_name(void) { return g_flt_name[g_filter]; }
static void gfx_filter_next(void) {
    do g_filter = (g_filter + 1) % FLTR_COUNT; while (!g_avail[g_filter]);
    gfx_save_filter();
}
static SDL_Texture *gfx_target(SDL_Renderer *r, SDL_PixelFormat fmt, int w, int h) {
    SDL_Texture *t = SDL_CreateTexture(r, fmt, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) { SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE); }
    return t;
}
static SDL_GPUTexture *gfx_gpu_tex(SDL_Texture *t) { return (SDL_GPUTexture *)SDL_GetPointerProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, NULL); }
/* texturas-alvo e estados do ScaleFX (depois que a textura do jogo existe); em falha, marca como indisponivel e volta para NORMAL */
static int gfx_sfx_setup(SDL_Renderer *r, SDL_Texture *game, int gw, int gh) {
    if (g_sfx_ready) return 1;
    if (g_sfx_failed) return 0;
    SDL_PixelFormat ff = SDL_PIXELFORMAT_RGBA64_FLOAT;
    for (int i = 0; i < 4; i++) {
        g_sfx_tex[i] = gfx_target(r, ff, gw, gh);
        if (!g_sfx_tex[i] && i == 0) { SDL_Log("ScaleFX: sem alvo half float (%s), usando 8 bits", SDL_GetError()); ff = SDL_PIXELFORMAT_ABGR8888; g_sfx_tex[0] = gfx_target(r, ff, gw, gh); }
        if (!g_sfx_tex[i]) { g_sfx_failed = 1; return 0; }
    }
    g_sfx_tex[4] = gfx_target(r, SDL_PIXELFORMAT_ABGR8888, gw * 3, gh * 3);
    if (!g_sfx_tex[4]) { g_sfx_failed = 1; return 0; }
    SDL_SetTextureScaleMode(g_sfx_tex[4], SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(game, SDL_BLENDMODE_NONE);
    for (int i = 0; i < 5; i++) {
        SDL_GPUTexture *extra = i == 2 ? gfx_gpu_tex(g_sfx_tex[0]) : i == 4 ? gfx_gpu_tex(game) : NULL;
        if ((i == 2 || i == 4) && !extra) { g_sfx_failed = 1; return 0; }
        g_sfx_state[i] = gfx_make_state(r, g_sfx_shader[i], extra);
        if (!g_sfx_state[i]) { g_sfx_failed = 1; return 0; }
    }
    g_sfx_ready = 1; return 1;
}
/* desenha o quadro do jogo (dst = NULL: janela inteira, com a apresentacao logica) com o filtro atual */
static void gfx_draw_game(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *dst, int gw, int gh) {
    int f = g_filter;
    if (f == FLTR_SCALEFX && !gfx_sfx_setup(r, t, gw, gh)) { g_avail[FLTR_SCALEFX] = 0; g_flt_n--; g_filter = f = FLTR_NORMAL; SDL_Log("ScaleFX indisponivel neste aparelho: voltando para NORMAL"); }
    if (f == FLTR_SCALEFX) {
        SDL_Texture *src[5] = { t, g_sfx_tex[0], g_sfx_tex[1], g_sfx_tex[2], g_sfx_tex[3] };
        for (int i = 0; i < 5; i++) { SDL_SetRenderTarget(r, g_sfx_tex[i]); SDL_SetGPURenderState(r, g_sfx_state[i]); SDL_RenderTexture(r, src[i], NULL, NULL); }
        SDL_SetGPURenderState(r, NULL); SDL_SetRenderTarget(r, NULL);
        SDL_RenderTexture(r, g_sfx_tex[4], NULL, dst);
        return;
    }
    SDL_GPURenderState *st = f ? g_flt_state[f] : NULL;
    if (st) {
        float x0, y0, x1, y1;                                        /* pixels de saida por pixel do jogo = largura do quadro em pixels fisicos / largura do jogo */
        SDL_FRect full = { 0, 0, (float)gw, (float)gh }; const SDL_FRect *d = dst ? dst : &full;
        SDL_RenderCoordinatesToWindow(r, d->x, d->y, &x0, &y0); SDL_RenderCoordinatesToWindow(r, d->x + d->w, d->y + d->h, &x1, &y1);
        float density = SDL_GetWindowPixelDensity(SDL_GetRenderWindow(r)); if (density <= 0) density = 1;
        float ow = (x1 - x0) * density, oh = (y1 - y0) * density;
        float prm[4];
        if (f == FLTR_XBRZ) { prm[0] = ow; prm[1] = oh; prm[2] = prm[3] = 0; }          /* OutputSize = tamanho do retangulo do jogo em pixels fisicos */
        else { prm[0] = (float)gw; prm[1] = (float)gh; prm[2] = ow / (float)gw; prm[3] = 0; }
        SDL_SetGPURenderStateFragmentUniforms(st, 0, prm, sizeof prm);
        SDL_SetGPURenderState(r, st);
    }
    SDL_RenderTexture(r, t, NULL, dst);
    if (st) SDL_SetGPURenderState(r, NULL);
}
#endif
