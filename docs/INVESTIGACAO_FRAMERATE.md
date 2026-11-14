# Investigacao de framerate / pacing do port do Grand Chase 1

Data: 2026-11-14. Escrito para ser lido numa sessao nova, sem contexto. Somente leitura de codigo + medicoes no `gc1_headless.exe` (via `tools/playtest/pt.py`, tempo virtual). Nenhum codigo do projeto foi alterado, nada foi compilado, `gc1.exe` nao foi executado.

Convencao: **[FATO]** = verificado no codigo ou medido. **[HIPOTESE]** = inferencia ainda nao testada.

---

## 1. Resumo (leia isto primeiro)

1. **A hipotese inicial "o jogo roda a 40 fps / 25 ms" esta ERRADA.** O periodo do quadro e `100 - 20*velocidade` ms, velocidade 1..4, **padrao de jogo novo = 3 = 40 ms (25 fps)**. O valor "25 ms/quadro" que aparece em `pt.py`/`main_headless.c` e so uma margem para contar quadros do harness, nao pacing.
2. **A velocidade 2 do usuario = 60 ms por quadro = 16,67 fps.** Isso bate exatamente com o logcat do celular (`queueBuffer fps=16.68`). Ou seja: **o ritmo de 16,7 fps no celular NAO e um problema/throttle do port; e o ritmo nativo do jogo na velocidade que o usuario escolheu.** (O `build/out/save/Grand4.sav` do PC tambem tem velocidade 2: bytes `02 03 02 01`.)
3. A velocidade e so o **periodo do timer de quadro**; os passos de movimento nao mudam (medido: 8 px por quadro em TODAS as velocidades). Toda a logica do jogo conta quadros, nao tempo real; `CurrentTime` so serve para tempo de jogo em minutos, timeouts de rede e semente do RNG. Logo mudar o periodo escala a velocidade do jogo linearmente (vel. 1: 0,75x, 2: 1x, 3: 1,5x, 4: 3x em relacao a 60 ms).
4. **A "trepidacao" tem causa matematica e e reproduzivel**: 60 ms nao e multiplo do periodo de vsync de 60/90/120 Hz. A 90 Hz cada quadro do jogo fica 5 ou 6 vsyncs na tela = **55,6 ms ou 66,7 ms** — exatamente o "intervalo min ~55 / max ~67 ms" medido no celular (modo 90 Hz). A 50 Hz seria perfeito (60 ms = 3 vsyncs exatos); o Realme tem modo de 50 Hz.
5. Mesmo com pacing perfeito, **16,7 passos/s com saltos de 8 px (36 px fisicos no 1080x2400) sera visivelmente "picado"**. O ganho grande vem de **interpolar o desenho (opcao c)**, e ela parece **viavel** neste motor: a camera e recalculada A CADA RENDER a partir de `player_entity->x/y` e o jogo ja tem um modo "render sem avancar animacao" (`anim_advance_enabled`). Risco medio, esforco medio-alto.
6. **Recomendacao (sequencia)**: (0) instrumentar e confirmar o pacing do lado do app; (1) fixar velocidade 2 como padrao de jogo novo e corrigir constantes do port que assumem 40 ms; (2) trocar para pacing alinhado ao vsync (ganho barato: pedir 50 Hz ao Android e/ou vsync); (3) prototipo de interpolacao so no modo gameplay (mapa 2D) com "render extra" a cada vsync. Opcao (d) (logica a 60 Hz com escala) nao compensa.

---

## 2. Arquivos e enderecos-chave

