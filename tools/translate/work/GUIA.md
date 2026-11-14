# Guia de traducao: Grand Chase 1 (celular, LG U+) coreano -> portugues do Brasil

Voce vai traduzir textos do jogo mobile **Grand Chase 1** (RPG de acao; protagonista **Elesis**, cavaleira da Ordem dos Cavaleiros Vermelhos de Serdin, que
procura o pai desaparecido em Canaban) do **coreano** para **portugues do Brasil**. A traducao antiga existente e ruim (traduzida palavra por palavra,
com nomes proprios traduzidos como se fossem substantivos, genero errado, palavroes que nao existem no original). **Traduza do coreano**; o campo `old_pt`
(quando existir) e so referencia de comprimento/estrutura e frequentemente esta ERRADO: nao copie.

## Formato
Voce recebe um arquivo JSON com a lista `units`. Cada unidade tem `id`, `kind`, `ko` (texto original) e contexto. Voce grava **um JSON** `{ "<id>": "<texto em PT>" , ... }`
(UTF-8, todas as unidades do lote, sem omitir nenhuma) no caminho de saida indicado. Depois rode o validador e corrija o que ele apontar:
`python -I tools/translate/validate.py <arquivo_de_saida> <arquivo_do_lote>`

## Regras tecnicas (o validador confere)
1. **So ASCII**: sem acentos, sem cedilha, sem aspas curvas, sem reticencias unicode. Escreva "voce", "nao", "e" (para e/é), "coracao", "ate", "ja". Use `...` para reticencias.
   (A fonte do jogo nao desenha acento; o BR oficial do jogo online da epoca tambem era sem acento.)
2. **Limite em bytes** (campo `limit`): o texto traduzido deve ter no maximo `limit` caracteres. Rotulos de UI pequenos pedem abreviacao ("Pet", "Sair", "Voltar", "PV", "ATQ"). Se nao couber, abrevie ou resuma, nunca estoure.
3. **Preserve marcadores**: `%d`, `%s`, `%02d` etc. (mesma quantidade e ordem relativa), codigos de cor `@1`..`@9`, e `\\` literal. `|` e quebra de linha manual em textos de UI/menu: mantenha-os onde o original os tem (pode mover para quebrar bem).
   Em unidades `dlg` NAO use `|` nem `/` (o programa insere as quebras de linha sozinho, por palavras); em unidades `menu`, nao use `/` nem `@` ou `$` fora da estrutura do original.
4. Largura aproximada onde o texto aparece: caixa de dialogo ~30 caracteres por linha, 2 linhas por pagina; descricoes ~24 caracteres por linha; rotulos de menu 1 linha.
   Frases de dialogo podem ser longas (paginam), mas seja natural e **nao mais longo que o necessario**.
5. Unidades `kind=menu` (menu.tx): a linha inteira tem estrutura `Titulo/corpo$` ou `Opcao1@Opcao2@...@$`. Mantenha exatamente a mesma estrutura (mesmo numero de `@`, o `$` final, a primeira `/` separando titulo e corpo; titulo ate ~28 chars, cada opcao ate ~28 chars).

## Estilo
- Portugues brasileiro natural, de jogo de aventura. Trate por "voce". Nada de "tu", "vos" arcaico, exceto quando o falante for de realeza ("Majestade", "Vossa Majestade" e ok).
- **Genero e concordancia seguem o FALANTE e o ALVO** (veja `speaker` e o glossario). Elesis, Lire, Arme, rainhas e outras mulheres falam e sao tratadas no feminino ("obrigada", "cansada", "ela"). Em duvida sobre o genero de um NPC (nome nao listado) e se o coreano nao indicar, reformule sem adjetivos/pronomes de genero.
- Elesis: jovem cavaleira, fria e sarcastica depois do sumico do pai, mas honrada e protetora dos fracos. Fala direta, sem exageros melosos. Quando ela se refere ao proprio pai: "meu pai"; quando outros falam dele: "seu pai".
- Quem fala? Em `dlg`, `speaker` diz quem fala e `scene` o local/NPC da cena; as linhas vem em ordem de cena: use as linhas vizinhas como contexto (quem responde a quem; quando "voce" e Elesis, etc.).
- Reproduza o tom: tratamento formal/informal do coreano (habnida vs. banmal) vira registro formal/informal em PT; interjeicoes ("흐음~" -> "Hmm~", "헉!" -> "Hã!?"/"Ei!") naturais; risadas "ㅋㅋ/하하" -> "Haha".
- **Nunca introduza palavroes** (merda, porra etc.) a menos que o coreano seja claramente vulgar. Xingamentos leves em coreano ("바보", "멍청이") -> "bobo", "idiota".
- Humor/trocadilhos: adapte o sentido; nao traduza literalmente o que ficaria sem sentido.
- Pontuacao do original (`..`, `...`, `~`, `!!`, `?!`) pode ser mantida.
- Numeros e simbolos do original mantidos (`50%`, `HP`, `SP`, `GP`, `EXP`, `Lv`).

## Nomes proprios: NAO traduza, transliterar (veja GLOSSARIO.md)
Nomes de pessoas/NPCs, lugares e monstros que sao nomes (Iron, Windy, Kamiki, Cazeaje...) ficam como nomes, nunca viram substantivo comum ("Iron" jamais vira "Ferro").
Para nomes de **monstros, itens, habilidades e lugares** do universo Grand Chase, consulte a tabela oficial do BR da Season 5 antes de inventar:
`grep -i "<termo>" tools/translate/work/str_br_s5.txt`  (formato `id<TAB>texto`; use a grafia dela, sem acento). Se o termo nao existe la, traduza pelo sentido (ex.: "Tumulo dos Mortos") ou transliterar o nome.
Seja **consistente**: o mesmo termo coreano deve ter sempre a mesma traducao em todos os lotes. O GLOSSARIO.md e a autoridade; no campo `"_novos_termos"` do seu JSON de saida
(opcional, objeto `{"coreano": "traducao"}`) liste termos novos que voce decidiu, para eu registrar.
