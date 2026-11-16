/* keymap.h - teclas do PC -> teclas do celular (WIPI), com tela de ajuda (F1) e reatribuicao. Incluido por main_sdl.c.
 * Cada ACAO do jogo tem ate 3 teclas; o arquivo <pasta de saves>/controles.ini guarda as escolhidas ("acao = Tecla1, Tecla2").
 * Digitos 2,4,5,6,8 continuam fixos (mover/atacar como no celular) a menos que estejam ligados a uma acao. */
#ifndef GC1_KEYMAP_H
#define GC1_KEYMAP_H

typedef struct { const char *id, *nome; int wipi; const char *padrao; SDL_Keycode k[3]; } Action;
static Action g_act[] = {
    { "cima",    "Mover: cima",         -1,  "Up,W" },
    { "baixo",   "Mover: baixo",        -2,  "Down,S" },
    { "esq",     "Mover: esquerda",   -3,  "Left,A" },
    { "dir",     "Mover: direita",    -4,  "Right,D" },
    { "ok",      "Atacar / OK / falar",     -5,  "Z,Return,Space" },
    { "voltar",  "Menu / voltar (CLR)",     -16, "Escape,X,Backspace" },
    { "minimapa","Minimapa (sup. esq.)", -6, "Q,Tab" },
    { "mapa",    "Mapa-mundi (sup. dir.)", -7, "E,M" },
    { "salvar",  "Salvar/missoes (Ligar)", -10, "F6,P" },
    { "atalho1", "Atalho 1",                49,  "1" },
    { "atalho3", "Atalho 3",                51,  "3" },
    { "atalho7", "Atalho 7",                55,  "7" },
    { "atalho9", "Atalho 9",                57,  "9" },
    { "atalho0", "Atalho 0",                48,  "0" },
    { "estrela", "Atalho *",                42,  "V,Keypad *" },
    { "cerquilha", "Atalhos extras (#)",    35,  "C,Keypad /" },
};
#define NACT ((int)(sizeof g_act / sizeof g_act[0]))