Recomp (`c:\Games\GrandChase1-recomp`):
- `runtime/hle.c`: relogio `g_now` (l.19), fila de timers `timer_add/timer_cancel/gc1_next_due/gc1_run_timers` (l.66-103), `H(CurrentTime)` l.200, `H(DefTimer)/H(SetTimer)/H(UnsetTimer)` l.201-208 (SetTimer = **one-shot** em `g_now + atraso`), `H(FlushLcd)` l.345 (chama `host_flush`), `H(GetFramebufferPointer)` l.316 (framebuffer RGB565 240x320 na memoria do guest, `g_fb_buf`), camada de entrada `smart_tick`/`gc1_input_key` l.526-562, save state l.572-.
- `platform/main_sdl.c`: laco principal l.224-290 (`SDL_WaitEventTimeout` com timeout <= 8 ms, `gc1_run_timers()`, `RenderPresent` so quando `g_dirty`), `host_flush` l.18-26 (`SDL_UpdateTexture` + `g_dirty=1`), `game_now()` l.118 (= `now_ms() - g_skew`).
- `platform/main_headless.c`: laco em tempo virtual l.90-100 (avanca direto para o proximo vencimento de timer/tecla).
- `runtime/patch.c`: ganchos por endereco (`install(addr, fn)` troca `gc_blocks[]`); prova que da para interceptar qualquer funcao do guest (`hook_walk_anim` 0x23fe4 etc.).
- `tools/playtest/pt.py` l.72-74: `frames = (tend+1500)//25 + 50` (so margem de contagem).
- `PROBLEMAS.md` l.10: comentario "4 quadros (160 ms)" e camada nova com 6 quadros "240 ms" — **assumem 40 ms/quadro**.

Decompilado (`c:\Games\GrandChase1-decomp`, `decompiled/c/*.c`, nomes em `tools/build/names_all.tsv` e `globals_all.tsv`):
- `0x1138 game_init` (01000.c:~140): `frame_interval_ms = game_speed_setting*-20 + 100`; `DefTimer(0x1633db4, 0x12ad)`; `SetTimer(...frame_interval_ms...)`.
- `0x12ac frame_timer_callback` (01000.c:173-240): **o unico timer do jogo**. Le `CurrentTime`, chama `game_update_by_mode` (0x157c) e `game_render_by_mode` (0x163c), `flush_lcd_region` (0x39cc -> `WIPIC_FlushLcd`, **unico chamador**, 1 por quadro, mesmo pausado), e rearma com `frame_interval_ms - (tempo gasto)` (min 1 ms).
- `0x1c98 update_frame_interval`; `0x1cb4 load_audio_settings` (defaults 2,2,3,1 e le `Grand4.sav`, 4 bytes); `0x1c28 save_audio_settings`.
- Globais: `game_speed_setting` = **0x162a757** (byte; musica=0x162a755, efeitos=0x162a756, vibracao=0x162a758), `frame_interval_ms` = **0x1633dc8** (u32), `anim_advance_enabled` = **0x162dce6**, `camera_x` 0x1633f28, `camera_y` 0x16356fc (s16), `entity_list` 0x1634bf8 (ate 0x226 ponteiros; entidade 0x3c bytes: +0x14 x, +0x16 y, +0x1c anim, +0x38 sprite), `entity_count` 0x150004c, `player_entity` 0x1634268, `item_drops`, pools de projeteis (0x1634b64, 25), de efeitos de hit (0x162cf24, 100 slots x 12 B).
- Menu: `0x15a78 menu_options_key` (15000.c:~640-760), `0x1722c draw_options_page`.
- Camera: `0x13b34 draw_gameplay_scene` (13000.c) -> `0x13aa4 draw_world_view_at` -> `0xd550 update_camera`; desenho: `0x13a80 draw_background_and_entities`, `0x13258 draw_entities_and_effects`, `0x3511c sprite_draw_anim_step` (35000.c:~38: avanca `*frame` so se `anim_advance_enabled`).
- Slow-mo de chefe: `23000.c` ~937/971 (`frame_interval_ms += 0xf` por quadro na animacao de morte de monstro subtipo 2/3, e volta a `100-20*vel` ao fim).

---

## 3. Achados

### 3.1 Opcao "Velocidade" (pergunta 1)

[FATO] Fica na **pagina 2 do menu de opcoes** (`menu_options_key`, 4 itens: musica 0..4, efeitos 0..4, **velocidade**, vibracao on/off). Cursor no item 2: tecla direita incrementa (`game_speed_setting+1`, se > 4 volta a 1), esquerda decrementa (se chega a 0 vai a 4); logo chama `update_frame_interval()` e `save_audio_settings()`. Valores validos: **1, 2, 3, 4**.

[FATO] Variavel `game_speed_setting` em **0x162a757**, salva em **`Grand4.sav`, byte de indice 2** (arquivo de 4 bytes: musica, efeitos, velocidade, vibracao). Default de jogo novo (sem arquivo): **3** (`load_audio_settings`: "2,2,3,1").

[FATO] Efeito: so `frame_interval_ms = 100 - 20*vel`:

