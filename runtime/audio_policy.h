/* audio_policy.h - politica de audio entre o jogo e a plataforma.
 * O jogo (WIPI) so tem UM clipe de audio por vez: para tocar um efeito (golpe, item...) ele para e libera a musica, carrega o efeito
 * no mesmo clipe e, depois, recarrega a musica DESDE O INICIO. Aqui isso vira o que um PC faz naturalmente:
 *  - clipe com loop = musica; sem loop = efeito, que e mixado POR CIMA da musica (a musica nao para);
 *  - recarregar a musica que ja esta tocando nao reinicia;
 *  - "parar a musica" so vale se nenhum efeito vier no mesmo quadro (parar+liberar+carregar efeito = trocar de clipe, nao parar de verdade).
 * A plataforma so implementa AudioOut (identifica o clipe pelo hash FNV-64 dos bytes MMF). */
#ifndef GC1_AUDIO_POLICY_H
#define GC1_AUDIO_POLICY_H
#include <stdint.h>
typedef struct AudioOut {
    void (*music)(uint64_t hash, int on);   /* on=1: toca em loop (so e chamado quando muda a faixa); on=0: para */
    void (*sfx)(uint64_t hash);             /* toca uma vez, mixando com o resto */
} AudioOut;
void ap_init(const AudioOut *out);
void ap_load(int id, const void *data, uint32_t size);
void ap_play(int id, int loop);
void ap_stop(int id);
void ap_free(int id);
void ap_tick(void);                          /* chamar apos cada quadro do jogo (confirma "parar a musica") */
#include <stdio.h>
void ap_state_save(FILE *f);               /* save state: guarda/retoma a musica corrente */
void ap_state_load(FILE *f);
uint64_t ap_fnv64(const void *p, uint32_t n);
#endif
