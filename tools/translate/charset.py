"""Letras acentuadas no jogo: a fonte so tem 0-9 A-Z a-z, entao 24 letras acentuadas usam os bytes 0x80..0x97 (passam intactos por text_to_utf16);
runtime/patch.c desenha esses codigos (a ordem TEM de ser a mesma da tabela ACC de la)."""
ACC = 'áéíóúâêôãõàçÁÉÍÓÚÂÊÔÃÕÀÇ'
TO_BYTE = {c: 0x80 + i for i, c in enumerate(ACC)}
FROM_BYTE = {b: c for c, b in TO_BYTE.items()}
BASE = dict(zip(ACC, 'aeiouaeoaoacAEIOUAEOAOAC'))


def enc(s):
    return bytes(TO_BYTE[c] if c in TO_BYTE else ord(c) for c in s) if all(c in TO_BYTE or ord(c) < 0x80 for c in s) else s.encode('ascii')


def dec(b):
    return ''.join(FROM_BYTE.get(x, chr(x)) for x in b)


def strip(s):
    """tira os acentos (volta ao texto ASCII original)."""
    return ''.join(BASE.get(c, c) for c in s)