| vel | periodo | fps | escala vs vel 2 |
|---|---|---|---|
| 1 | 80 ms | 12,5 | 0,75x |
| **2** | **60 ms** | **16,67** | **1x (base do usuario)** |
| 3 (padrao) | 40 ms | 25 | 1,5x |
| 4 | 20 ms | 50 | 3x |

Medido no headless (poke de 0x162a757 e 0x1633dc8, intervalos entre linhas `F` de `estado.txt`): vel 1 -> 80 ms, vel 2 -> 60 ms, vel 3 -> 40 ms, vel 4 -> 20 ms, exatos e constantes (sem jitter em tempo virtual). Movimento do jogador (andar para a direita, cidade): **dx = 8 px por quadro em todas as velocidades** (26 quadros seguidos de +8); so muda o dt entre quadros. Logo a velocidade em px/s = 8/periodo: 100 (vel 1), 133 (vel 2), 200 (vel 3), 400 px/s (vel 4).

[FATO] Mudanca dinamica adicional: durante a morte de monstro subtipo 2/3 (chefes) o jogo soma 15 ms ao periodo a cada quadro (`23000.c` ~937) e restaura no fim (~971): camera lenta de chefe. Qualquer esquema de periodo fixo/travado ao vsync precisa respeitar isso.

[FATO] Nenhum ajuste de velocidade no port: `grep` por `162a757`, `speed`, `1633dc8`, `frame_interval` em `platform/` e `runtime/` nao acha nada (so strings de traducao). O que esta ativo = o que o jogo/Grand4.sav/snapshot decidem. No PC: `build/out/save/Grand4.sav` = `02 03 02 01` (vel **2**). `build/backup_celular/` nao contem `Grand4.sav`; no celular o valor efetivo e deduzido do logcat (16,68 fps = vel 2) [inferencia forte, nao leitura direta do arquivo]. Obs.: o `auto.snap`/`estado.snap` restaura a memoria inteira, incluindo 0x162a757/0x1633dc8, entao o snapshot fixa a velocidade com que foi salvo.

[FATO] O harness (`pt.py`) apaga a pasta de saves a cada execucao, entao o headless sempre comeca no padrao 3 = 40 ms (e os estados `cidade.snap`/`torre1.snap` foram criados assim). Para testar vel 2 no harness: `poke 0x162a757 1 2` e `poke 0x1633dc8 4 60` (como nas medicoes abaixo).

### 3.2 Como o port entrega quadros (pergunta 2)

[FATO] Fluxo por quadro: timer de quadro vence -> `gc1_run_timers()` -> guest roda `frame_timer_callback` (update + render no framebuffer RGB565 do guest, **desenhado no lugar, redesenhando tudo todo quadro**: tiles do mapa, depois entidades ordenadas por profundidade) -> `WIPIC_FlushLcd` -> `host_flush` copia para a textura SDL (`SDL_UpdateTexture`) e marca `g_dirty` -> ao fim da iteracao do laco: `RenderClear` + `RenderTexture` (+ overlay de toque) + `RenderPresent`. **Um Present por quadro do jogo**, nada de throttle proprio (16,7 fps e consequencia do periodo de 60 ms, nao de limitador).

[FATO] Sem vsync: `grep` por `VSync/SetFrameRate/preferredDisplayMode/SwapInterval` no repo = nada. No SDL 3.4.20 vendorizado (`third_party/SDL3-3.4.20-src/src/video/SDL_egl.c:1151`) o EGL e forcado a swap interval 0, e `SDL_render.c` so liga vsync se a propriedade/hint existir. Ou seja, o Present e imediato e quem alinha ao display e o SurfaceFlinger.

