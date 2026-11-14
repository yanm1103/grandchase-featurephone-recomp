# Problemas do jogo (lista viva)

Formato: [estado] titulo - causa - onde foi tratado / teste que cobre.

## Resolvidos
- [ok] **Movimento falho (para / nao anda)** - o jogo e feito para teclado de celular (uma tecla por vez): soltar QUALQUER tecla chama `stop_player_movement`
  (0x14394), e apertar direcao durante ataque/acao e ignorado. Tratado em `runtime/hle.c` (`gc1_input_key`, camada de entrada): direcoes seguradas viram uma pilha,
  soltar tecla que nao e direcao nao para mais, soltar uma direcao com outra segurada troca para ela, aperto ignorado e repetido quando o jogo fica livre.
  Testes: `tools/playtest/mov_rollover.txt`, `mov_attack.txt` (falham com `--raw`, passam sem).
- [ok] **Correr (toque duplo) quase impossivel** - o jogo conta 4 quadros (160 ms) desde o aperto; a camada nova conta 6 quadros (240 ms) desde a soltura. Teste `mov_correr.txt`.
- [ok] **Musica para e recomeca a cada golpe** - o jogo so tem 1 clipe de audio: para e libera a musica, carrega o efeito no mesmo clipe e depois recarrega a musica do inicio.
  Tratado em `runtime/audio_policy.c` (musica em loop + efeitos mixados por cima; recarregar a mesma musica nao reinicia). SDL: `platform/main_sdl.c` (1 fluxo de musica + 8 de efeitos). Teste `musica_golpe.txt`.
- [ok] **Teclas confusas / sem ajuda** - mapa de teclas configuravel em `platform/keymap.h` (F1 abre a tela de ajuda e permite trocar; salva em `<saves>/controles.ini`).
  Padrao novo: setas/WASD andam, Z/Enter/Espaco atacam, **Esc/X/Backspace = menu/voltar (CLR)**, Q/Tab minimapa, E/M mapa-mundi, F6/P salvar (popup do jogo), V = `*`, C = `#`.
- [ok] **Save state** - **F5 salva / F9 carrega** a foto completa do jogo (qualquer lugar, ate em combate), separado do salvar do proprio jogo (F6/P). F1, F5, F9, F11 sao reservadas.
  O texto de ajuda do jogo (menu.tx linha 13) continua com os nomes do celular ("CLR", "tecla Ligar") + uma linha avisando do F1.

## Em aberto
- [ok] **Palavras cortadas no meio em dicas/avisos** ("teletran-sportar", "visi-tou") - o jogo quebra por caractere em `text_draw_wrapped`; `runtime/patch.c` agora quebra por palavra. Achados nos testes aleatorios: 9 textos (dicas de teletransporte/refino/bau/runas/mudanca de classe, "Digite o valor...", cobranca de conexao); `--wraplog arq` do headless lista os que ainda caem no corte por caractere (palavra maior que a linha).
- [ok] **Acentos** - motor (24 letras acentuadas por patch de glifos + `charset.py`) e texto: os 4.877 textos foram acentuados por agentes em 41 lotes (`tools/translate/acentos.py check`: 0 erros, so acentos adicionados). Conferido por quadros: titulo, intro, dialogos, nomes de lugar, dicas. Sem revisao humana: pode haver acento errado (ex.: "e"/"é", "esta"/"está"); corrija em `work/acentos/out/NN.json` e rode `python -I tools/translate/build.py`.
- [ ] Texto quebra de linha antes do esperado (margem de seguranca em `tools/translate/build.py`: DLG_W/DESC_W/QUEST_W).
- [ ] Traducao PT-BR (retraduzida do coreano) nao foi revisada por humano; telas nao conferidas: lojas, inventario, habilidades, HUD de combate, finais.
- [~] Port Android: APK funcionando no emulador (boot, toque, D-pad, ataque, save state carregado) - ver `android/README.md`. Falta: testar num celular real, ouvir o audio, tablet/paisagem, auto-save ao pausar, tela de ajuda no toque, APK de release (hoje debuggable, chave de debug).
- [ ] `build.sh all`/`ref` nao liga (falta `gc1_cov_dump` no backend unicorn) - ja estava assim antes; `headless` e `sdl` funcionam.
- [ ] **Reduzir o tamanho do audio** - `build/out/audio` tem ~76 MB de WAV sem compressao (e vai igual dentro do APK de ~105 MB; o original era MIDI/MMF).
  Ideia: converter para OGG/Opus e decodificar ao carregar. Cuidado: `runtime/audio_policy.c` identifica o clipe pelo hash do conteudo (`ap_fnv64`), entao
  o hash tem que continuar sendo calculado sobre os dados originais (ou mapear hash -> arquivo convertido). Fazer depois de confirmar que o audio funciona no celular.
  Extra: APK so com arm64-v8a (sem x86_64, que e so do emulador) tira ~11 MB.
