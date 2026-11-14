# Retraducao PT-BR do Grand Chase 1 (a partir do coreano)

O PT original do jar era traducao automatica palavra por palavra (nomes proprios traduzidos como substantivos, genero errado, palavroes que nao existem no original).
Aqui o texto foi **retraduzido do coreano** e regravado no formato do jogo. O jar PT original nao e alterado: a saida vai para `build/out/game_ptbr`.

## Como jogar
`build/out/jogar_ptbr.bat` (ou `gc1_ptbr.exe --res game_ptbr --save save --audio audio`). O jar coreano (`C:\Games\GrandChase1-extracted\ko\jar`) tambem roda com o recomp:
o portao de verificacao agora confere so o **codigo** do `.text` (ate 0x40fc8), nao as strings (idioma).

## Onde fica cada texto (formatos descobertos)
| Origem | Formato | Unidades |
|---|---|---|
| `n*.scp/.dat`, `m*.scp/.dat` | bytecode de script; opcodes 0x11 (fala: u32 ator, u8 expr, s16 off, s16 len) e 0x1a (popup/titulo: s16 off, s16 len, u8 modo) apontam para o `.dat` (texto sem separador) | 2.922 |
| `scp_quest`, `scp_map`, `scp_mix`, `tips` (`.scp` registros fixos + `.dat`) | esquemas em `tables.py` (stride, posicao do offset/tamanho), validados por cobertura exata do `.dat` | 529 |
| `menu.tx`, `npcmenu.tx` | linhas `Titulo/corpo$` e `Opcao@Opcao@$`, CRLF, buffer de 10.000 bytes | 60 |
| strings de UI na rodata do `binary.mod` (dentro do `.text`) | slots de tamanho fixo (ponteiros no codigo apontam para o inicio); a traducao tem de caber | 322 de texto real |
| `ConstData.br` | tabelas binarias + blob de nomes em 0x82d2 referenciado por (off s16, len u8): itens, pets, runas, pocoes, habilidades, monstros, NPCs (`constdata.py`) | 1.064 |

`scp_npc.*` solto no jar e codigo morto (o jogo le os nomes de NPC do `ConstData`).

## Pipeline
1. `extract.py` -> `work/catalog.json` (todas as unidades, com falante/cena/limite).
2. `make_batches.py` -> `work/batches/*.json`; traducao em lotes com `GUIA.md` + `GLOSSARIO.md` (+ `work/str_br_s5.txt`, tabela oficial do BR da Season 5 extraida de `string.kom`) -> `work/out/*.json`; `validate.py` confere ASCII, limite, marcadores, nomes proibidos.
3. `build.py` -> `build/out/game_ptbr`: aplica `normalize.py` (padronizacao de termos entre lotes) e `work/overrides.json` (correcoes manuais), quebra linhas por largura, reconstroi os arquivos e **verifica por releitura** (cada texto gravado tem de voltar identico; o codigo do `.text` tem de ficar intacto).
4. `ppm2png.py`, `montage.py`: convertem quadros do `gc1_headless.exe --dump` para ver as telas.

## Regras do motor que importam
- **Acentos**: a fonte so tem 0-9 A-Z a-z, e bytes >= 0xb0 sao hangul. Mas os bytes 0x80..0xA3 passam intactos por `text_to_utf16`, entao 24 deles sao letras acentuadas (`charset.py`: á é í ó ú â ê ô ã õ à ç e maiusculas); `runtime/patch.c` desenha esses codigos (folha de glifos montada a partir da propria fonte) e mede 6 px. Ocupam 1 byte, como a letra sem acento (limites de tamanho nao mudam). O texto traduzido e acentuado pelo passe `acentos.py` (prepare/check, regras em `work/acentos/PROMPT.md`; resultado em `work/acentos/out/NN.json`, 41 lotes) que `build.py` aplica por cima de tudo; as traducoes-base em `work/out` continuam ASCII. Para corrigir um texto, edite o lote em `work/acentos/out`.
- Largura dos glifos: letra/digito 6 px, espaco 4, resto 7, `@N` (cor) 0. Dialogo: quebra forcada com `/`, 185 px, 2 linhas por pagina; `text_draw_wrapped` quebra em `|`, `/` ou ao estourar a largura (sem respeitar palavras), por isso `build.py` insere as quebras por palavras (alem disso `runtime/patch.c` faz `text_draw_wrapped` quebrar por palavra em tempo de execucao, para os textos que o build nao conhece): dialogo 182 px, descricoes e ajuda 145 px, quests 130 px, descricoes de classe 112 px, texto rolante 140 px.
- Buffers: dialogo 0x11 ate ~990 caracteres; popup 0x1a ate 99 bytes; menu.tx total < 10.000 bytes; `ConstData` blob < 32.767 bytes, nomes/descricoes < 256 bytes.

## Decisoes de nomes
`work/GLOSSARIO.md` (autoridade) e `work/npc_pt.json` (titulos/nomes de NPC). Grafias do BR oficial: Gaicoz, Cazeaje, Kamiki, Gadosen, Ninkoro, Floresta dos Desafios... Nomes proprios sao transliterados, nunca traduzidos (Iron, Windy, Queenie...).

## Grind das missoes e mineracao (2026-11-14)
`grind.py` (chamado por `build.py`) reduz as missoes de matar/coletar com contador > 3 para `max(3, ceil(n/3))` (90 missoes): byte +0x0f do registro de `scp_quest.scp`,
opcode 0x22 (remove item) dos scripts de entrega, descricao da missao e falas que citam o numero (n23 tem "Vinte"/"Dez" por extenso). Nao mexe nas missoes de combinar item
(Onyx da Luz/Trevas, Missao Importante?): o custo delas vem da receita de combinacao no codigo.
`runtime/patch.c` (`hook_resolve_chest`, 0xfcb4): cada minerada solta 4-5 itens (mesma tabela/faixa da barra; faixa -1 continua sem nada) e, com missao de mineracao ativa
(Bijou 0x1f0001, Cristal 0x1f0064, Fragmento de Rubi 0x1e0040 e 0x1f002c), os primeiros itens sao os da missao.
Atencao: `auto.snap`/`estado.snap` guardam a tabela de missoes antiga na memoria; depois de mudar dados, apague-os (ou o jogo retoma com os numeros velhos).