/* teclas da propria plataforma: nao podem ser ligadas a acoes do jogo */
static int key_reserved(SDL_Keycode k) { return k == SDLK_F1 || k == SDLK_F2 || k == SDLK_F3 || k == SDLK_F5 || k == SDLK_F9 || k == SDLK_F11; }
static void act_parse(Action *a, const char *list) {
    a->k[0] = a->k[1] = a->k[2] = SDLK_UNKNOWN;
    char buf[160]; SDL_strlcpy(buf, list, sizeof buf); int n = 0;
    for (char *p = buf; p && *p && n < 3;) {
        char *c = SDL_strchr(p, ','); if (c) *c = 0;
        while (*p == ' ') p++;
        SDL_Keycode k = SDL_GetKeyFromName(p); if (k != SDLK_UNKNOWN && !key_reserved(k)) a->k[n++] = k;
        p = c ? c + 1 : NULL;
    }
}
static void keymap_defaults(void) { for (int i = 0; i < NACT; i++) act_parse(&g_act[i], g_act[i].padrao); }
static void act_text(const Action *a, char *out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < 3 && a->k[i] != SDLK_UNKNOWN; i++) { if (i) SDL_strlcat(out, ", ", n); SDL_strlcat(out, SDL_GetKeyName(a->k[i]), n); }
}
static char g_keyfile[600];
static void keymap_save(void) {
    FILE *f = fopen(g_keyfile, "w"); if (!f) return;
    fprintf(f, "# teclas do Grand Chase 1 (PC): acao = Tecla1, Tecla2, Tecla3 (nomes do SDL; apague uma linha para voltar ao padrao)\n");
    for (int i = 0; i < NACT; i++) { char t[160]; act_text(&g_act[i], t, sizeof t); fprintf(f, "%s = %s\n", g_act[i].id, t); }
    fclose(f);
}
static void keymap_load(const char *dir) {
    SDL_snprintf(g_keyfile, sizeof g_keyfile, "%s/controles.ini", dir); keymap_defaults();
    FILE *f = fopen(g_keyfile, "r"); if (!f) { keymap_save(); return; }
    char ln[256];
    while (fgets(ln, sizeof ln, f)) {
        if (ln[0] == '#') continue; char *eq = SDL_strchr(ln, '='); if (!eq) continue; *eq = 0;
        char *id = ln; while (*id == ' ') id++; for (char *e = eq - 1; e >= id && (*e == ' '); e--) *e = 0;
        char *v = eq + 1; while (*v == ' ') v++; for (char *e = v + SDL_strlen(v) - 1; e >= v && (*e == '\n' || *e == '\r' || *e == ' '); e--) *e = 0;
        for (int i = 0; i < NACT; i++) if (!SDL_strcmp(g_act[i].id, id)) act_parse(&g_act[i], v);
    }
    fclose(f);
}
/* tecla do PC -> codigo do celular (0 = nao mapeada) */
static int map_key(SDL_Keycode k) {
    for (int i = 0; i < NACT; i++) for (int j = 0; j < 3; j++) if (g_act[i].k[j] == k) return g_act[i].wipi;
    switch (k) {   /* fixas: igual ao celular (123 em cima) */
        case SDLK_KP_ENTER: return -5;
        case SDLK_AC_BACK: return -16;          /* botao voltar do Android = CLR */
        case SDLK_2: case SDLK_KP_2: return 50; case SDLK_4: case SDLK_KP_4: return 52; case SDLK_5: case SDLK_KP_5: return 53;
        case SDLK_6: case SDLK_KP_6: return 54; case SDLK_8: case SDLK_KP_8: return 56;
        case SDLK_KP_0: return 48;
        /* teclado numerico do PC tem 789 em cima e 123 embaixo, o inverso do celular: troca as duas fileiras */
        case SDLK_KP_7: return 49; case SDLK_KP_9: return 51; case SDLK_KP_1: return 55; case SDLK_KP_3: return 57;
        case SDLK_HASH: return 35;
        default: return 0;
    }
}

