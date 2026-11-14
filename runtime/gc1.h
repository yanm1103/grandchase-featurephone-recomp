/* gc1.h - API do runtime do recomp do Grand Chase 1 e interface com a plataforma (SDL ou headless). */
#ifndef GC1_H
#define GC1_H
#include <stdio.h>
#include "rt.h"

/* --- mapa de memoria do guest --------------------------------------------------------------------- */
#define TEXT_BASE   0x1000u
#define DATA_BASE   0x1400000u
#define BSS_BASE    0x1500000u
#define BSS_SIZE    0x13dca8u
#define HEAP_BASE   0x2000000u
#define HEAP_END    0x3800000u
#define STACK_TOP   0x3f00000u
#define SENTINEL    0x3fff001u      /* LR de chamadas vindas do host */

typedef struct GcStub { uint32_t addr, table, index; } GcStub;     /* stubs de import (gerado) */
extern const GcStub gc_stubs[]; extern const int gc_nstubs;
extern BlockFn gc_blocks[]; extern uint8_t gc_mode[];
extern const uint32_t gc_text_halfwords;
extern const uint64_t GC1_TEXT_FNV; extern const uint32_t GC1_TEXT_SIZE;
extern const uint64_t GC1_CODE_FNV; extern const uint32_t GC1_CODE_SIZE; /* so o codigo (.text ate o inicio da rodata); as strings (idioma) ficam livres */
void gc1_register_all(void);

extern Cpu gc_cpu;
extern FILE *gc1_trace;             /* se != NULL, cada chamada de API e cada FlushLcd e registrada */

/* --- controle (usado pela plataforma) ---------------------------------------------------------------- */
int      gc1_boot(const char *resdir, const char *savedir);       /* 0 = ok */
void     gc1_start(void);                                         /* startClet */
void     gc1_paint(void);                                         /* paintClet */
void     gc1_key(int key, int pressed);                           /* handleCletEvent(502/503, key, 0) */
void     gc1_input_enable(int on);                                /* liga a camada de entrada inteligente (ver hle.c) */
void     gc1_input_key(int key, int pressed);                     /* use no lugar de gc1_key nas plataformas */
int      gc1_snapshot_save(const char *path);                     /* save state: 0 = ok */
int      gc1_snapshot_load(const char *path);                     /* 0 = ok; 1 arquivo nao existe, 2 corrompido, 3 de outro binario */
void     gc1_event(uint32_t type, uint32_t p1, uint32_t p2);
void     gc1_set_time(uint64_t ms);                               /* relogio (ms) visto por CurrentTime e pelos timers */
int64_t  gc1_next_due(void);                                      /* proximo vencimento de timer (ms) ou -1 */
int      gc1_run_timers(void);                                    /* roda timers vencidos; devolve quantos */
int      gc1_should_exit(void);
uint32_t gc1_call(uint32_t addr, int nargs, const uint32_t *args);/* chama funcao do guest (addr com bit0 = Thumb) */
uint64_t gc1_frame_crc(void);
int      gc1_cov_dump(const char *path);                         /* escreve os enderecos dos blocos executados; devolve quantos */                                     /* CRC do framebuffer atual (para traces) */

/* --- implementado pela plataforma ------------------------------------------------------------------------ */
void host_flush(const uint16_t *rgb565, int w, int h);
void host_audio_load(int clip, const void *data, uint32_t size);
void host_audio_play(int clip, int loop);
void host_audio_stop(int clip);
void host_audio_free(int clip);
void host_audio_volume(int volume);
void host_vibrate(int level, int ms);

#endif