[FATO] Tempo do jogo: `game_now() = SDL_GetCurrentTime/1e6 - g_skew` (g_skew soma o tempo em segundo plano/ajuda). O laco chama `gc1_set_time(game_now())` antes de rodar timers, e **`g_now` fica congelado durante todo o callback** (so muda no proximo `gc1_set_time`). Consequencias:
  - O guest mede "tempo gasto no quadro" = 0 e rearma com `due = g_now(no despertar) + 60`. Nao ha compensacao do tempo de CPU/Present, mas tambem nao ha acumulo: o proximo quadro vence 60 ms apos o despertar do atual. Se o despertar atrasa L ms, o periodo vira 60+L (deriva so pelo atraso de despertar; media medida 16,68 fps = ~59,95 ms, ou seja L~0).
  - Quadro lento (> 60 ms): **nao acumula nem pula**; o jogo simplesmente anda mais devagar (nunca roda dois quadros seguidos para "recuperar"; so existe 1 timer).
  - `SDL_WaitEventTimeout` com timeout = min(8, due - now) ms; eventos de entrada acordam o laco, mas so afetam o jogo no proximo quadro (latencia de entrada = 0..60 ms + o que o jogo ja tem).
  - Com toast na tela (`g_toast_until`) `g_dirty` e rearmado a cada iteracao, entao ha Present a cada <= 8 ms durante 1,8-2,2 s (pode bloquear no swap em alguns drivers): curiosidade, nao causa do problema.

[FATO] Padrao de repeticao de quadros com ticks a 60 ms (cada quadro aparece no primeiro vsync apos ser apresentado), calculado por simulacao:

| Tela | vsyncs por tick | duracao na tela de cada quadro | desvio do ideal (60 ms) |
|---|---|---|---|
| 50 Hz | 3,0 | 60,0 sempre | **zero** |
| 60 Hz | 3,6 | 50,0 / 66,7 (40% / 60%) | -10 / +6,7 ms |
| **90 Hz** | 5,4 | **55,6 / 66,7** (60% / 40%) | -4,4 / +6,7 ms |
| 120 Hz | 7,2 | 58,3 (80%) / 66,7 (20%) | -1,7 / +6,7 ms |
| 48 Hz | 2,88 | 62,5 / 41,7 | ruim |
| 45 Hz | 2,7 | 66,7 / 44,4 | ruim |

[FATO] O celular real (Realme RMX3780, informado pelo coordenador: modo atual 90 Hz; modos 120/90/60/50/48/45 Hz) mostrou "queueBuffer fps=16,68, intervalo min ~55 ms, max ~67 ms" estavel: **bate com a linha de 90 Hz** (55,6/66,7) e com a media 59,95 ms. [HIPOTESE] que esse intervalo e o do lado da composicao (SurfaceFlinger/BufferQueue) e que o lado do app (instante do Present) e uniforme em ~60 ms; confirmar com log de timestamps no Present (passo 0 do plano). Se o proprio app tivesse jitter de despertar, o resultado teria o mesmo aspecto, entao a medicao no app e necessaria.

[FATO] A taxa de atualizacao do celular **nao esta registrada** no repo (README/PROBLEMAS so citam 1080x2400); o dado 90 Hz veio do coordenador. Ela e adaptativa (ColorOS), entao pode variar com o conteudo.

### 3.3 Quadros vs tempo real (pergunta 3)

[FATO] Todas as chamadas a `WIPIC_CurrentTime` no decompilado (grep em `decompiled/c/*.c`): `game_init` (01000.c:131, base do contador de minutos), `frame_timer_callback` (01000.c:188 e 211: minutos jogados `play_time_minutes`, timeouts de rede 30 s e 120 s, e compensacao do periodo), `1f000.c:693` e `21000.c:39/90/119/176` (marcas de tempo de I/O de rede), `0a000.c:671 -> seed_rand_from_time` (RNG). **Nada de gameplay (movimento, animacao, recarga, regen, dano) escala por delta de tempo.** O unico `SetTimer` do guest e o do quadro (01000.c:155/239/286, resume rearma com 1 ms).

[FATO] Exemplos de contadores em quadros: regen de HP a cada 0x20 ticks (`hp_regen_tick` 0x13b80); `bgm_restart_countdown = 0x14` (resume); `audio_busy_frames`; toque duplo `0x163617c` (4 quadros; a camada do port usa 6 quadros em `hle.c`); tremor de tela (`screen_shake_duration = 4`); `status_tick`, `combo_idle_timer`; popups de dano "6 quadros"; sombra residual "6 frames"; cursor do minigame de bau "+-6 por quadro"; fade/blackout (`DAT_0150003c > 5`); animacoes de sprite por `*frame++` em `sprite_draw_anim_step`; deslocamento do jogador 8 px/quadro (5 px em tile lento) e multiplos de 8 para alinhar caminhadas de cena.

