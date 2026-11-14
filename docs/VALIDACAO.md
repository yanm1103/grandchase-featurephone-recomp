# Validação do recomp — o que foi provado e o que não foi

Tudo abaixo foi executado em 2026-11-13 contra o `binary.mod` PT (`.text` FNV-64 gravado em `gen/tables.c`).

## 1. Instruções (prova forte)
`tools/lift/test_lifter.py 24`: cada encoding distinto do binário (e cada instância PC-relativa e de desvio) × 24 estados aleatórios
(registradores, flags, memória), executado no Unicorn e no C lifted. **458.412 casos, 0 divergências** em 49 mnemônicos
(r0–r14, N/Z/C/V, próximo PC com bit de modo, CRC da memória de teste). Vale para *todo* o código, executado ou não.
Ressalva: o Unicorn usa o modelo de CPU padrão, não o ARM926; acesso desalinhado não foi validado.

## 2. Estrutura (CFG)
22.858 blocos; 26/26 tabelas de `switch` resolvidas; 0 saltos indiretos sem resolver; 150 stubs de import achados (65 em `.data`, 85 em `.text`);
todos os corpos de função do Ghidra cobertos (sobram 34 trechos de 2 bytes de padding). 4 blocos têm instrução não suportada
(`cdp2`, `cbnz`, `svc`, `rev16`): parecem dados decodificados por um desvio espúrio e nunca foram executados (não verifiquei um a um).

## 3. Execução (igualdade de traces com o Unicorn)
Backend A = blocos lifted, backend B = Unicorn, **mesmo HLE e mesma memória**. Trace = cada chamada de API (args, retorno), eventos e o CRC do
framebuffer a cada `FlushLcd`.
- Roteiro de 700 quadros (avisos → logo → título): traces idênticos (114.529 linhas).
- 6 sessões aleatórias × 3.000 quadros (cerca de 30 milhões de linhas de trace): **idênticas**, inclusive jogo no Palácio Serdin, menu de ajustes e tela de atalhos.

## 4. Cobertura (o limite honesto do item 3)
O fuzz aleatório de 40 sessões longas executou **6.278 de 22.858 blocos (27,5%)**, com 0 erros fatais, e praticamente satura nesse valor.
A igualdade de traces só cobre esses blocos. Falta exercitar: lojas, ferreiro, depósito, missões, fim de jogo, salvar/carregar.
Para isso: jogar com `gc1.exe --record roteiro.txt ...` e rodar `tools/difftest/replay.py <jar> roteiro.txt`
(compara os dois backends, os saves e a cobertura).

## 5. O que NÃO é validado por nada acima
- **Semântica do HLE** (tempo, framebuffer, arquivos, `sprintf`): compartilhada pelos dois backends, então um erro aí não aparece. Vem da leitura do WIE e de jogar.
- **Áudio**: a música usa WAVs aproximados (GM + GeneralUser GS, sem a camada FM); os efeitos são exatos. Não ouvi nenhum.
- **Janela SDL**: só teste de fumaça (abre e fica rodando); não verifiquei a janela visualmente nem a entrada de gamepad.
- Rede: o servidor original não existe; `Connect` falha como no WIE.
