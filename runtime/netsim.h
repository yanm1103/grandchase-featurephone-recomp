/* netsim.h - servidor online FALSO do Grand Chase 1 (incluido por hle.c).
 * O servidor da Netmarble (210.217.196.90:10022) nao existe mais. O jogo original so o usava como "caixa registradora":
 *   - pergaminho de ressurreicao (100 won) e chaves de bau (500/900/1300 won): o servidor cobrava no celular e dizia "ok";
 *     os premios em item eram sorteados no proprio aparelho (roll_loot_drops);
 *   - cupom (codigo para o Grand Chase de PC) e medalha (evento no site da Netmarble): so enfeite;
 *   - Comunidade: ranking, lista de cupons e de medalhas.
 * Aqui tudo e respondido localmente: pergaminho e chaves saem de graca, mas a conta em wons "que voce teria gastado na epoca"
 * e guardada (tela Sobre). Cupons e medalhas sao falsos (codigo aleatorio) e saem JUNTO com os itens (o jogo so mostra a
 * medalha; o cupom aparece num aviso da plataforma e na lista Comunidade > Cupons). Ranking: jogadores inventados.
 * Formato dos quadros: pedido [u16 tam][u16 id][corpo], resposta [u16 tam][u16 id][u8 status][corpo] (tam inclui o status).
 * Estado em <saves>/online.sav. Variaveis de ambiente: GC1_NONET=1 (volta a falhar a conexao, como antes), GC1_NETLUCK=0..100 (forca a
 * chance de cupom e medalha, para teste). Veja docs/INVESTIGACAO_REDE_ITENS.md. */
#ifndef GC1_NETSIM_H
#define GC1_NETSIM_H

#define NS_MAXC 32
typedef struct { char code[16]; char text[50]; } NsCoupon;
static struct { uint32_t magic, won, medals, ncoup, seed; NsCoupon coup[NS_MAXC]; } ns;
static char ns_path[640]; static uint32_t ns_rng; static int ns_luck = -1, ns_nonet;
static char ns_notice[96]; static int ns_has_notice;
#define NS_MAGIC 0x31534e47u

static uint32_t ns_rand(void) { ns_rng ^= ns_rng << 13; ns_rng ^= ns_rng >> 17; ns_rng ^= ns_rng << 5; return ns_rng; }
static int ns_chance(int pct) { if (ns_luck >= 0) pct = ns_luck; return (int)(ns_rand() % 100) < pct; }
static void ns_save(void) { FILE *f = fopen(ns_path, "wb"); if (f) { fwrite(&ns, sizeof ns, 1, f); fclose(f); } }
static void ns_init(const char *savedir) {
    const char *e = getenv("GC1_NONET"); ns_nonet = e && *e && *e != '0';
    e = getenv("GC1_NETLUCK"); if (e && *e) ns_luck = atoi(e);
    snprintf(ns_path, sizeof ns_path, "%s/online.sav", savedir);
    memset(&ns, 0, sizeof ns);
    FILE *f = fopen(ns_path, "rb");
    if (f) { if (fread(&ns, sizeof ns, 1, f) != 1 || ns.magic != NS_MAGIC || ns.ncoup > NS_MAXC) memset(&ns, 0, sizeof ns); fclose(f); }
    if (ns.magic != NS_MAGIC) { ns.magic = NS_MAGIC; ns.seed = (uint32_t)time(NULL) * 2654435761u + 12345u; }
    ns_rng = (ns.seed ^ (uint32_t)time(NULL) ^ 0x9e3779b9u) | 1u;
}
void gc1_net_stats(uint32_t *won, uint32_t *medals, uint32_t *coupons) { if (won) *won = ns.won; if (medals) *medals = ns.medals; if (coupons) *coupons = ns.ncoup; }
int gc1_net_notice(char *buf, int cap) { if (!ns_has_notice) return 0; snprintf(buf, (size_t)cap, "%s", ns_notice); ns_has_notice = 0; return 1; }

