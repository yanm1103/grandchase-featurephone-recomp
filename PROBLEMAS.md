# Pendências do projeto (lista viva)

Só o que falta. O que já foi feito saiu daqui (histórico no git e nos docs: `docs/`, `android/README.md`, `tools/playtest/README.md`, `tools/translate/README.md`).
Formato: [ ] a fazer, [~] feito pela metade.

## Bug aberto (prioridade)
- [~] **CAUSA ACHADA (2026-11-16) e corrigida, falta o usuário confirmar jogando:** `load_monster_name` (0x14420) copia o nome do monstro para um buffer de 20 bytes (`monster_name_buf`, 0x162b731) sem checar o tamanho; "Capitão Arqueiro Esqueleto" (26) e mais 10 nomes em PT passavam disso e pisavam em `dialog_active` (a caixa "O baú guarda..." ficava com valor 117; o jogo fecha com `^1`, então 116 continua "aberta" e nenhuma tecla funcionava), `save_slot_index` (o save ia para o slot 101: **explica o "salvei e voltou a ponto velho" abaixo**, o `Grand0.sav` do celular parou em 14/11 03:53) e `boss_warning_active`. Correção: nomes encurtados para <= 19 bytes (`work/acentos/out/04..06.json`), trava `hook_monster_name` em `runtime/patch.c` e `sanitize_overflow` ao carregar foto de estado. Falta: usuário jogar e salvar pelo jogo (SALVAR > Salvar) e conferir que `Grand0.sav` ganhou horário novo. Auditoria de buffers feita (2026-11-16, `tools/translate/audit_len.py`): nomes de item (<= 27 bytes) estouravam buffers de pilha de 32 bytes ("+N " + nome) e a linha "Local:" da missão (100 bytes) podia estourar com 4 mapas longos; corrigido (nomes encurtados + corte em `sprintk_cap`, `runtime/hle.c`). Nomes de NPC/habilidade/mapa/menu e diálogos conferidos e seguros.
- [ ] **Android: "Continuando de onde parou" / o jogo volta a um ponto velho, mesmo salvando.** O usuário relatou em 2026-11-14 (depois da correção do `auto.snap`, APK `c3319bb` instalado) que continua errado; salvar pelo jogo (não o estado) também parecia não valer. O que se sabe (veja a causa provável acima: slot de save corrompido pelo nome do monstro):
  - O save do jogo grava: `files/save/Grand0.sav` (+ `Grand3`, `Grand5`) com horário novo (02:39 e 02:56); só existe o slot 0.
  - O aviso "Continuando de onde parou" só sai de `auto.snap` (platform/main_sdl.c, bloco `__ANDROID__` depois de `gc1_start`). Às 02:56 o `auto.snap` NÃO existia no celular (a correção apaga ao voltar ao primeiro plano e depois de carregar), então se o aviso apareceu, ele veio de um `auto.snap` gravado ao minimizar e carregado logo em seguida, ou o usuário viu o aviso numa versão anterior do app. Não se sabe qual.
  - Hipóteses a testar: (1) o `WILL_ENTER_BACKGROUND` grava a foto num momento antigo/errado (ex.: o app minimiza ao abrir o seletor de apps e a foto sai com o estado de antes do último save do jogo); (2) o `SDL_EVENT_DID_ENTER_FOREGROUND` não chega e o `auto.snap` não é apagado; (3) o app é morto sem evento (a foto de uma minimização anterior fica) e a próxima abertura carrega essa foto por cima do save; (4) o "ponto velho" é o ponto de retorno do próprio jogo (o save guarda posição/cidade, não o ponto exato), e não o `auto.snap`.
  - Como investigar: no celular, `adb shell run-as dev.gc1.recomp ls -la files/save` antes e depois de cada passo (jogar, salvar pelo jogo, minimizar, matar o app, abrir) e `adb logcat -s SDL` para ver quando a foto é gravada/carregada; considerar desligar o `auto.snap` de vez (deixar só o save normal e o estado manual da gaveta) se não houver jeito de torná-lo confiável.

## Testar / conferir
- [ ] **Celular, APK instalado em 2026-11-14 sem conferir:** botão SOBRE da gaveta (créditos + wons/medalhas/cupons), loja do Dennis (aba Suprimentos nunca foi vista na tela, só o estoque na memória), correção do `auto.snap` (minimizar e voltar; abrir o app depois de sair pelo jogo não deve mais voltar a ponto velho).
- [ ] **Andar na diagonal:** conferir no jogo real saídas de mapa, NPC e baú passando na diagonal e quinas estreitas (`GC1_DIAGDBG=1` avisa se o jogador parar em cima de parede).
- [ ] **Ranking da Comunidade:** o servidor falso usa como pontuação o primeiro valor que o jogo manda (maior campo +3 dos saves), sem saber o que é; ver se faz sentido jogando.
- [ ] **Cupom/medalha:** conferir no jogo real que a medalha aparece na caixa "Obteve..." e o cupom no aviso do topo e em Comunidade > Cupons (testado só com `GC1_NETLUCK=100` e direto no código).

## Rede e itens (decisões do usuário em 2026-11-14: tudo grátis e offline; ver `docs/INVESTIGACAO_REDE_ITENS.md`)
- [~] **Itens sem fonte local:** rosto (20), colares/anéis, asas, peças de set (Comandante, Celestial, Cristal, Alma Sagrada, Bulls) e pets (Goldie, CaoCao, Ninko) agora entram nas lojas de armadura (abas Elmo/Armadura/Capa/Luvas/Bota; rosto na aba Elmo, colares e anéis na aba Capa) e de pets (`hook_shop_rebuild` em `runtime/patch.c`). Conferido só pela memória (`tools/playtest/loja_extras.txt` com `GC1_SHOPDBG=1`); falta ver a tela jogando (ícones de rosto/colar/anel e a grade de 40 vagas).
- [ ] **Contador de wons é global** (`<saves>/online.sav`), não por slot; o aviso do cupom é um aviso da plataforma (a caixa "Obteve..." do jogo não tem espaço para o código).
- [ ] Mercado entre jogadores, correio e loja de cash: inalcançáveis (servidor falso responde "Serviço online encerrado"); não vale reimplementar.

## Telas e textos
- [ ] **Tela de contexto única no primeiro boot** no lugar das duas telas de texto iniciais (a primeira é "[Aviso] Este jogo é avulso e não cobra para jogar..." ui 0x41a10, que já não bate com o que existe; a segunda não foi capturada): diagonal, grind 1/3, mineração, save/load state, filtros, tela Sobre. Pode reaproveitar `about_draw` (`platform/keymap.h`).
- [ ] Texto quebra de linha antes do esperado (margem de segurança em `tools/translate/build.py`: DLG_W/DESC_W/QUEST_W).
- [ ] **Tradução PT-BR e acentos sem revisão humana** (retraduzida do coreano por agentes; pode haver "e"/"é", "esta"/"está" trocados). Telas não conferidas: lojas, inventário, habilidades, HUD de combate, finais. Corrigir em `tools/translate/work/acentos/out/NN.json` (vence `overrides.json`) e rodar `python -I tools/translate/build.py`; depois apagar `build/pt/states/` (os estados guardam os textos).

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