=> [CONCLUSAO] Mudar o periodo do timer **acelera/desacelera tudo proporcionalmente** (confirmado nas medicoes de velocidade). Como musica e efeitos sonoros sao independentes do relogio do jogo (so reiniciam por contadores de quadros), a trilha nao acompanha a escala.

[FATO] Nota sobre o port: a camada de entrada (`hle.c` ~537-562) e os comentarios de `PROBLEMAS.md` (160/240 ms) foram escritos assumindo 40 ms/quadro; na vel 2 o toque duplo de 6 quadros vira **360 ms** (4 quadros do jogo = 240 ms). Provavelmente aceitavel, mas e uma decisao.

### 3.4 Onde interceptar para interpolar (viabilidade da opcao c)

[FATO] O guest desenha direto num unico framebuffer 240x320 (RGB565) do heap do guest (`g_fb_buf`); nao ha lista de sprites retida: a cena e re-renderizada por inteiro a cada chamada. Mas:
  - **Camera**: `draw_gameplay_scene` (0x13b34) usa `player_entity->x/y` (ou `cutscene_camera_x/y` no modo 4) e `draw_world_view_at` chama `update_camera` (0xd550) **dentro do render**, a cada quadro. Se o x/y do jogador for substituido por um valor interpolado antes do render, a camera acompanha sozinha (com o mesmo clamp). Tremor (`screen_shake`) e somado no mesmo ponto.
  - **Entidades**: `entity_list` (0x1634bf8, `entity_count` em 0x150004c) com x em +0x14 e y em +0x16 (s16), mais o jogador (0x1634268, que tambem esta na lista de ordenacao com id 0x225), pet, projeteis (pool 0x1634b64), drops, efeitos de hit (pool 0x162cf24). Cada pool tem layout proprio; interpolar so o jogador+camera+entity_list ja cobre a maior parte do movimento visivel.
  - **Efeitos colaterais do render** (o problema real): `sprite_draw_anim_step` avanca `*frame++` dentro do desenho (mas so se `anim_advance_enabled` (0x162dce6) != 0 — **o proprio jogo ja zera essa flag para desenhar o mundo "congelado" atras de menus/dialogos**, ver `game_render_by_mode` 01000.c:358/415/448/479); `game_render_by_mode` decrementa `toast_timer`; `draw_world_view_at` avanca o contador de blackout; `draw_hit_effect_pool` libera slots; minigames desenham com cursor que anda; `draw_map_tiles_animated` usa contador 0x16356fa. Portanto re-chamar `game_render_by_mode` varias vezes por tick exige (a) `anim_advance_enabled = 0` nos renders extras, e (b) salvar/restaurar o que o render muta (ou a memoria do guest inteira: o save state ja prova que a "foto" completa e ~1,5 MB e deterministica; um memcpy de poucos MB por render extra e barato). [HIPOTESE] que so isso basta; precisa de prototipo para medir o que realmente muda (diff de memoria antes/depois de um render).
  - Pontos de interceptacao reais ja usados no projeto: `install(addr, fn)` em `patch.c`. Hooks naturais: `frame_timer_callback` (0x12ac) para separar logica de render; `game_render_by_mode` (0x163c); `flush_lcd_region` (0x39cc) ou `H(FlushLcd)`.
  - Custo de render: medi no headless (PC, tudo incluso, update+render+harness): 910 quadros em ~2,5 s = **~3,1 ms por quadro** [medicao grosseira, inclui overhead do processo/harness]. No celular, ~3-5x mais [HIPOTESE]; a 90 Hz seriam ~5,4 renders por tick de 60 ms (~20-25 ms de CPU por tick): cabe, mas a 120 Hz e com o resto do sistema e preciso medir.

### 3.5 Medicoes realizadas (headless, tempo virtual, `--out` em pasta propria)

- Intervalos de FlushLcd (linhas `F` do estado.txt) a partir de `cidade.snap`: padrao 40 ms (vel 3); depois de `poke 0x162a757 1 2` + `poke 0x1633dc8 4 60`: 60 ms (191 intervalos de 60, 38 de 40 antes do poke).
- Velocidades 1..4: dt = 80/60/40/20 ms; dx = 8 px/quadro em todas.
- Tempo de execucao do harness: 910 quadros em 3,19 s totais (0,70 s de arranque medido com 114 quadros).

---

## 4. Opcoes e avaliacao (pergunta 4)

