# Grand Chase 1 (WIPI-C): uso de rede/online, itens dependentes e plano para o port offline

Investigação somente leitura (nenhum arquivo do recomp/decomp foi alterado; nada foi compilado ou executado). Data: 2026-11-14.
Legenda: **[FATO]** = verificado no código decompilado ou nos dados do jogo (endereço/arquivo citado). **[HIPÓTESE]** = inferência, não comprovada.

## STATUS (2026-11-14): servidor falso implementado
Decisoes do usuario: pergaminho e chaves **de graca**, mas somando os wons "que teria gasto na epoca" numa tela Sobre (tambem mostra medalhas); cupons e medalhas falsos (codigo aleatorio) **junto com os itens**; ranking com jogadores aleatorios.
Implementado em `runtime/netsim.h` + sockets do `runtime/hle.c` (resposta 0x65/0x69/0x6b/0xc9; o resto -> "Servico online encerrado."), tela Sobre em `platform/keymap.h` (`about_draw`) e `platform/touch.h` (4o botao da gaveta). Detalhes e o que falta: `PROBLEMAS.md` (item "Rede / itens..."). P3 feito: Dennis virou loja local (curas/receitas sem fonte, bilhetes de chefe e pacotes de armadura; ver PROBLEMAS.md). Ainda em aberto daqui: os itens de rosto/aneis/colares/asas/pets/pecas de set sem fonte e o texto de cupons da tela de abertura.

## 0. Resumo (leia só isto se tiver pouco tempo)

1. O cliente fala um protocolo binário próprio sobre TCP com `210.217.196.90:10022` (string em 0x42858, porta 0x2726). Há **uma única porta de entrada para o "modo online" completo (mercado de jogadores + correio)**: o NPC **Dennis (NPC 56, "Mercador Viajante"/"특별 상인")**, presente nas 5 cidades (mapas 1, 3, 6, 7, 8), opção "Conectar à rede" (script tipo 9, `n56.scp`). Fora isso, só 3 usos de rede: **comprar Pergaminho de Ressurreição** (menu de morte), **abrir Baú do Tesouro com chave paga** (inventário) e **menu principal > Comunidade** (ranking, cupons, medalhas).
2. **Ninguém na rede entrega itens "que só existem online".** O cliente decide localmente o que cai de uma chave (`roll_loot_drops`, 0x8e88). O servidor só cobrava o dinheiro e decidia "cupom/medalha". Porém **42 itens só saem dessa tabela de chaves** (9 armas topo de linha, 3 pets, 5 bilhetes de chefe, 3 sets de armadura via "Pacote", etc.) e **63 itens não têm nenhuma fonte local** (20 itens de rosto, anéis, colares, partes de sets, 3 pets, curas de status...), candidatos a "só mercado/correio/loja de cash" ou conteúdo cortado.
3. Hoje, no port, `Connect` falha de propósito, então: **o Baú do Tesouro (cai em ~5% das mortes de 129 dos 137 monstros) não pode ser aberto** e, depois dos 5 pergaminhos iniciais, **não há como reviver no local**. Isso trava 42 itens e 5 dos 8 desafios de chefe (bilhetes só vêm das chaves).
4. **Recomendação:** (a) implementar um "servidor falso" no HLE de sockets (`runtime/hle.c`) que responde ao pedido 0x65 (chave/pergaminho) e aos pedidos 0x69/0x6b/0xc9 (Comunidade) e devolve erro com texto para o resto; (b) reaproveitar Dennis como loja local (dados apenas) e assim tornar o modo online (mercado/correio) inalcançável; (c) decidir o preço (grátis ou em GP). Esforço total estimado: 2 a 3 dias, risco baixo-médio.

## 0.1 Arquivos e endereços-chave

