/* touch.h - controles de toque (Android; no PC com --touch, o mouse vira dedo): D-pad, OK, menu/voltar, minimapa, mapa-mundi, atalhos 1 3 7 9 0 * #.
 * Layout em retrato: o jogo (240x320, escala inteira) no topo e os controles na area de baixo.
 * Cada dedo guarda a ultima tecla que apertou; mexer o dedo para outro botao solta um e aperta o outro. Passa tudo por gc1_input_key. */
#ifndef GC1_TOUCH_H
#define GC1_TOUCH_H

typedef struct { int kind; int key; float x, y, w, h; const char *label; } Ctl;     /* kind 0 = botao retangular, 1 = D-pad (x,y = centro, w = raio), 2 = botao redondo (w = raio) */
static Ctl g_ctl[16]; static int g_nctl;
static SDL_FRect g_game_rect; static int g_touch_ui; static int g_scr_w, g_scr_h;
typedef struct { SDL_FingerID id; int used; int ctl; int key; } Finger;
static Finger g_fing[10];
static int g_state_req;        /* pedido dos botoes de estado: 1 = salvar, 2 = carregar (o laco principal atende) */

/* calcula o retangulo do jogo e a posicao dos controles para a janela atual */
static void touch_layout(void) {
    SDL_GetRenderOutputSize(ren, &g_scr_w, &g_scr_h);
    float W = (float)g_scr_w, H = (float)g_scr_h;
    int s = (int)SDL_floorf(SDL_min(W / 240.0f, H * 0.60f / 320.0f)); if (s < 1) s = 1;
    g_game_rect = (SDL_FRect){ SDL_floorf((W - 240.0f * s) / 2), 0, 240.0f * s, 320.0f * s };
    float top = g_game_rect.h + 6, ah = H - top;
    { static int logged; if (!logged) { logged = 1; SDL_Log("gc1 layout: saida %dx%d, escala do jogo %d", g_scr_w, g_scr_h, s); } }
    g_nctl = 0;
    float bh = SDL_min(ah * 0.10f, W * 0.1f), bw = W / 7.0f;
    static const struct { int key; const char *l; } sc[7] = { {49, "1"}, {51, "3"}, {55, "7"}, {57, "9"}, {42, "*"}, {48, "0"}, {35, "#"} };
    for (int i = 0; i < 7; i++) g_ctl[g_nctl++] = (Ctl){0, sc[i].key, i * bw + 3, top + 2, bw - 6, bh, sc[i].l};
    float rowy = top + bh + 20, rowh = bh * 1.1f;                       /* fileira do meio: estado (foto do jogo) e MENU */
    float padtop = rowy + rowh + 12, padbot = H - bh * 1.15f - 14, avail = padbot - padtop;   /* D-pad e OK ocupam o resto */
    float R = SDL_min(W * 0.25f, avail * 0.48f), cy = padtop + avail * 0.5f;
    g_ctl[g_nctl++] = (Ctl){1, 0, W * 0.27f, cy, R, 0, ""};
    float r = SDL_min(W * 0.14f, avail * 0.28f);
    g_ctl[g_nctl++] = (Ctl){2, -5, W * 0.80f, cy + r * 0.2f, r, 0, "OK"};
    g_ctl[g_nctl++] = (Ctl){0, 1001, W * 0.04f, rowy, W * 0.25f, rowh, "SALVAR|ESTADO"};      /* foto do jogo (F5/F9 do PC) */
    g_ctl[g_nctl++] = (Ctl){0, 1002, W * 0.31f, rowy, W * 0.25f, rowh, "CARREGAR|ESTADO"};
    g_ctl[g_nctl++] = (Ctl){0, -16, W * 0.64f, rowy, W * 0.32f, rowh, "MENU"};
    g_ctl[g_nctl++] = (Ctl){0, -6, W * 0.04f, H - bh * 1.15f, W * 0.28f, bh, "MINIMAPA"};
    g_ctl[g_nctl++] = (Ctl){0, -7, W * 0.36f, H - bh * 1.15f, W * 0.28f, bh, "MAPA"};
    g_ctl[g_nctl++] = (Ctl){0, -10, W * 0.68f, H - bh * 1.15f, W * 0.28f, bh, "SALVAR"};
}
static int ctl_dir_key(const Ctl *c, float px, float py) {     /* tecla do D-pad para o ponto, ou 0 (centro morto) */
    float dx = px - c->x, dy = py - c->y, d2 = dx * dx + dy * dy, r = c->w;
    if (d2 > r * r * 1.7f * 1.7f || d2 < r * r * 0.14f * 0.14f * 4) return 0;
    if (SDL_fabsf(dx) > SDL_fabsf(dy)) return dx < 0 ? -3 : -4;
    return dy < 0 ? -1 : -2;
}
static int hit_ctl(float px, float py, int *key) {
    for (int pass = 0; pass < 2; pass++)            /* botoes primeiro, D-pad por ultimo (a zona de toque dele e generosa e invade os vizinhos) */
        for (int i = 0; i < g_nctl; i++) {
            const Ctl *c = &g_ctl[i];
            if ((c->kind == 1) != (pass == 1)) continue;
            if (c->kind == 1) { int k = ctl_dir_key(c, px, py); if (k) { *key = k; return i; } }
            else if (c->kind == 2) { float dx = px - c->x, dy = py - c->y; if (dx * dx + dy * dy <= c->w * c->w * 1.3f * 1.3f) { *key = c->key; return i; } }
            else if (px >= c->x && px <= c->x + c->w && py >= c->y - 6 && py <= c->y + c->h + 6) { *key = c->key; return i; }
        }
    return -1;
}
static Finger *finger_get(SDL_FingerID id, int create) {
    for (int i = 0; i < 10; i++) if (g_fing[i].used && g_fing[i].id == id) return &g_fing[i];
    if (!create) return NULL;
    for (int i = 0; i < 10; i++) if (!g_fing[i].used) { g_fing[i] = (Finger){id, 1, -1, 0}; return &g_fing[i]; }
    return NULL;
}
/* x,y normalizados (0..1) como nos eventos de dedo do SDL; down: 1 apertou, 2 moveu, 0 soltou */
static void touch_event(SDL_FingerID id, float nx, float ny, int down) {
    Finger *f = finger_get(id, down == 1); if (!f) return;
    int key = 0, ci = down ? hit_ctl(nx * g_scr_w, ny * g_scr_h, &key) : -1;
    if (!down || ci < 0) key = 0;
    if (key != f->key) {
        if (f->key && f->key < 1000) { rec_key(f->key, 0); gc1_input_key(f->key, 0); }
        if (key >= 1000) g_state_req = key - 1000;
        else if (key) { rec_key(key, 1); gc1_input_key(key, 1); }
        f->key = key; f->ctl = ci; g_dirty = 1;
    }
    if (!down) f->used = 0;
}
static int ctl_pressed(int key) { for (int i = 0; i < 10; i++) if (g_fing[i].used && g_fing[i].key == key) return 1; return 0; }
static void label_c(SDL_Renderer *r, float cx, float cy, const char *t, float k) {
    SDL_SetRenderScale(r, k, k);
    const char *bar = SDL_strchr(t, '|');
    if (!bar) { float w = (float)SDL_strlen(t) * 8; SDL_RenderDebugText(r, (cx / k) - w / 2, (cy / k) - 4, t); }
    else {      /* duas linhas */
        char a[24]; size_t n = (size_t)(bar - t); if (n > 23) n = 23; SDL_memcpy(a, t, n); a[n] = 0;
        SDL_RenderDebugText(r, (cx / k) - (float)n * 4, (cy / k) - 10, a);
        SDL_RenderDebugText(r, (cx / k) - (float)SDL_strlen(bar + 1) * 4, (cy / k) + 1, bar + 1);
    }
    SDL_SetRenderScale(r, 1, 1);
}
static void circle(SDL_Renderer *r, float cx, float cy, float rad, int fill) {
    for (int i = -(int)rad; i <= (int)rad; i += fill ? 2 : 1) {
        float h = SDL_sqrtf(rad * rad - (float)(i * i));
        if (fill) { SDL_FRect b = {cx - h, cy + i, 2 * h, 2}; SDL_RenderFillRect(r, &b); } else { SDL_RenderPoint(r, cx + h, cy + i); SDL_RenderPoint(r, cx - h, cy + i); SDL_RenderPoint(r, cx + i, cy + h); SDL_RenderPoint(r, cx + i, cy - h); }
    }
}
static void touch_draw(SDL_Renderer *r) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    float ts = SDL_max(1.0f, SDL_floorf(g_scr_w / 360.0f));
    for (int i = 0; i < g_nctl; i++) {
        const Ctl *c = &g_ctl[i];
        if (c->kind == 1) {
            float R = c->w; SDL_SetRenderDrawColor(r, 255, 255, 255, 38); circle(r, c->x, c->y, R, 1);
            static const struct { int k; float dx, dy; const char *l; } arr[4] = { {-1, 0, -0.62f, "^"}, {-2, 0, 0.62f, "v"}, {-3, -0.62f, 0, "<"}, {-4, 0.62f, 0, ">"} };
            for (int j = 0; j < 4; j++) {
                int on = ctl_pressed(arr[j].k);
                if (on) { SDL_SetRenderDrawColor(r, 255, 220, 90, 110); circle(r, c->x + arr[j].dx * R, c->y + arr[j].dy * R, R * 0.30f, 1); }
                SDL_SetRenderDrawColor(r, 255, 255, 255, on ? 255 : 170); label_c(r, c->x + arr[j].dx * R, c->y + arr[j].dy * R, arr[j].l, ts * 2.5f);
            }
        } else if (c->kind == 2) {
            int on = ctl_pressed(c->key);
            SDL_SetRenderDrawColor(r, on ? 255 : 255, on ? 220 : 120, on ? 90 : 120, on ? 170 : 85); circle(r, c->x, c->y, c->w, 1);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 230); label_c(r, c->x, c->y, c->label, ts * 3.0f);
        } else {
            int on = ctl_pressed(c->key);
            SDL_SetRenderDrawColor(r, on ? 255 : 255, on ? 220 : 255, on ? 90 : 255, on ? 150 : 40); SDL_FRect b = {c->x, c->y, c->w, c->h}; SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 190); SDL_RenderRect(r, &b);
            label_c(r, c->x + c->w / 2, c->y + c->h / 2, c->label, ts * (SDL_strchr(c->label, '|') ? 1.2f : SDL_strlen(c->label) > 3 ? 1.6f : 2.2f));
        }
    }
}
#endif