### (a) So melhorar o pacing, sem mudar a logica
- Pedir ao Android uma taxa compativel com 16,67 Hz: `Surface.setFrameRate(16.67f, FRAME_RATE_COMPATIBILITY_FIXED_SOURCE)` (API 30+) via JNI/Java no SDLActivity, ou `WindowManager.LayoutParams.preferredDisplayModeId` apontando para o modo de 50 Hz. [HIPOTESE] 16,67 x 3 = 50 Hz exato, que o aparelho oferece: pacing perfeito (60 ms = 3 vsyncs), sem mexer na logica. Risco: o ColorOS pode ignorar ou o painel a 50 Hz parecer mais "piscante"; somente testando no aparelho.
- Ligar `SDL_SetRenderVSync(ren, 1)` e fazer o laco ser guiado por vsync (presentar a cada vsync, tick do jogo quando o relogio passar de `due - meio vsync`): nao elimina o padrao 5/6 em 90 Hz (e matematico), so evita Present fora de fase/jitter de despertar (+-1-2 ms).
- Travar o periodo a um multiplo do vsync (ex.: 90 Hz: 5 vsyncs = 55,6 ms = +8% de velocidade; 120 Hz: 7 vsyncs = 58,3 ms = +2,9%; 60 Hz: 4 vsyncs = 66,7 ms = -10%, ou 3 = 50 ms = +20%): jitter zero, mas muda levemente a velocidade (decisao do usuario) e precisa respeitar o slow-mo de chefe.
- Esforco: baixo. Risco: baixo. Ganho: elimina o padrao 55/67 mas o movimento continua em 16,7 passos/s (ainda "picado").

### (b) Mudar o periodo para o da velocidade 2 como base
- O periodo da vel 2 ja e 60 ms, e o usuario ja joga assim. O que falta e **tornar vel 2 o padrao de jogo novo** (hoje o padrao e 3) e decidir se o menu continua oferecendo 1..4. Implementacao: gancho depois de `load_audio_settings` (0x1cb4) que, se `Grand4.sav` nao existia, grava 2 em 0x162a757 e chama `update_frame_interval`; ou simplesmente semear um `Grand4.sav` `02 02 02 01` no APK/pasta de saves. Ajustar constantes do port que assumem 40 ms (toque duplo, comentarios).
- Esforco: muito baixo. Risco: baixo. Ganho de fluidez: nenhum (so consistencia).

### (c) Desacoplar render da logica interpolando posicoes (jogador, monstros, camera)
- Ideia: tick logico a 60 ms (como esta); entre ticks, a cada vsync, re-renderizar com `player_entity->x/y` e `entity_list[i]->x/y` = `prev + (cur-prev)*alpha` (alpha = tempo desde o tick / 60 ms), `anim_advance_enabled=0` nos renders extras, desfazendo os efeitos colaterais; presentar cada render. Camera segue sozinha (3.4). Animacao de sprite continua a 16,7 Hz (so o deslocamento fica suave), que e o aspecto "de jogo antigo" esperado.
- Viabilidade: **sim, plausivel** [HIPOTESE parcialmente verificada pelo codigo]. Pontos a resolver: guardar posicao anterior por entidade (chave = ponteiro/slot de `entity_list`; sem interpolar se o salto for grande: teletransporte, troca de mapa, knockback instantaneo, spawn, cutscene); efeitos/projeteis/drops sem interpolacao ficariam "em degraus" ao lado do mundo suave (aceitavel); renders extras so quando `game_mode==2` e sem overlay (mapa-mundi, dialogo, menus continuam a 16,7 Hz); cuidar do estado mutado pelo render (diff de memoria); latencia visual +1 tick (60 ms) se interpolar entre prev e cur (alternativa: extrapolar com velocidade, sem atraso mas com sobrecorrecao nas paradas).
- Esforco: medio-alto (prototipo ~1-2 sessoes; refinamento por pool depois). Risco: medio (render reentrante em memoria do guest; snapshot/restore; custo de CPU a 90/120 Hz; quebrar o determinismo dos save states se mal isolado). Ganho: **o maior** — de 16,7 para 60-120 quadros de deslocamento por segundo, elimina o jitter 5/6 para o movimento.
- Teste: harness com `anim_advance_enabled`/hook em modo diagnostico para comparar memoria antes/depois do render extra; teste de determinismo (rodar com e sem renders extras, comparar `gc1_frame_crc` do quadro logico e posicoes); medir tempo de render por chamada no celular.

