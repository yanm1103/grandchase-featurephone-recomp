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
- **So ASCII**: bytes >= 0xb0 sao hangul para o motor e a fonte nao tem acentos (o BR oficial da epoca tambem nao tinha). Escreve-se "voce", "nao".
- Largura dos glifos: letra/digito 6 px, espaco 4, resto 7, `@N` (cor) 0. Dialogo: quebra forcada com `/`, 185 px, 2 linhas por pagina; `text_draw_wrapped` quebra em `|`, `/` ou ao estourar a largura (sem respeitar palavras), por isso `build.py` insere as quebras por palavras: dialogo 182 px, descricoes e ajuda 145 px, quests 130 px, descricoes de classe 112 px, texto rolante 140 px.
- Buffers: dialogo 0x11 ate ~990 caracteres; popup 0x1a ate 99 bytes; menu.tx total < 10.000 bytes; `ConstData` blob < 32.767 bytes, nomes/descricoes < 256 bytes.

## Decisoes de nomes
`work/GLOSSARIO.md` (autoridade) e `work/npc_pt.json` (titulos/nomes de NPC). Grafias do BR oficial: Gaicoz, Cazeaje, Kamiki, Gadosen, Ninkoro, Floresta dos Desafios... Nomes proprios sao transliterados, nunca traduzidos (Iron, Windy, Queenie...).
