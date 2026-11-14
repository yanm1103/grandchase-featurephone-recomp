"""Loja do Dennis (NPC 56): no original a opcao "Conectar a rede" abria o mercado entre jogadores (servidor da Netmarble, que nao existe mais).
Aqui o script dele passa a abrir o menu de loja (tipo 2: Comprar/Vender/Conversar/Presentear/Sair, o mesmo da Marsha) e runtime/patch.c (hook_shop_rebuild) monta o estoque.
Este modulo faz a parte de dados: (1) n56.scp: operando do opcode 0x25 de 9 para 2; (2) precos do ConstData.br que estavam zerados nos itens que ele vende
(bilhetes e5-e9 e pacotes e4f-e54; as curas ja tem preco). Chamado por build.py no fim, depois de gravar os arquivos em OUT."""
import struct
UTIL_BASE, UTIL_STRIDE, PRICE_OFF = 0x1b64, 14, 10          # tabela util do ConstData: preco = u32 em +10 do registro
PRECOS = {0xe05: 25000, 0xe06: 30000, 0xe07: 40000, 0xe08: 50000, 0xe09: 60000,                     # bilhetes (Sebas vende e02-e04 por 10000/15000/20000)
          0xe4f: 30000, 0xe50: 50000, 0xe51: 70000, 0xe52: 90000, 0xe53: 110000, 0xe54: 130000}     # pacotes de armadura, do set mais fraco ao mais forte

def patch_binary(out):
    p = f'{out}/n56.scp'; s = bytearray(open(p, 'rb').read())
    assert s[15] == 0x25 and s[16] in (9, 2), 'n56.scp mudou: opcode 0x25 nao esta em 15/16'
    s[16] = 2; open(p, 'wb').write(s)
    p = f'{out}/ConstData.br'; d = bytearray(open(p, 'rb').read())
    for iid, preco in PRECOS.items():
        o = UTIL_BASE + (iid & 0xff) * UTIL_STRIDE
        assert struct.unpack_from('<H', d, o)[0] == iid, hex(iid)            # o registro comeca com o proprio id
        struct.pack_into('<I', d, o + PRICE_OFF, preco)
    open(p, 'wb').write(d)
    return len(PRECOS)
