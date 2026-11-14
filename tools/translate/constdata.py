"""ConstData.br: tabelas binarias de registro fixo + blob de textos (base 0x82d2) referenciado por (off s16, len u8).
TABS = (nome, base, stride, contagem, [(tipo, pos_off, pos_len)...]). Validado: o blob e coberto exatamente, sem sobreposicao, em KO e PT."""
import struct
BLOB = 0x82d2
TABS = [
    ('eq',    0,       22, 277, [('name', 2, 4)]),
    ('pet',   0x17ce,  17, 9,   [('name', 2, 4)]),
    ('rune',  0x1867,  11, 23,  [('name', 2, 4)]),
    ('heal',  0x1964,  16, 32,  [('name', 2, 4), ('desc', 5, 7)]),
    ('util',  0x1b64,  14, 85,  [('name', 2, 4), ('desc', 5, 7)]),
    ('misc',  0x200a,  13, 66,  [('name', 2, 4), ('desc', 5, 7)]),
    ('skill', 0x2364,  11, 60,  [('name', 5, 7), ('desc', 8, 10)]),
    ('mon',   24448,   66, 137, [('name', 2, 4)]),
    ('npc',   21875,   39, 66,  [('title', 1, 3), ('name', 4, 6)]),
]
def fields(d):
    """-> lista (tab, idx, kind, pos_off, pos_len, off, len) com len>0"""
    out = []
    for name, base, S, n, fl in TABS:
        for i in range(n):
            p = base + i * S
            for kind, fo, fl_ in fl:
                off = struct.unpack_from('<h', d, p + fo)[0]; ln = d[p + fl_]
                if ln: out.append((name, i, kind, p + fo, p + fl_, off, ln))
    return out
