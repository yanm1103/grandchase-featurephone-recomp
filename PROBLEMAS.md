# Pendências do projeto (lista viva)

Só o que falta. O que já foi feito saiu daqui (histórico no git e nos docs: `docs/`, `android/README.md`, `tools/playtest/README.md`, `tools/translate/README.md`).
Formato: [ ] a fazer, [~] feito pela metade.

## Testar / conferir
- [ ] **Celular, APK instalado em 2026-11-14 sem conferir:** botão SOBRE da gaveta (créditos + wons/medalhas/cupons), loja do Dennis (aba Suprimentos nunca foi vista na tela, só o estoque na memória), correção do `auto.snap` (minimizar e voltar; abrir o app depois de sair pelo jogo não deve mais voltar a ponto velho).
- [ ] **Andar na diagonal:** conferir no jogo real saídas de mapa, NPC e baú passando na diagonal e quinas estreitas (`GC1_DIAGDBG=1` avisa se o jogador parar em cima de parede).
- [ ] **Ranking da Comunidade:** o servidor falso usa como pontuação o primeiro valor que o jogo manda (maior campo +3 dos saves), sem saber o que é; ver se faz sentido jogando.
- [ ] **Cupom/medalha:** conferir no jogo real que a medalha aparece na caixa "Obteve..." e o cupom no aviso do topo e em Comunidade > Cupons (testado só com `GC1_NETLUCK=100` e direto no código).

## Rede e itens (decisões do usuário em 2026-11-14: tudo grátis e offline; ver `docs/INVESTIGACAO_REDE_ITENS.md`)
- [ ] **Itens sem fonte local:** as curas e receitas entraram na loja do Dennis, mas rosto (20 itens), anéis, colares, asas, pets (Goldie, CaoCao, Ninko) e peças de set (Comandante, Celestial, Cristal, Alma Sagrada, Bulls) ainda não saem de lugar nenhum. A tela da loja do Dennis só mostra Cura/Suprimentos: precisa de outra loja (equipamentos) ou de drops raros.
- [ ] **Preços do Dennis são chute:** bilhetes de chefe e05-e09 (25.000 a 60.000 GP) e pacotes de armadura e4f-e54 (30.000 a 130.000 GP). Ajustar em `tools/translate/dennis.py` (`PRECOS`); a lista de itens está em `runtime/patch.c` (`DENNIS_STOCK`).
- [ ] **Texto da tela de abertura (ui 0x41b24)** ainda promete cupons do Grand Chase online da Netmarble.
- [ ] **Contador de wons é global** (`<saves>/online.sav`), não por slot; o aviso do cupom é um aviso da plataforma (a caixa "Obteve..." do jogo não tem espaço para o código).
- [ ] Mercado entre jogadores, correio e loja de cash: inalcançáveis (servidor falso responde "Serviço online encerrado"); não vale reimplementar.

## Telas e textos
- [ ] **Tela de contexto única no primeiro boot** no lugar das duas telas de texto iniciais (a primeira é "[Aviso] Este jogo é avulso e não cobra para jogar..." ui 0x41a10, que já não bate com o que existe; a segunda não foi capturada): diagonal, grind 1/3, mineração, save/load state, filtros, tela Sobre. Pode reaproveitar `about_draw` (`platform/keymap.h`).
- [ ] Texto quebra de linha antes do esperado (margem de segurança em `tools/translate/build.py`: DLG_W/DESC_W/QUEST_W).
- [ ] **Tradução PT-BR e acentos sem revisão humana** (retraduzida do coreano por agentes; pode haver "e"/"é", "esta"/"está" trocados). Telas não conferidas: lojas, inventário, habilidades, HUD de combate, finais. Corrigir em `tools/translate/work/acentos/out/NN.json` (vence `overrides.json`) e rodar `python -I tools/translate/build.py`; depois apagar `build/pt/states/` (os estados guardam os textos).
- [ ] O texto de ajuda do jogo (menu.tx linha 13) ainda cita os nomes do celular ("CLR", "tecla Ligar").

## Android
- [ ] Tela de ajuda de teclas (F1) no toque.
- [ ] APK de release (hoje `debuggable`, chave de debug).
- [ ] Medir custo de CPU/GPU/bateria do filtro ScaleFX jogando no celular.

## Gráficos
- [ ] Ghosting (xBRZ/ScaleFX) não implementado.
- [ ] **Licença:** `platform/shaders/xbrz.frag` é GPLv3 (HqMAME); ver antes de distribuir binários.

## Framerate (plano em `docs/INVESTIGACAO_FRAMERATE.md`, nada implementado)
- [ ] Decidir: período exato de 60 ms vs travado no vsync; forçar 50 Hz vs interpolação; +60 ms de latência vs extrapolação; velocidade 2 como padrão do jogo novo; escopo da interpolação; janela do toque duplo (6 quadros).

## Build e publicação
- [ ] `build.sh all`/`ref` não liga (falta `gc1_cov_dump` no backend Unicorn); `headless` e `sdl` funcionam.
- [ ] Antes de publicar o repositório: licenças (xBRZ GPLv3, Unicorn GPL só no backend de referência), textos traduzidos, ícone, nome/e-mail no histórico do git.
- [ ] **Commitar:** servidor falso, tela Sobre, loja do Dennis e correção do `auto.snap` estão no disco, sem commit.