/* ---- tela de ajuda (F1) ---- */
static int g_help, g_help_sel, g_help_wait, g_help_page;      /* g_help_page: 0 = teclas (F1), 1 = Sobre (F3 / botao da gaveta) */
static void help_draw(SDL_Renderer *r) {
    SDL_SetRenderLogicalPresentation(r, 360, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);   /* fonte de 8 px: 45 colunas */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(r, 0, 0, 0, 250);
    SDL_FRect all = {0, 0, 360, 480}; SDL_RenderFillRect(r, &all);
    float y = 12;
    SDL_SetRenderDrawColor(r, 255, 220, 90, 255); SDL_RenderDebugText(r, 12, y, "TECLAS DO JOGO   (F1 fecha)"); y += 16;
    SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
    SDL_RenderDebugText(r, 12, y, "Cima/Baixo escolhe, Enter troca a tecla,"); y += 11;
    SDL_RenderDebugText(r, 12, y, "Del volta ao padrao. F11: tela cheia."); y += 18;
    for (int i = 0; i < NACT; i++, y += 13) {
        char t[160]; act_text(&g_act[i], t, sizeof t); int sel = (i == g_help_sel);
        if (sel) { SDL_SetRenderDrawColor(r, 60, 60, 110, 255); SDL_FRect bar = {6, y - 3, 348, 13}; SDL_RenderFillRect(r, &bar); }
        SDL_SetRenderDrawColor(r, sel ? 255 : 215, sel ? 255 : 215, sel ? 255 : 215, 255);
        SDL_RenderDebugText(r, 12, y, g_act[i].nome);
        if (sel && g_help_wait) { SDL_SetRenderDrawColor(r, 255, 130, 130, 255); SDL_RenderDebugText(r, 190, y, "aperte a tecla..."); }
        else { SDL_SetRenderDrawColor(r, 120, 220, 255, 255); SDL_RenderDebugText(r, 190, y, t); }
    }
    y += 10; SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
    SDL_RenderDebugText(r, 12, y, "Teclas 2 4 6 8 tambem andam e 5 ataca."); y += 11;
    SDL_RenderDebugText(r, 12, y, "Dois toques na direcao = correr."); y += 11;
    SDL_RenderDebugText(r, 12, y, "Duas direcoes juntas = anda na diagonal."); y += 11;
    SDL_RenderDebugText(r, 12, y, "Segurar uma direcao enquanto ataca ou"); y += 11;
    SDL_RenderDebugText(r, 12, y, "usa habilidade continua andando."); y += 11;
    SDL_RenderDebugText(r, 12, y, "Arquivo: controles.ini (pasta de saves)"); y += 16;
    SDL_SetRenderDrawColor(r, 255, 220, 90, 255); SDL_RenderDebugText(r, 12, y, "F5 = salvar ESTADO (foto do jogo, em"); y += 11;
    SDL_RenderDebugText(r, 12, y, "qualquer lugar)    F9 = carregar estado."); y += 11;
    SDL_RenderDebugText(r, 12, y, "F2 = filtro de imagem (LCD, CRT, xBRZ...)"); y += 11;
    SDL_RenderDebugText(r, 12, y, "F3 = sobre (creditos e contador de wons).");
    SDL_SetRenderLogicalPresentation(r, 240, 320, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
}

/* ---- tela "Sobre" (F3; botao SOBRE da gaveta no toque) ---- */
static void fmt_milhar(char *o, size_t n, uint32_t v) {      /* 12300 -> "12.300" */
    char t[24]; SDL_snprintf(t, sizeof t, "%u", v); int L = (int)SDL_strlen(t), j = 0;
    for (int i = 0; i < L; i++) { if (i && (L - i) % 3 == 0 && j < (int)n - 1) o[j++] = '.'; if (j < (int)n - 1) o[j++] = t[i]; }
    o[j] = 0;
}
/* texto da tela Sobre: um paragrafo por linha da lista, quebrado por palavras; big = 2x. O tamanho da letra se ajusta sozinho a tela (celular em pe: letra grande) */
typedef struct { uint32_t col; int big; const char *t; } AboutLn;
static float about_flow(SDL_Renderer *r, const AboutLn *L, int n, int cols, float x0, float s) {     /* devolve a altura em unidades de 8 px; desenha se r != NULL */
    float y = 2;
    for (int i = 0; i < n; i++) {
        int c = L[i].big ? cols / 2 : cols; float k = L[i].big ? 2.0f : 1.0f; const char *p = L[i].t;
        if (!*p) { y += 6; continue; }
        while (*p) {
            int len = (int)SDL_strlen(p);
            if (len > c) { int b = c; while (b > 0 && p[b] != ' ') b--; len = b > 0 ? b : c; }
            if (r) {
                char buf[128]; SDL_snprintf(buf, sizeof buf, "%.*s", len, p);
                SDL_SetRenderScale(r, s * k, s * k); SDL_SetRenderDrawColor(r, L[i].col >> 16, (L[i].col >> 8) & 255, L[i].col & 255, 255);
                SDL_RenderDebugText(r, x0 / (s * k), y / k, buf);
            }
            y += 11 * k; p += len; while (*p == ' ') p++;
        }
    }
    return y + 2;
}
static void about_draw(SDL_Renderer *r) {
    int pw = 0, ph = 0; SDL_RendererLogicalPresentation pm = SDL_LOGICAL_PRESENTATION_DISABLED; SDL_GetRenderLogicalPresentation(r, &pw, &ph, &pm);
    int W = 0, H = 0; SDL_GetCurrentRenderOutputSize(r, &W, &H); if (W <= 0 || H <= 0) return;
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND); SDL_SetRenderScale(r, 1, 1); SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_FRect all = {0, 0, (float)W, (float)H}; SDL_RenderFillRect(r, &all);
    uint32_t won = 0, med = 0, cup = 0; gc1_net_stats(&won, &med, &cup);
    char w[24], big[48], lm[48], lc[48]; fmt_milhar(w, sizeof w, won);
    SDL_snprintf(big, sizeof big, "%s wons", w); SDL_snprintf(lm, sizeof lm, "Medalhas ganhas: %u", med); SDL_snprintf(lc, sizeof lc, "Cupons ganhos: %u", cup);
    const AboutLn L[] = {
        { 0xffdc5a, 0, "SOBRE   (F3, Esc ou toque fecha)" }, { 0, 0, "" },
        { 0xffffff, 0, "GRAND CHASE   (versao de celular, WIPI)" },
        { 0xc8c8c8, 0, "(c) CJ Internet Corp. & KOG Ltd." },
        { 0xc8c8c8, 0, "Desenvolvimento original: Moazio." }, { 0, 0, "" },
        { 0x96b4ff, 0, "Esta e uma versao recompilada para PC e Android, com traducao para portugues e ajustes de jogabilidade (diagonal, menos grind, salvar estado, filtros de imagem). Projeto de fa, sem fins lucrativos e sem ligacao com os donos do jogo, que seguem com todos os direitos sobre ele." }, { 0, 0, "" },
        { 0xffdc5a, 0, "SERVICO ONLINE (SUBSTITUIDO POR UM FALSO)" },
        { 0xffffff, 0, "Se fosse na epoca, voce teria gasto:" },
        { 0x78ff96, 1, big },
        { 0xc8c8c8, 0, "com pergaminhos de ressurreicao (100) e chaves de bau (500, 900 e 1300), que eram vendidos pela rede. Aqui sao gratis e o total so mostra quanto teria custado." }, { 0, 0, "" },
        { 0xffd070, 0, lm }, { 0xffd070, 0, lc }, { 0, 0, "" },
        { 0x909090, 0, "Cupons e ranking sao inventados: o Grand Chase online da Netmarble nao existe mais." },
    };
    const int n = (int)(sizeof L / sizeof L[0]);
    float bs = 0; int bc = 28;                      /* escolhe a largura (em colunas) que deixa a letra maior sem estourar a tela */
    for (int cols = 26; cols <= 120; cols++) {
        float s = SDL_min(W / (cols * 8.0f + 16), H / (about_flow(NULL, L, n, cols, 0, 1) + 2));
        if (s > bs) { bs = s; bc = cols; }
    }
    float s = bs, x0 = (W - bc * 8 * s) / 2;
    about_flow(r, L, n, bc, x0, s);
    SDL_SetRenderScale(r, 1, 1);
    SDL_SetRenderLogicalPresentation(r, pw, ph, pm);
}
/* devolve 1 se consumiu a tecla */
static int help_key(SDL_Keycode k) {
    if (g_help_wait) {
        if (k == SDLK_ESCAPE || key_reserved(k)) { g_help_wait = 0; return 1; }
        for (int i = 0; i < NACT; i++) for (int j = 0; j < 3; j++) if (g_act[i].k[j] == k) { memmove(&g_act[i].k[j], &g_act[i].k[j + 1], sizeof(SDL_Keycode) * (2 - j)); g_act[i].k[2] = SDLK_UNKNOWN; j--; }
        Action *a = &g_act[g_help_sel]; a->k[2] = a->k[1]; a->k[1] = a->k[0]; a->k[0] = k;   /* a nova vira a principal, as antigas continuam */
        g_help_wait = 0; keymap_save(); return 1;
    }
    switch (k) {
        case SDLK_UP: g_help_sel = (g_help_sel + NACT - 1) % NACT; return 1;
        case SDLK_DOWN: g_help_sel = (g_help_sel + 1) % NACT; return 1;
        case SDLK_RETURN: case SDLK_KP_ENTER: g_help_wait = 1; return 1;
        case SDLK_DELETE: act_parse(&g_act[g_help_sel], g_act[g_help_sel].padrao); keymap_save(); return 1;
        case SDLK_ESCAPE: g_help = 0; return 1;
        default: return 1;
    }
}
#endif