Decompilado (`c:\Games\GrandChase1-decomp\decompiled\c\`):

| Assunto | Função (endereço) | Arquivo |
|---|---|---|
| Montar pedido | `net_build_request` 0x1febc | 1f000.c:722 |
| Tratar resposta | `net_handle_response` 0x20640 | 20000.c |
| Enviar / estado | `net_send_request` 0x213ec, `net_set_service_mode` 0x21444, `net_send_current_request` 0x21308, `net_write_pending` 0x21210, `net_read_response` 0x210c4 | 21000.c |
| Conexão | `net_begin_connection` 0x1fcf4, `net_on_bearer_connected` 0x1fcb4, `net_connect_to_server` 0x1fc5c, `net_close_connection` 0x1fb34 | 1f000.c |
| Chave/baú | `finish_chest_open` 0x19218 (19000.c), `roll_loot_drops` 0x8e88 (08000.c), UI de chave 1c000.c ~880-960, envio 18000.c:998 | |
| Reviver | `death_menu_key` 0x13f20, `has_revive_scroll` 0x13e04, `revive_player_in_place` 0x13ebc (13000.c) | |
| Comunidade | `menu_online_key` 0x15804, `open_ranking_page` 0x156b8, `open_medals_page` 0x15698 (15000.c) | |
| Modo online (mercado/correio) | `online_menu_init` 0x3ac14, `online_entry_screen_key` 0x3ae38, `online_lobby_key` 0x3b150, `market_buy_key` 0x3b2a0, `market_sell_key` 0x3b7f8, `mail_compose_key` 0x3bed8, `mailbox_key` 0x3c5f8, `online_menu_on_key` 0x3caac | 3a000.c..3c000.c |
| Entrada no modo online | `script_choice_menu_key` 0x2ef68, trecho `script_choice_menu_type == 9` -> `set_game_mode(6)` | 2e000.c:~1193 |

Globais úteis (de `tools/build/globals_all.tsv`): `net_current_msg_id` 0x163683c, `net_service_mode` 0x1637790, `net_request_pending` 0x16329e5, `net_error_code` 0x162ba98, `net_status` 0x162ba5a, `net_connected` 0x1637791, `net_must_reconnect` 0x1636f04, `net_socket` 0x1638508, `net_recv_buf` 0x1636e04 (ponteiro, 2000 B), `net_send_buf_ptr` 0x1637414 (1000 B), `net_io_remaining` 0x16375a4, `net_io_total` 0x16375a0, `net_done_callback` 0x163726c, `chest_server_response` 0x1637324 (ponteiro para o corpo da resposta), `key_result_has_medal` 0x162b790, `key_result_is_coupon` 0x16302c0, `chest_choice` 0x163643c, `items_page_substate` 0x163623a (6 escolher chave, 7 confirmar, 8 resultado, 9 falha), `death_menu_state` 0x1500070 (4 = oferta de compra), `player_gp` 0x162c1a0, `last_loot_ids` 0x162ca8c.

Dados: `$GC1_EXTRACTED/ko/jar/` (`ConstData.br`, `n56.scp/.dat`, `g_00NN.mgd`, `scp_map`), tabelas de texto em `tools\translate\work\` (`catalog.json`, `acentos\out\*.json`). Scripts de varredura que usei: `C:\Users\TWISTZ~1\AppData\Local\Temp\claude\74933114b6a533e3\44dfdc7e-37b9-4cc5-8433-595c36698bee\scratchpad\items2.py`, `sweep.py` (resultado em `items.json`, `ref.json` na mesma pasta).

## 1. Mapa completo do que usa rede

### 1.1 Transporte [FATO]
- `net_begin_connection` (0x1fcf4) consulta as propriedades `DS_LOCK`, `ROAMING_AREA`, `AIRPLANE_MODE` (erros 0xb/0xc/0xd), depois `WIPIC_Connect` -> `net_on_bearer_connected` (0x1fcb4) cria o socket `WIPIC_UnkSocket(2,1,...)` -> `net_connect_to_server` (0x1fc5c): `parse_ipv4_address("210.217.196.90")`, `Htons(0x2726=10022)`, `WIPIC_SocketConnect(sock, ip, porta, cb 0x213a1)` -> `net_on_socket_connected` (0x213a0) -> `net_send_current_request` -> `net_build_request` -> `net_write_pending` (SocketWrite; `-0x13` = rearma `SetWriteCallback`) -> `net_prepare_receive` -> `net_read_response` (SocketRead; `-0x13` rearma `SetReadCallback`) -> `net_handle_response`.
- Quadro de **pedido**: `[u16 tam_corpo][u16 id]` + corpo (little-endian; `net_write_packet_header`). Total = tam_corpo + 4. A maioria dos corpos começa com 12 bytes = a string em 0x42890 (`'011111111112'`, placeholder de número de telefone) ou com os 16 bytes de `net_player_id` (0x1637588).
- Quadro de **resposta**: `[u16 tam][u16 id][u8 status][corpo]`. `net_handle_response` faz `net_io_total = tam + 4` (o `tam` inclui o byte de status). Status 0 e 0x29 = ok; outro valor = erro: lê `[s16 len][texto]` em `net_error_code = 4` e mostra o texto (copiado para 0x1636e3c).
- Timeouts: 30 s sem E/S fecha (`net_io_active`), 120 s ocioso fecha (`net_idle_timer_armed`).
- Id da resposta = id do pedido + 1 em todos os pares abaixo.

### 1.2 Tabela de mensagens [FATO, salvo indicação]

| Pedido -> Resposta | Quem envia (chamador) | Corpo do pedido | Resposta (o que o cliente faz) | O que o jogador perdia sem servidor |
|---|---|---|---|---|
| **0x65 -> 0x66** "serviço" | Menu de morte (13000.c:774) e confirmação de chave (18000.c:998), seguidos de `net_set_service_mode(modo,0)` | 0x10 B: 12 B `'011111111112'` + 1 B **modo** (0x1637790) + 3 B zero | status 0; corpo: [0] desconhecido, **[1] = medalha** (`key_result_has_medal`), **[2] = é cupom** (`key_result_is_coupon`), **[3..0x34] = texto do cupom (0x32 B)**, **[0x35..0x44] = código (0x10 B)**. Tratamento por `net_service_mode`: modos **1, 5, 10, 14 -> `revive_player_in_place`**; qualquer outro -> `finish_chest_open` | Reviver no local após acabarem os 5 pergaminhos iniciais; abrir o Baú do Tesouro (ver 1.3, 1.4) |
| **0x69 -> 0x6a** lista de cupons | Comunidade > Cupons (15000.c, subestado 2/6) | 12 B id + u32 `net_list_offset` (pagina*4) + u8 4; corpo total 0x14 | corpo: [0]=contagem (<=4), a partir de [1] entradas de 0x42 B = código 0x10 + texto 0x32 -> 0x162ba0a / 0x162f255 | Ver cupons guardados (cupons só valiam no Grand Chase online PC da Netmarble) |
| **0x6b -> 0x6c** medalhas | Comunidade > Medalhas (substado 3) | 12 B id (corpo 0x14) | corpo: [0] = nº de medalhas -> "Você possui %d medalhas" (0x162ce08) | Nada em jogo; medalhas só serviam para eventos no site Netmarble Mobile (menu.tx 17, tópico "Medalha") |
| **0xc9 -> 0xca** ranking | Comunidade > Registrar ranking (substado 1) | 0x2c B: 12 B id + u32 A + u32 B (o maior valor em +3 / +7 dos arquivos `Grand0..2.sav`, **[HIPÓTESE]** pontuação) + 20 B `PHONEMODEL` + u8 `DAT_01633750` + u8 0x68 | corpo: u32 `my_rank`, u32 `my_score`, 0x80 B = 8 entradas de 16 B (telefone mascarado + pontos) | Ranking online |
| 0x12d -> 0x12e / 0x12f -> 0x130 | **Nenhum chamador encontrado** (grep em todo o decompilado) | lista paginada (0x14) / id da entrada | 0x130 insere o item no inventário | Provável recurso "Presentear" (menu.tx 36) cortado: **[HIPÓTESE]** código morto |
| 0x191 -> 0x192, 0x193 -> 0x194, 0x195 -> 0x196 | `online_entry_screen_key` (3ae38) | checar ID / registrar ID de 16 B / confirmar | 0x192 grava `net_player_id` (status 0x29 = sem ID); 0x194 e 0x196 viram `online_id_status` | Conta/ID do mercado |
| 0x2bd -> 0x2be | `online_menu_update` (3ac9c) | 16 B id + u8 0x68 | u32 nº de mensagens novas + u8 n + n x [s16 len][texto] ("Avisos") | Avisos do servidor |
| 0x1f5 -> 0x1f6 / 0x1fb -> 0x1fc | `market_buy_key` / `market_sell_key` | busca por tipo/grau/nível/ordem ou nome (0x14 B); minhas vendas | até 20 ofertas: 20 x 12 B de cabeçalho (+8 = preço) seguidas dos itens (0x28 B equip/pet, 0xc runa, 0x10 cura/util/misc) em `net_recv_items_*` | Mercado jogador-a-jogador |
| 0x1f7 -> 0x1f8 (comprar), 0x1f9 -> 0x1fa (vender), 0x1fd -> 0x1fe (cancelar) | `market_*_key` | id da oferta / item completo + preço (remove o item do inventário e salva) | 0x1f8 faz `add_gp(-preço)`; o item comprado **não** entra aqui (vem pelo correio, 0x261) | idem |
| 0x259 -> 0x25a (enviar), 0x25b -> 0x25c (lista), 0x25d -> 0x25e (abrir), 0x25f -> 0x260 (apagar), 0x261 -> 0x262 (receber anexos) | `mail_compose_key` / `mailbox_key` | ID destino, texto (0x50), GP, até 3 anexos (corpo ate ~0x78+) | 0x262 soma GP e chama `mailbox_item_to_inventory` (0x6944) por anexo | Correio com anexos e entrega de compras do mercado |

Obs.: `net_build_request` (1f000.c) só monta os ids acima; a lista do comentário do Ghidra bate com o switch.

### 1.3 Pontos de entrada para o jogador [FATO]

1. **Menu de morte, "Renascer" sem pergaminho** (13000.c:774-790): se `has_revive_scroll` (item **0xe4e**) -> consome e `revive_player_in_place`; senão estado 4 ("Sem pergaminho... custava 100 won") -> `net_send_request(0x65)` + `net_set_service_mode(1 ou 5)`. O modo é 1 se `DAT_01633750 != 0`. Esse byte vem de `Grand5.sav` (byte 0, default 1 em `game_init`); **não achei nenhuma escrita** no decompilado, então na prática é sempre 1. Outras opções do menu de morte (voltar à vila com -10% EXP/-5% GP, menu principal) funcionam offline. Fonte local do pergaminho: só os **5 iniciais** (`init_new_game_state` 0x8bc0: loop 5x `inventory_add_item(0xe4e,1)`); nenhuma loja, drop ou script o dá.
2. **Baú do Tesouro (misc 0xf41, "보물상자")** (18000.c:~860-1017, 1c000.c:880-960): "Usar" -> substado 6 "Com qual chave vai abrir?" (3 opções: Enferrujada 500 won / Ferro 900 won / Ouro 1300 won, `chest_choice` 0..2) -> substado 7 confirmação -> `net_send_request(0x65)` + modo `chest_choice+2` (2,3,4; ou +6 se `DAT_01633750==0`) -> resposta 0x66 -> `finish_chest_open` (0x19218): `use_consumable_item` (consome o baú), e **se `chest_server_response[2]==0` sorteia localmente `roll_loot_drops(chest_choice)` (n+1 prêmios: 1, 2 ou 3)**; senão copia cupom (código 0x10 B + texto 0x32 B). Substado 8 = resultado ("Obteve ..."), 9 = "Falha de conexao.". Exige 4 vagas livres no inventário (checagem em 18000.c e `use_consumable_item`).
   - **Baús caem de monstros:** 129 dos 137 monstros têm 0xf41 na tabela de drops (slot 10 ou 11 de `spawn_monster_loot`, 0x10e70 = 5% por morte). **[FATO]** (varredura da tabela `mon` de ConstData, +42, 12 x s16, base 0x5f80).
   - O "baú" de mapa (objeto 0xe6, minigame da barra, `resolve_chest_minigame` 0xfcb4) é **outra coisa**: 100% local (mineração; já tem o hook `hook_resolve_chest` em patch.c).
3. **Menu principal > Comunidade** (menu.tx 4; `menu_online_key` 0x15804): "Registrar ranking" (0xc9), "Cupons" (0x69), "Medalhas" (0x6b). O substado 4 ("Presentear", menu.tx 36) não tem pedido associado (cursor máximo = 2). Hoje todas mostram erro de conexão.
4. **Dennis (NPC 56)**, só ele: `n56.scp` = `op 0x25` operando 9 -> `script_open_npc_choice_menu(9)` (0x326bc) abre a entrada 5 de `npcmenu.tx` (ko `네트워크접속@대화@나가기`, PT "Conectar à rede@Conversar@Sair"); a opção 0 executa `set_game_mode(6)` (2e000.c:~1193) = **modo online** (`online_menu_init`). Confirmei em todos os 91 scripts: tipo 9 só aparece em `n56.scp`. Dennis aparece em `g_0001, g_0003, g_0006, g_0007, g_0008.mgd` (Praça de Serdin, Vila Élfica, Praça de Canaban, Vila Costeira de Elia, Vila das Ruínas); nenhum script o cria por `op 0xc`.
   - No modo online: tela 0 (aviso de tarifa 0x438c8 "Conectar?", cadastro de ID, avisos), tela 1 lobby (menu.tx 43: "Negociar / Correio / Loja de cash"), tela 3 mercado (menu.tx 44-46: comprar por condição/nome, vender, itens à venda), tela 4 correio (menu.tx 47). **A tela 5 ("Loja de cash") não existe no `switch` de `online_menu_on_key` (só casos 0..4)**: abrir a loja de cash não faz nada. **[FATO]**
   - O registro de Dennis em ConstData tem tipo de loja 0x0a e **os 9 slots de itens zerados** (loja local vazia). **[FATO]**
5. "Opções > Gerenciar dados" (menu.tx 30-33, "enviar dados ao servidor") **não tem manipulador** (`menu_options_key` 0x15a78 só trata cursor 0..3) e nenhum id de mensagem corresponde. **[FATO]** (código morto)
6. Tela de abertura: "Móvel / Online" e o texto "Os baús deste jogo podem fornecer cupons..." (ui 0x41b24) são só gráfico/texto (`draw_boot_screen`, 0x15204); não fazem rede. **[FATO]**

### 1.4 O que o jogador perdia sem servidor (hoje, no port)
- Reviver no local depois do 5º uso (mitigável com "Na vila").
- **Todo o conteúdo das chaves** (seção 2.1): é a perda mais séria, porque baús caem sempre e não abrem.
- Cupons, medalhas, ranking: nada fora do site Netmarble; não afetam o jogo.
- Mercado, correio, "loja de cash", ID: economia entre jogadores (não recuperável sem servidor real).

## 2. Itens que só vinham da rede

Método: tabelas de itens de `ConstData.br` (id = `(categoria<<8)|índice`; 492 itens: 277 equip, 9 pet, 23 runa, 32 cura, 85 util, 66 misc) contra **todas** as fontes locais que consegui enumerar: drops de monstros (137 x 12 ids), listas e estoque de lojas (registros de NPC + `fill_armor_shop_stock`/`shop_rebuild_stock`), `op 0x21` de todos os scripts, receitas `scp_mix`, tabelas de `roll_loot_drops` e `roll_chest_loot`, itens iniciais, caixas de set (`item_set_table`, 24 ids em 0x162a624..). 429 itens têm pelo menos uma fonte; **63 não têm nenhuma**; **42 só têm fonte nas chaves/caixas**.

Limitações honestas: (i) a loja de armaduras foi lida por regex do decompilado (30000.c) e a de armas por interpretação do registro de NPC; um erro aí poderia mover alguns itens de "sem fonte" para "com fonte"; (ii) a loja de pets decompilada só insere 0xb00 (Slime), embora o registro de NPC liste 0xb00/b02/b04: tratei os três como à venda; (iii) itens usados só como alvo de missão ("ter N itens") não contam como fonte.

### 2.1 Só obtidos pelas chaves pagas (fonte única = tabela de `roll_loot_drops` ou caixa "Pacote") [FATO]

A tabela (0x8e88, para cada um dos n+1 prêmios): faixa 1 (65%) = Soro 0xd19 (15%), bilhetes de chefe 0xe05..e09 (30%), Rubi/Safira/Topázio/Esmeralda 0xe14/17/1a/1d (14%), Cristal/Gema/Bijou 0xe0a-c (4%), Pedra de Proteção 0xe0d, **Pacotes 0xe4f/e50** (6-8%), **arma da classe idx 0x17 "do Supremo"** (5%); faixa 6 (10%) runas poderosas 0xc10-13, receitas e36-3d, corações e22-25; faixa 2 (7%) **runas antigas 0xc14-16**, receitas e46-4a, purificações e26-29; faixa 3 (6%) **Pacotes e51/e52/e53 ou arma idx 0x18**; faixa 4 (6%) **pets 0xb06/b07/b08**; faixa 5 (6%) **Pacote e54 ou arma idx 0x19**. Cada Pacote (util subtipo 7, `use_consumable_item` 0x87b4) entrega 4 peças do set (`give_item_random_grade`, exige 4 vagas).

Itens cuja **única** fonte é essa (42):
- **9 armas por classe**: Espada/Lança/Duplas "do Supremo" (0x417/0x517/0x617), "de Felis" (0x418/0x518/0x618), "de Kamiki/Ishkelon" (0x419/0x519/0x619).
- **3 pets**: Icy 0xb06, Luna 0xb07, Lunatic 0xb08.
- **5 bilhetes de desafio**: Gadosen 0xe05, Basilisco 0xe06, Kamiki 0xe07, Cazeaje 0xe08, Cazeaje Verdadeiro 0xe09. `boss_challenge_menu_key` (0x2dc64) exige o item `0xe02+n` para o chefe n (0..7); só 0xe02/e03/e04 são vendidos (Sebas, mapa 141) e 0xe01 (Expedição) por Marsha. **Os 5 últimos chefes da Expedição de Monstros dependem das chaves.**
- **6 Pacotes de armadura** 0xe4f..e54 (set 1 Valquíria, 2 Supremo, 3 Rei do Castelo, 4 Deus Egípcio, 5 Cazeaje, 6 Cérbero) e, como consequência, as peças que só estão nos sets: Armadura/Luvas/Botas do Supremo (0x71e/0x91e/0xa1e), de Cazeaje (0x71f/0x91f/0xa1f), Cérbero (0x720/0x920/0xa20) e Elmo Cérbero (0x21d).
- **Runas antigas** 0xc14/c15/c16, **receitas** 0xe36 (Runa de Poder Força), 0xe46/e47/e48 (Purificações), 0xe4a (Cápsula de HP), **Soro** 0xd19.
- Outras 28 (gemas, receitas de gema, corações, purificações, runas poderosas) também caem das chaves mas têm segunda fonte local (drop/loja/craft).
- **Pergaminho de Ressurreição 0xe4e**: fora os 5 iniciais, só a compra online.
- **Baú do Tesouro 0xf41** em si cai de monstros; é o único elo que **depende** da rede.

Conclusão: dentro do cliente, "itens exclusivos da rede" = **o conteúdo das chaves**. Não existe item-chave "mobile" separado; o texto "item mobile" das chaves (ui 0x420f8...) é só o nome que o jogo dava ao prêmio local (em oposição ao "cupom online").

### 2.2 Sem nenhuma fonte local (63 itens) [FATO como varredura; origem = HIPÓTESE]

- **Rosto/cat 1 (20 itens, 0x100-0x113)**: óculos (Fofo, Natação, Googles, Sol, Fundo de Garrafa, Tapa-olho), chifres (Unicórnio, Dragão, Cabra, Palhaço, Herói, Fênix), máscaras (Oxigênio, Negra, Cabra, Palhaço, Periton, Herói, Fênix) e Bico do Tiger Hawk. Todos com preço 400 GP, sem atributos. O filtro do mercado tem a categoria "Face" (`market_filter_type_name` 0x3cb00) e o equipamento de rosto tem slot (`item_category_to_equip_slot` 0x72b0: 1->slot 0). **Hipótese: itens da "loja de cash"/mercado** (cosméticos).
- Equipamentos: Elmos do Comandante 0x209 e Celestial 0x213; Colares 0x302/303/304 (Cavaleiro do Gelo/Chamas/Relâmpago); Anéis 0x312-0x315 (Terra, Gelo, +HP, Esquiva); Armaduras do Comandante 0x709, Alma Sagrada 0x70c, Celestial 0x714, Bulls 0x719; Asas de Cazeaje 0x81a e Cérbero 0x81b; Luvas do Comandante 0x909, de Cristal 0x90f, Celestiais 0x914; Botas do Comandante 0xa09, Sapatos de Cristal 0xa0f, Botas Celestiais 0xa14.
- Pets Goldie 0xb01, CaoCao 0xb03, Ninko 0xb05.
- Cura: Cápsula de SP 0xd07, Bolo/Pizza/Carne/Suplemento 0xd15-18, curas de status 0xd1a-0xd1e (queimadura, congelamento, maldição, paralisia, antídoto). O tipo de loja 9 do código (todos os 0xd00..0xd1f) **não é usado por nenhum NPC**.
- Receitas 0xe2a (Runa Força), 0xe4b/e4c/e4d (HP M, HP G, Cura-Tudo).
- Temporários 0xf35/f36 (Coroas de Serdin/Canaban), 0xf3e/f3f/f40 (Medalhas de Cavaleiro/Lanceiro/Saber), "Item temporário".

Notas: muitos desses (Comandante, Celestial, Cristal) são peças de sets com pares presentes nas lojas, o que sugere **conteúdo cortado ou de evento/mercado**, não necessariamente online. Não encontrei código que os conceda; as chances de estarem em fluxo que a varredura não cobre são baixas, mas não nulas.

### 2.3 Medalhas, cupons, presentes
- **Cupom** [FATO]: código de 0x10 B + texto de 0x32 B vindos do servidor; só mostrado (1c000.c, substado 8) e **não tem efeito no jogo**; valia no Grand Chase PC (ui 0x41b24). Só saía das chaves de Ferro/Ouro (texto 0x421c4/0x422a0).
- **Medalha** [FATO]: apenas um contador no servidor (0x6c) e um aviso "[medalha]" ao abrir chave. Para que servia: menu.tx 17: eventos no site Netmarble Mobile. **Nada em jogo.**
- **Presentear** (menu.tx 36 e 0x12d/0x12f): sem chamador **[HIPÓTESE: cortado]**. O "Presentear" que existe no jogo (NPC, amizade, `gift_add_friendship` 0x2de88) é local.

### 2.4 NPC/diálogo "que fala de conectar à rede para liberar itens" [FATO parcial]
- **Não existe nenhum diálogo no cliente que mencione rede/servidor/conexão.** Varri todo o `catalog.json` (diálogos `dlg`, tabelas, UI, menus) no coreano e no PT com termos `네트워크, 서버, 접속, 온라인, 넷마블, 쿠폰, 메달, 랭킹, 통신, 무선`: as ocorrências são só menus/UI (lista acima) e nenhuma é fala de NPC.
- O único NPC ligado à rede é **Dennis (NPC 56)**, título "특별 상인" (Mercador Viajante), em 5 cidades (acima). Original (`n56.dat`):
  - fala 0 (`dlg|n56|0`): "희귀한 물건이 많이 있습니다!" -> PT final: "Tenho muitos itens raros!"
  - fala 1 (`dlg|n56|1`): "레어 아이템이 잔뜩 있답니다. 하나 골라보세요~" -> PT: "Tenho itens raros de sobra. Escolha um~"
  - menu (`npcmenu.tx` entrada 5): "네트워크접속@대화@나가기" -> PT: "Conectar à rede@Conversar@Sair".
  Ou seja, o "comentário sobre itens raros" é de Dennis, e o "desbloqueio" é a opção "Conectar à rede". **Sebas (NPC 64, "Mercador Viajante" também, mapa 141 Expedição) NÃO é de rede**: script tipo 14 (Desafiar/Comprar/Vender), vende bilhetes e doces.
- Se a sua lembrança é de um texto específico em PT, ele não está no catálogo atual (novo PT nem antigo): talvez seja o texto ui 0x41a10 (aviso "[AVISO] Este jogo é gratuito...", escrito por vocês, não original) ou a leitura do menu de Dennis. Vale confirmar com o usuário.

## 3. Estado técnico atual do HLE [FATO]
`runtime/hle.c` linhas 403-406, 436-438: `Connect` agenda um timer que chama o callback com `-1` (-> `net_error_code=1` "Sem acesso à rede"), `UnkSocket`, `SocketConnect/Write/Read/Close`, `SetRead/WriteCallback` devolvem `-1`. Convenção do timer: `callback(arg0, param)`; no `Connect` `arg0` = status (primeiro parâmetro de `net_on_bearer_connected`); o callback de `SocketConnect` (`net_on_socket_connected`) recebe o **status em `param_2`** (0 = sucesso). `GetSystemProperty` já devolve "0" para DS_LOCK/ROAMING_AREA/AIRPLANE_MODE.

## 4. Plano priorizado

### P1. "Servidor falso" no HLE de sockets (recomendado) - esforço ~1 dia, risco baixo-médio
Abordagem (não precisa de gancho no código do jogo, o fluxo original e as telas "Ligando.../Baixar..." continuam intactos):
1. `Connect`: callback com `arg0 = 0` (sucesso) em vez de `-1` (manter o comportamento antigo atrás de uma variável de ambiente, p.ex. `GC1_NONET=1`).
2. `UnkSocket` -> devolve um descritor fixo (ex.: 5). `SocketConnect(sock, ip, porta, cb=r3, 0)`: `timer_add(0, cb, param=0, arg0=sock, now+1)` e `return 0` (status 0 em `param_2` -> `net_connected=1` -> `net_send_current_request`).
3. `SocketWrite(sock, ptr, len)`: copiar os bytes do convidado, ler `[u16 tam][u16 id]`; quando `len >= tam+4`, montar a resposta (passo 5) num buffer do host e `return len`.
4. `SocketRead(sock, ptr, n)`: se há resposta pendente, copiar `min(n, resp_len)` para o convidado e devolver esse valor (a resposta tem de chegar inteira na primeira leitura: `net_handle_response` usa `net_io_remaining == 2000 - bytes` para detectar o primeiro pedaço); sem resposta devolver `-0x13`. `SocketClose`, `NetClose`, `SetReadCallback`, `SetWriteCallback`: devolver 0.
5. Resposta por id (little-endian, `tam` = 1 + corpo):
   - **0x65 -> 0x66**: status 0, corpo 0x45 B zerados (sem medalha, sem cupom). O modo está no pedido em `req[0x10]`: modos 1/5/10/14 = reviver, 2/3/4/6/7/8 = chave Enferrujada/Ferro/Ouro. Opcional: cobrar GP aqui (`player_gp` em 0x162c1a0): se `player_gp < custo`, responder status 1 + `[s16 len]"GP insuficiente"` (o jogo mostra o texto) e não debitar; caso contrário debitar no host (`ST32`) e responder ok.
   - **0x69 -> 0x6a**: contagem 0 (lista de cupons vazia). **0x6b -> 0x6c**: `[0]` = 0 medalhas (ou contador local se quiser). **0xc9 -> 0xca**: `rank=1`, `score` = o valor que o cliente mandou, 0x80 B zeros (ou ranking local das 3 fichas: opcional).
   - Qualquer outro id: status 1 + texto "Servico online encerrado." (cabe em `net_error_code=4`).
6. Teste: o harness `tools/playtest` (não rodei) deve forçar morte sem pergaminho -> Sim, e abrir um baú (0xf41 -> `chest_choice` 0..2) e checar `last_loot_ids` (0x162ca8c), `items_page_substate == 8` e `net_request_pending == 0`. Cuidado: snapshots (`auto.snap`/`estado.snap`) não devem ser tirados com uma resposta pendente (só existe entre Write e Read, no mesmo tick).
Alternativa pior: gancho em `net_send_request` (0x213ec) + `net_set_service_mode` (0x21444; o modo só é gravado depois de `net_send_request` retornar nos dois chamadores) chamando `finish_chest_open`/`revive_player_in_place` direto e zerando `net_request_pending`: mais frágil (precisa replicar o estado que `net_request_done_callback`/`net_close_connection` limpam).

### P2. Textos e preço (dados) - ~0,5 dia, risco baixo
- Trocar os ui 0x41830, 0x420f8, 0x421c4, 0x422a0 (pergaminho e 3 chaves) para não falar de "won reais/conexão/demora" (cabe: os slots originais em coreano são longos); remover o aviso 0x438c8/ui 0x41b24 (cupons) se Dennis deixar de ser rede.
- Decisão de economia no §5: grátis, GP, ou chave por drop.

### P3. Dennis vira loja local (só dados) - ~0,5 a 1 dia, risco médio
- `n56.scp`: byte do `op 0x25` (offset 16 do arquivo, valor 0x09) -> **2** (mesmo fluxo de Marsha: Comprar/Venda/Conversar/Presentear/Sair). O registro de NPC 56 em ConstData (base 0x5572 + 56*0x27) já tem tipo de loja 0x0a: preencher os 9 slots `u16` (offset +8) com ids de **cura/util** (a UI de suprimentos só mostra categorias 0xd/0xe; equip/pet/runa não aparecem nessa UI). Sugestão: 0xe4e (Pergaminho), 0xe05-e09 (5 bilhetes), 0xe50/e53/e54 (Pacotes Supremo/Cazeaje/Cérbero). **Os preços desses itens são 0 no registro (campo u32 em +10 do util)**: tem de fixar preços em ConstData (edição binária no `build.py`, que hoje só reescreve o blob de texto).
- Armas topo de linha (0x417/0x517/0x617, Felis, Kamiki): mais simples liberá-las pelas lojas de armas existentes (registros tipo 3 têm 3 ids de "liberação por amizade" `u16` em +32..+37 que usam `(cat_base + aba)`), também só dados.
- Efeito colateral bom: o modo 6 (mercado/correio) vira inalcançável (a única entrada era `script_choice_menu_type == 9`).
- Rosto/anéis/colares: dados apenas (`ConstData` `mon`, 12 slots de drop por monstro; slots 2..9 têm 0,3% a 0,8%): adicionar a poucos monstros ou ao baú. Sem UI de loja para categorias 1 e 3.

### P4. Esconder/neutralizar o menu Comunidade - ~0,25 dia, risco baixo
Com P1, "Registrar ranking/Cupons/Medalhas" passam a mostrar "Serviço online encerrado" (ou lista vazia/0 medalhas), sem código no jogo. Remover o item do menu exige mexer no mapeamento de cursor (menu.tx entrada 0 tem 5 opções cujas páginas são fixas no código): só vale com gancho em patch.c.

### P5. O que NÃO vale a pena
- Reimplementar mercado/correio/ID/avisos como servidor local de um jogador (20 tipos de pacote, formatos de 0x28/0xc/0x10 B por categoria): custo alto, valor baixo num jogo solo. Se quiser algo, o P3 já dá o "rare merchant".
- Cupons: sem equivalente local; a opção honesta é mostrar o texto "indisponível". (Se o usuário quiser, `finish_chest_open` poderia exibir o cupom fabricado, mas não serve para nada.)

## 5. Decisões que o usuário precisa tomar
1. **Chaves e pergaminho: grátis, em GP ou limitado?** (grátis = baú sempre dá 3 prêmios da faixa de ouro; em GP = definir custos, ex.: 5.000/9.000/13.000; ou impedir a Enferrujada/Ferro e deixar uma só). Isso define se os 9 equipamentos topo de linha e os sets entram "de graça" via drop.
2. **Reviver no local** depois dos 5 pergaminhos: grátis infinito, custo em GP, ou vender o pergaminho no Marsha/Dennis?
3. **Dennis**: virar loja local (e o que vende; preços) ou só mostrar "indisponível"?
4. **Itens sem fonte local (63)**: ignorar, distribuir por drop raro/lojas ou deixar como conteúdo cortado? Especialmente os 20 itens de rosto (cosméticos) e peças de set Comandante/Celestial/Cristal.
5. **Menu Comunidade e texto de cupons/boot**: manter com "indisponível", ou remover/adaptar os textos que prometem cupons Netmarble?
6. **Aprovar o servidor falso no HLE** (afeta PC e Android igualmente; `GC1_NONET=1` volta ao estado atual).
