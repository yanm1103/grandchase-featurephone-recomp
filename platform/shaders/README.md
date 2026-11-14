# Shaders de upscaling (xBRZ e ScaleFX) para o renderer GPU do SDL3

Fragment shaders GLSL 450 (Vulkan) para usar como "custom fragment shader" via `SDL_GPURenderState`
(`SDL_CreateGPURenderState` / `SDL_SetGPURenderState`). Portados dos shaders GLSL do libretro
(usados tambem no Miki's World). O vertex shader do SDL e fixo; ele entrega ao fragment:

```glsl
layout(location = 0) in  vec4 v_color;   // branco opaco (nao usado)
layout(location = 1) in  vec2 v_uv;      // 0..1 no quadrilatero desenhado, (0,0) = canto superior esquerdo
layout(location = 0) out vec4 o_color;
```

Tudo o que o vertex shader do libretro calculava (TEX0, t1..t4) foi refeito no fragment com
`v_uv`, `textureOffset()` e `textureSize()`. Por isso **nenhum shader do ScaleFX precisa de bloco
de uniformes** (nao chame `SDL_SetGPURenderStateFragmentUniforms` para eles; crie o shader com
`num_uniform_buffers = 0`). Somente o xBRZ tem `Params`.

Arquivos: `xbrz.frag`, `scalefx0.frag` ... `scalefx4.frag` (`crt.frag`/`lcd.frag`, se existirem, nao fazem parte
deste conjunto). Compilar: `glslc -fshader-stage=frag -O arq.frag -o arq.spv` e conferir com `spirv-val arq.spv`.
Todos compilam e validam com glslc / spirv-val do Vulkan SDK 1.4.363.1.

Em todos os shaders de dados as texturas devem usar amostragem NEAREST e CLAMP (e o draw deve usar
`SDL_BLENDMODE_NONE`, pois os canais, inclusive alfa, guardam dados).

## xBRZ freescale (1 passe)

| Item | Valor |
|---|---|
| sampler binding 0 (set 2) | `Source` = textura do jogo, NEAREST, CLAMP |
| Params (set 3, binding 0), std140, 16 bytes | offset 0 `vec2 OutputSize` (largura, altura em pixels do retangulo de destino); offset 8 `vec2 _pad` (zero) |
| alvo | janela / qualquer alvo; saida opaca (alfa = 1) |

`UV_SCALE = 1.0001` (mesmo truque do vertex shader do Miki's World) vem por padrao; compile com `-DUV_SCALE=1.0` para desligar.

## Cadeia do ScaleFX (5 passes)

`gw x gh` = tamanho da textura do jogo (Original). Todos os passes 0..3 desenham um quadrilatero que cobre o
alvo inteiro (alvo do mesmo tamanho da entrada: um texel de saida por texel de entrada).

| Passe | Le (binding 0 = `Source`) | Le (binding 1) | Alvo de saida | Filtro (b0 / b1) | Params |
|---|---|---|---|---|---|
| 0 | Original (jogo) | - | `gw x gh`, RGBA16F (`SDL_PIXELFORMAT_RGBA64_FLOAT`) | NEAREST | nenhum |
| 1 | saida do passe 0 | - | `gw x gh`, RGBA16F | NEAREST | nenhum |
| 2 | saida do passe 1 | `PassPrev2Texture` = saida do passe 0 | `gw x gh`, RGBA8 serve (valores k/15), RGBA16F tambem | NEAREST / NEAREST | nenhum |
| 3 | saida do passe 2 | - | `gw x gh`, RGBA16F recomendado (RGBA8 ainda decodifica, erro < 0,2 de passo) | NEAREST | nenhum |
| 4 | saida do passe 3 | `PassPrev5Texture` = Original (jogo, mesmo tamanho do Source) | `3*gw x 3*gh`, RGBA8 | NEAREST / NEAREST | nenhum |

Encadeamento (igual ao `PostProcessor.cs` do Miki's World: `RunPass(0, game)`, `RunPass(1, p0)`,
`RunPass(2, p1, extra p0)`, `RunPass(3, p2)`, `RunPass(4, p3, extra game)`):

```
Original --p0--> A --p1--> B --p2(+A em binding 1)--> C --p3--> D --p4(+Original em binding 1)--> 3x
```

O resultado do passe 4 (`3*gw x 3*gh`) e depois desenhado na tela com filtro LINEAR (bilinear), como no Miki's World.

Parametros (todos fixos por `#define`, sem uniformes; sobrescreva em tempo de compilacao se quiser):

| Define | Padrao | Passe |
|---|---|---|
| `SFX_CLR` | 0.5 (limiar) | 1 |
| `SFX_SAA` | 1.0 (filtro AA) | 1 |
| `SFX_SCN` | 1.0 (filtro de cantos) | 3 |

O passe 4 exige que `Source` e `PassPrev5Texture` tenham as mesmas dimensoes (como no libretro).

### Notas de integracao no SDL GPU

- Texturas intermediarias: `SDL_CreateTexture(..., SDL_TEXTUREACCESS_TARGET, ...)`. O renderer GPU do SDL 3.4.20
  suporta `SDL_PIXELFORMAT_RGBA64_FLOAT` (half float, `R16G16B16A16_FLOAT`); **nao** ha RGBA128_FLOAT (alvo float de 32 bits).
  Os passes 0 e 1 guardam distancias/forcas; em RGBA8 a qualidade cai, em half float fica proxima do float32 do Miki's World.
- Desenhar cada passe com `SDL_SetRenderTarget(alvo)`, `SDL_SetGPURenderState(renderer, estado_do_passe)`,
  `SDL_RenderTexture(renderer, fonte, NULL, NULL)` (retangulo de destino = alvo inteiro, `src` = textura inteira).
  O `Source` do shader e a textura passada ao `SDL_RenderTexture`; as extras vao em `sampler_bindings` do
  `SDL_GPURenderStateCreateInfo` (viram bindings 1, 2, ... na ordem; o SDL as liga a partir do slot seguinte ao da textura desenhada).
- Ajuste a amostragem de cada textura com `SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST)` (a textura desenhada) e
  crie amostradores NEAREST + CLAMP_TO_EDGE para as texturas extras (`SDL_GPUSamplerCreateInfo`).
- Orientacao: nao e preciso inverter nada; todos os passes usam a mesma convencao (v = 0 em cima) e o
  ScaleFX so depende da vizinhanca relativa.
- Limpeza/blend: use `SDL_SetTextureBlendMode(fonte, SDL_BLENDMODE_NONE)` nos passes intermediarios para o SDL nao misturar o alfa (que e dado) com o conteudo anterior.
- xBRZ: `OutputSize` deve ser o tamanho em pixels do retangulo em que o jogo e desenhado (nao o da janela inteira, se houver barras).

## Licencas

| Arquivo | Origem | Licenca (conforme o cabecalho do arquivo) |
|---|---|---|
| `xbrz.frag` | libretro `xbrz-freescale.glsl` (Hyllian, 2011/2016, mapeamento de texels xBR) + codigo/conceitos do xBRZ do DeSmuME (HqMAME, Zenju) | Hyllian: MIT. xBRZ (HqMAME/Zenju): **GPLv3** (com excecao para ligar com MAME). Por derivar do xBRZ, o arquivo como um todo deve ser tratado como **GPLv3**. |
| `scalefx0.frag` ... `scalefx4.frag` | ScaleFX de Sp00kyFox (Copyright 2016), versao GLSL do libretro | MIT |

Os cabecalhos de licenca originais estao mantidos integralmente em cada arquivo, logo abaixo do comentario de
descricao da portabilidade. Atencao: distribuir um binario que embute `xbrz.frag` implica atender a GPLv3 para esse shader.
