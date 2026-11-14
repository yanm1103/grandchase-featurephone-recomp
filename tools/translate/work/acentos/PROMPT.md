Voce vai ACENTUAR textos em portugues do Brasil de um jogo. Cada arquivo de entrada e um JSON {id: texto}; devolva um JSON com os MESMOS ids e o MESMO texto, so com os acentos que faltam.

Regras (rigidas, um validador confere):
- Tirando os acentos, o texto tem de voltar IDENTICO ao original (mesmas letras, espacos, pontuacao, maiusculas, "|", "/", "@1".."@9", "%d", "%s", "$"). Nao corrija, reescreva nem traduza nada.
- Letras permitidas com acento (so estas 24): á é í ó ú â ê ô ã õ à ç  e as maiusculas Á É Í Ó Ú Â Ê Ô Ã Õ À Ç. NAO use ü, ñ, nem outros (se uma palavra pediria um deles, deixe sem).
- Acentue como o portugues correto: "voce"->"você", "nao"->"não", "e" (verbo)->"é" mas "e" (conjuncao) fica "e", "esta"->"está" so quando for verbo, "ha"->"há", "ate"->"até", "tambem"->"também", "sao"->"são", "mae"->"mãe", "ataque"/"para" etc. ficam como estao.
- Nomes proprios do jogo (Elesis, Lire, Arme, Ronan, Gaicoz, Cazeaje...) NAO levam acento; nomes de itens/monstros/lugares em portugues levam normalmente ("Poção de Cura", "Floresta dos Desafios").
- Na duvida entre duas leituras, escolha a que faz sentido no contexto do jogo (RPG de acao medieval/fantasia).
Saida: apenas o JSON, mesma ordem de chaves, sem comentarios.
