# Arquitetura e validação

## Como funciona
```
binary.mod ──tools/lift/cfg.py──► 22.858 blocos básicos (recursive descent, 26 tabelas de switch)
           ──tools/lift/lift.py─► um bloco = uma função C que devolve o próximo PC (bit0 = Thumb)
           ──tools/lift/emit.py─► gen/blocks_*.c + gen/tables.c
runtime/cpu.c  trampolim de blocos + interceptação dos 150 stubs de import
runtime/hle.c  67 chamadas de API (WIPI-C + libc) reimplementadas, semântica tirada do WIE (https://github.com/dlunch/wie)
runtime/patch.c  patches do motor (acentos, quebra de linha, diagonal, mineração…)
runtime/netsim.h servidor online falso
platform/      main_sdl.c (jogar) | main_headless.c (testes: tempo virtual, entrada roteirizada, trace)
runtime/cpu_unicorn.c  backend de REFERÊNCIA (Unicorn) com o MESMO HLE e a MESMA memória: serve só para validar
```
O runtime só aceita o `binary.mod` cujo **código** (`.text` até 0x40fc8) bate com o que foi recompilado (hash FNV-64) e o lê do disco: o jogo em si não vai dentro do `.exe`.
As strings (idioma) ficam fora do hash: o jar PT, o coreano original e a retradução rodam no mesmo `.exe`.

## Testes e validação (o que foi realmente verificado)
| Portão | Resultado |
|---|---|
| CFG | 26/26 `switch` resolvidos; 0 saltos indiretos sem resolver; todos os corpos de função conhecidos cobertos (só 34 padding de 2 bytes sobram) |
| Lifter, teste de propriedade (`tools/lift/test_lifter.py 24`) | 458.412 casos de instrução × estado aleatório, **0 divergências** contra o Unicorn em 49 mnemônicos (registradores, flags N/Z/C/V, próximo PC, memória) |
| Traces (`build/out/gc1_headless.exe` × `gc1_ref_headless.exe`) | mesma entrada → **mesmo trace**: cada chamada de API (args e retorno) e o hash do framebuffer em cada `FlushLcd` |
| Fuzzing de entrada (`tools/difftest/fuzz.py`) | ver `VALIDACAO.md` |

Limites: o Unicorn usa o modelo de CPU padrão (não ARM926): comportamento de acesso desalinhado não foi validado. O HLE é compartilhado
pelos dois backends, então erros de **semântica do HLE** (tempo, framebuffer, arquivos) não aparecem nesses testes: vêm da leitura do WIE e de jogar.
A música usa WAVs aproximados (sem a camada FM do chip original); os efeitos são exatos.
O servidor online original não existe mais: o jogo usa um servidor falso (`runtime/netsim.h`, ver `INVESTIGACAO_REDE_ITENS.md`).

## Estrutura
| Pasta | Conteúdo |
|---|---|
| `tools/lift/` | CFG, lifter e emissor de C (`functions.tsv` = limites de função usados como sementes) |
| `runtime/`, `platform/` | HLE da API, patches do motor, backends SDL3/headless, shaders |
| `tools/translate/` | pipeline de retradução e ferramentas de formato (`scp`, `ConstData`…) |
| `tools/playtest/`, `tools/difftest/` | testes roteirizados e fuzzing/diferencial |
| `tools/sprites/` | extração dos sprites (`.pbp` + `.msd`) |
| `android/` | port Android (sem Gradle) |
| `docs/`, `PROBLEMAS.md` | relatórios de investigação e pendências conhecidas |
