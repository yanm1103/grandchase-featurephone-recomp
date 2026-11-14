"""Tabelas de texto com registro fixo (.scp = indice, .dat = blob de texto sem separador).
SCHEMAS[nome] = (stride, base, [(pos_off_s16, pos_len, largura_len), ...]).  Esquemas validados por cobertura exata do .dat em KO e PT
(cada byte do .dat pertence a exatamente um campo)."""
import struct
SCHEMAS = {
    'tips':      (4, 0,  [(1, 3, 1)]),
    'scp_map':   (12, 0, [(2, 4, 1)]),
    'scp_mix':   (13, 0, [(2, 4, 1)]),
    'scp_npc':   (38, 0, [(1, 3, 1), (4, 6, 1)]),
    'scp_quest': (37, 0, [(4, 6, 1), (7, 9, 2)]),
}
def fields(name, scp, ndat):
    """-> lista de (indice_registro, indice_campo, pos_off, pos_len, largura, off, len) na ordem do arquivo"""
    S, base, fl = SCHEMAS[name]; out = []
    for r in range((len(scp) - base) // S):
        for fi, (po, pl, w) in enumerate(fl):
            o = base + r * S
            off = struct.unpack_from('<H', scp, o + po)[0]
            ln = scp[o + pl] if w == 1 else struct.unpack_from('<H', scp, o + pl)[0]
            out.append((r, fi, o + po, o + pl, w, off, ln))
    return out