static void ns_make_coupon(int choice) {
    static const char *const kind[] = {"Pacote de Itens", "Cupom de Aventura", "Kit de Pocoes", "Pacote de Moedas", "Caixa de Equipamento", "Cupom Especial"};
    NsCoupon c; memset(&c, 0, sizeof c);
    static const char al[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 14; i++) c.code[i] = (i % 5 == 4) ? '-' : al[ns_rand() % 32];   /* XXXX-XXXX-XXXX + NUL (o jogo copia 16 bytes sem garantir o NUL) */
    snprintf(c.text, sizeof c.text, "%s %s", kind[ns_rand() % 6], choice == 2 ? "Dourado" : "de Ferro");
    memmove(&ns.coup[1], &ns.coup[0], sizeof(NsCoupon) * (NS_MAXC - 1)); ns.coup[0] = c; if (ns.ncoup < NS_MAXC) ns.ncoup++;
    snprintf(ns_notice, sizeof ns_notice, "Cupom: %s", c.code); ns_has_notice = 1;
}

static void ns_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void ns_put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t ns_get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* monta a resposta de um pedido completo (req = quadro inteiro); devolve o tamanho do quadro de resposta */
static int ns_respond(const uint8_t *req, int len, uint8_t *out) {
    uint32_t id = req[2] | (req[3] << 8); const uint8_t *b = req + 4; int blen = len - 4; uint8_t *body = out + 5; int n = 0, status = 0;
    if (id == 0x65 && blen >= 13) {                       /* pergaminho / chave de bau */
        int mode = b[12], choice = -1, cost = 0;
        if (mode == 1 || mode == 5 || mode == 10 || mode == 14) cost = 100;
        else if (mode >= 2 && mode <= 4) choice = mode - 2; else if (mode >= 6 && mode <= 8) choice = mode - 6;
        if (choice >= 0) { static const int price[3] = {500, 900, 1300}; cost = price[choice]; }
        if (cost) {
            ns.won += (uint32_t)cost; n = 0x45; memset(body, 0, n);
            if (choice >= 0) {
                if (ns_chance(25)) { body[1] = 1; ns.medals++; }           /* o jogo escreve "[medalha]" sozinho */
                if (choice >= 1 && ns_chance(20)) ns_make_coupon(choice);   /* body[2] = 0: os itens saem normalmente */
            }
            ns_save();
        } else status = 1;
    } else if (id == 0x69 && blen >= 17) {                /* lista de cupons, 4 por pagina */
        uint32_t off = ns_get32(b + 12), cnt = b[16] > 4 ? 4 : b[16], k = 0;
        for (; k < cnt && off + k < ns.ncoup; k++) { memset(body + 1 + k * 0x42, 0, 0x42); memcpy(body + 1 + k * 0x42, ns.coup[off + k].code, 15); memcpy(body + 1 + k * 0x42 + 0x10, ns.coup[off + k].text, 49); }
        body[0] = (uint8_t)k; n = 1 + (int)k * 0x42;
    } else if (id == 0x6b) {                              /* medalhas */
        body[0] = (uint8_t)(ns.medals > 255 ? 255 : ns.medals); n = 1;
    } else if (id == 0xc9 && blen >= 20) {                /* ranking: 8 jogadores inventados (fixos por instalacao) */
        uint32_t mine = ns_get32(b + 12), base = mine < 1000 ? 1000 : mine, save = ns_rng; ns_rng = ns.seed | 1u;
        ns_put32(body, 12 + ns.seed % 380); ns_put32(body + 4, mine); n = 8 + 8 * 16; memset(body + 8, 0, 128);
        static const char *const pre[] = {"010", "010", "010", "011", "016", "017", "018", "019"};
        for (int i = 0; i < 8; i++) {
            uint8_t *e = body + 8 + i * 16; snprintf((char *)e, 12, "%s%08u", pre[ns_rand() % 8], ns_rand() % 100000000u);
            ns_put32(e + 12, (uint32_t)((double)base * (3.0 - 0.22 * i) + (ns_rand() % (base / 10 + 1))));
        }
        ns_rng = save;
    } else { status = 1; }                                /* mercado, correio etc.: encerrado */
    if (status) { static const char msg[] = "Servico online encerrado."; ns_put16(body, sizeof msg - 1); memcpy(body + 2, msg, sizeof msg); n = 2 + (int)sizeof msg; }   /* com o NUL, fora da contagem */
    ns_put16(out, (uint32_t)n + 1); ns_put16(out + 2, id + 1); out[4] = (uint8_t)status;
    return n + 5;
}
#endif
