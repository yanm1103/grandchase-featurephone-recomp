# Grand Chase 1 (LG U+ / WIPI-C) — recomp de PC

Recompilação estática do `binary.mod` (ARM/Thumb) do Grand Chase 1 para C, com um runtime em C + SDL3 que reimplementa a API do aparelho.
O jogo roda nativo no PC, sem emulador de CPU em tempo de execução.

> **Os arquivos do jogo não estão incluídos.** Você precisa do jar da versão PT já extraído (a pasta com `binary.mod`, `*.pbp`, `*.mmf`…).
> O runtime só aceita o `binary.mod` cujo **código** (`.text` até 0x40fc8) bate com o que foi recompilado (hash FNV-64) e o lê do disco: o jogo em si não vai dentro do `.exe`.
> As strings (idioma) ficam fora do hash: o jar PT, o coreano original e a retradução (`tools/translate/`, saída em `build/out/game_ptbr`) rodam no mesmo `.exe`.

## Como funciona
```
binary.mod ──tools/lift/cfg.py──► 22.858 blocos básicos (recursive descent, 26 tabelas de switch)
           ──tools/lift/lift.py─► um bloco = uma função C que devolve o próximo PC (bit0 = Thumb)
           ──tools/lift/emit.py─► gen/blocks_*.c + gen/tables.c
runtime/cpu.c  trampolim de blocos + interceptação dos 150 stubs de import
runtime/hle.c  67 chamadas de API (WIPI-C + libc) reimplementadas, semântica tirada do WIE (ref/wie)
platform/      main_sdl.c (jogar) | main_headless.c (testes: tempo virtual, entrada roteirizada, trace)
runtime/cpu_unicorn.c  backend de REFERÊNCIA (Unicorn) com o MESMO HLE e a MESMA memória: serve só para validar
```

## Compilar e rodar
Precisa de clang (alvo MSVC), VS BuildTools, Python 3 com capstone + pyelftools, e as dependências em `third_party/` (SDL3 3.4.20 VC, Unicorn).
```
bash build/build.sh sdl                 # build/out/gc1.exe (+ SDL3.dll)
python -I tools/make_audio.py <jar_extraido> <music\wav> build/out/audio   # mapeia os clipes MMF -> WAV pré-renderizado
build/out/gc1.exe --res <jar_extraido> --save save --audio audio [--scale 3] [--fullscreen]
```
Teclas: setas, **Enter/Z/Espaço** = ação, **Q/A** = tecla esquerda, **W/S/Esc** = tecla direita, Backspace/C = limpar, 0-9 (a linha de cima do teclado segue o celular; no teclado numérico as fileiras 789 e 123 são trocadas, pois o celular tem 123 em cima), `*` e `#` (numérico), F11 = tela cheia. Gamepad: D-pad, A = ação, X = esquerda, B = direita.
Saves vão para `--save` (`Grand*.sav`), no mesmo formato do jogo original.

## Validação (o que foi realmente verificado)
| Portão | Resultado |
|---|---|
| CFG | 26/26 `switch` resolvidos; 0 saltos indiretos sem resolver; todos os corpos do Ghidra cobertos (só 34 padding de 2 bytes sobram) |
| Lifter, teste de propriedade (`tools/lift/test_lifter.py 24`) | 458.412 casos de instrução × estado aleatório, **0 divergências** contra o Unicorn em 49 mnemônicos (registradores, flags N/Z/C/V, próximo PC, memória) |
| Traces (`build/out/gc1_headless.exe` × `gc1_ref_headless.exe`) | mesma entrada → **mesmo trace**: cada chamada de API (args e retorno) e o hash do framebuffer em cada `FlushLcd` |
| Fuzzing de entrada (`tools/difftest/fuzz.py`) | ver `docs/VALIDACAO.md` |

Limites: o Unicorn usa o modelo de CPU padrão (não ARM926): comportamento de acesso desalinhado não foi validado. O HLE é compartilhado
pelos dois backends, então erros de **semântica do HLE** (tempo, framebuffer, arquivos) não aparecem nesses testes: vêm da leitura do WIE e de jogar.
A música usa WAVs aproximados (GM + GeneralUser GS, sem a camada FM do chip original); os efeitos são exatos.
O servidor online original não existe mais: `Connect` falha como no WIE.
