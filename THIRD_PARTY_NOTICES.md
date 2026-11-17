# Componentes de terceiros / Third-party components

O codigo original deste repositorio esta sob a licenca MIT (`LICENSE`). Os itens abaixo mantem as licencas proprias.

| Componente | Onde | Licenca |
|---|---|---|
| [WIE](https://github.com/dlunch/wie) (Inseok Lee) | A semantica da API WIPI-C implementada em `runtime/hle.c` foi tirada da leitura do codigo do WIE (nao ha codigo copiado/vendorizado; fica fora do repositorio, em `ref/`) | MIT |
| [stb_vorbis](https://github.com/nothings/stb) (Sean Barrett) | `platform/stb_vorbis.c` | dominio publico / MIT |
| [SDL3](https://libsdl.org) 3.4.20 | dependencia, baixada para `third_party/` (nao versionada) | zlib |
| [Unicorn](https://www.unicorn-engine.org) | so no backend de referencia de teste (`runtime/cpu_unicorn.c`, alvo `ref`), baixado para `third_party/` | GPLv2 |
| xBRZ freescale (Hyllian MIT + Zenju GPLv3), via libretro | `platform/shaders/xbrz.frag` (e `platform/shaders_spv.h`, gerado dele) | **GPLv3** (ver cabecalho do arquivo) |
| ScaleFX (Sp00kyFox), via libretro | `platform/shaders/scalefx0..4.frag` | MIT (texto no cabecalho de cada arquivo) |
| CRT / LCD | `platform/shaders/crt.frag`, `lcd.frag` | adaptados do filtro do Miki's World (confirmar a licenca de origem) |

Observacao sobre a GPLv3 do xBRZ: um executavel que embute `xbrz.frag` deve ser tratado como GPLv3. Para distribuir um binario
sem essa restricao, remova o filtro xBRZ (`xbrz.frag` e sua entrada em `shaders_spv.h` / `main_sdl.c`).

O jogo **Grand Chase** (LG U+ / WIPI-C) e todos os seus arquivos (`binary.mod`, `*.pbp`, `*.mmf`, scripts, imagens, musicas...) pertencem aos seus donos e **nao** estao neste repositorio.
