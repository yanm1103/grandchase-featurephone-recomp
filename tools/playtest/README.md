# Teste automatico sem jogar na mao

`pt.py cenario.txt` roda o jogo no `gc1_headless.exe` (tempo virtual, sem janela), aperta teclas por roteiro e escreve `build/pt/out/estado.txt`:
uma linha por quadro/tecla com modo, mapa, posicao/direcao do jogador, modo de movimento (`mv` 0 parado/1 anda/2 corre), acao (`act`), musica e os eventos de audio
(`A` = chamadas cruas do jogo, `M` = o que a plataforma tocaria depois da politica de audio). `shot nome` salva uma captura PNG (`build/pt/out/nome.png`) - da para olhar a tela.

**Save states** (`gc1_snapshot_save/load`, runtime/hle.c): foto completa do jogo (memoria + heap + timers + arquivos + musica), ~1,5 MB, determinista (carregar e jogar da o mesmo que continuar direto - conferido quadro a quadro). `savestate nome` / `loadstate nome` / `from nome` (primeira linha do cenario: comeca do estado, sem boot). Os estados ficam em `build/pt/states/` e sao gerados por `criar_estados.txt` (cidade = mapa 2, torre1 = mapa 20); `run_all.py` os cria se faltarem. Refaca-os (apague a pasta) quando o jogo ou o roteiro de boot mudar. No jogo com janela: **F5 salva / F9 carrega** (`<saves>/estado.snap`).

Comandos do cenario (um por linha, `#` comenta): `boot` (leva ao gameplay: ~54 s virtuais de roteiro gravado; o mapa 2 e uma cidade; prefira `from`), `wait ms`, `press|release|tap|hold <tecla> [ms]`,
`call <endereco_thumb> <a0> [a1]` (chama uma funcao do jogo, ex.: `call 0x213ec 0x65` = net_send_request), `expect online won|medals|coupons <op> <n>` (confere `<saves>/online.sav` do servidor falso ao fim), `teleport <mapa> <x> <y> <dir>` (usa a propria transicao de mapa do jogo; mapa 20 = Torre dos Desafios 1, com monstros), `poke <addr> <bytes> <valor>`,
`expect <campo> <op> <n>` (x y dir st act mv run dead bgm mode map), `mark` + `count music_on|music_off|sfx <op> <n>`, `shot nome`.
Teclas: up down left right ok soft1 soft2 clear menu 0-9 * #.  Opcoes: `--raw` (entrada crua, sem a camada inteligente: reproduz o bug original), `--res pasta`, `--out pasta`.
`run_all.py` roda todos os cenarios (cada um tem `expect`/`count`; retorna 1 se algum falhar). **Rode depois de mexer em entrada, audio ou runtime.**

Enderecos do estado (de `GrandChase1-decomp/tools/globals.tsv`): game_mode 0x1400058, current_map_id 0x1400061, player_entity 0x1634268 (+0x14 x, +0x16 y, +1 dir, +5 estado, +8 acao),
player_move_mode 0x1634b40, player_running 0x163407c, last_move_dir 0x1636040, toque duplo 0x163617c, transicao de mapa 0x162c440..0x162c448.
`gc1_headless` aceita ainda `--state arq`, `--shots ms,ms`, `--smart` / `--smart-from ms` (liga a camada de entrada), e linhas `ms P addr bytes valor` no arquivo de teclas.