### (d) Rodar a logica a 60 Hz com escala de passos/contadores
- Inviavel na pratica: toda a logica e contada em quadros (secao 3.3) com centenas de pontos (regen 0x20, tremor 4, fade 5, animacoes `*frame++`, ataques com `n_steps-2`, timers de status, etc.) e passos de 8 px alinhados a tiles de 16 px; escalar cada um e reescrever o jogo. Risco alto, ganho igual ao de (c) sem a vantagem de nao tocar a logica. **Nao recomendado.**

---

## 5. Sequencia recomendada

0. **Instrumentar (sem mudar comportamento)**: no `main_sdl.c` (Android e PC) logar `SDL_GetTicksNS()` em cada `host_flush`/`RenderPresent` e a diferenca para o anterior; resumo p50/p95/max a cada 5 s. Em paralelo medir no celular `adb shell dumpsys SurfaceFlinger --latency <layer>` e `dumpsys gfxinfo dev.gc1.recomp framestats`. Objetivo: confirmar se o jitter 55-67 ms e so da composicao (app uniforme em 60) ou tambem do despertar do laco.
1. **Padrao vel 2** (opcao b) + corrigir comentarios/constantes que assumem 40 ms (toque duplo etc.). Teste: harness (`poke 0x162a757 1 2` e `0x1633dc8 4 60` ou `Grand4.sav`), confirmar 60 ms entre quadros e `run_all.py` verde.
2. **Pacing alinhado a tela** (opcao a): primeiro o experimento de 50 Hz no Realme (setFrameRate/preferredDisplayModeId) e comparar visualmente; depois `SDL_SetRenderVSync`; so entao, se o usuario aceitar, travar o periodo ao multiplo de vsync. Teste: o log do passo 0 mostrando intervalos de Present estaveis; observar no aparelho (video em camera lenta) o padrao de quadros.
3. **Prototipo de interpolacao** (opcao c) so no modo 2 (gameplay) para jogador + camera + `entity_list`; chave de ativacao (env/arg) para comparar com o modo original; validar determinismo e custo de CPU no celular; ampliar para pet/projeteis se valer a pena.

Como testar em geral: harness de playtest (`tools/playtest/pt.py`, `run_all.py` apos qualquer mexida em runtime/entrada), medicao de intervalos de `FlushLcd` (linhas `F` do `estado.txt`; no app, o log do passo 0), teste no celular Realme (1080x2400, modo de tela 90 Hz com 120/90/60/50/48/45 disponiveis) em cenas de andar na cidade, combate na Torre (mapa 20) e morte de chefe (slow-mo).

---

## 6. Decisoes que o usuario precisa tomar

1. Manter **exatamente 60 ms por quadro** (fiel, com padrao 55/67 a 90 Hz), ou aceitar travar o periodo a um multiplo do vsync (90 Hz: +8%, 120 Hz: +3%, 60 Hz: -10%) para zerar o jitter sem interpolar?
2. Vale testar **forcar 50 Hz** no celular (pacing perfeito a 60 ms, mas painel a 50 Hz)? Ou prefere manter 90/120 Hz com interpolacao?
3. Se houver interpolacao: aceita **+60 ms de atraso visual** (interpolar entre quadros) ou prefere extrapolar (sem atraso, mas com leve "passada a mais" ao parar)?
4. Velocidade 2 passa a ser o **padrao de jogo novo**? O menu de velocidade continua com 1..4 (4 = 3x, quase injogavel) ou limitar/ocultar?
5. A interpolacao deve valer so no mapa de jogo (recomendado) ou tambem em menus/cenas?
6. Orcamento de CPU/bateria: limitar a taxa de apresentacao (ex.: 60 Hz mesmo em tela de 90/120)?
7. O toque duplo (correr) deve seguir 6 quadros (360 ms a vel 2) ou ser reajustado para ~240 ms?
8. O Realme: confirmar se a tela fica fixa em 90 Hz ou e adaptativa (o dado de 90 Hz veio do coordenador, nao do repo); e o valor real de `Grand4.sav` no aparelho (`adb shell run-as dev.gc1.recomp cat files/save/Grand4.sav | xxd`, esperado `02 xx 02 xx`).
