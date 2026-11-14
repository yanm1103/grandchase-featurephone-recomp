"""Disassembler minimo do bytecode n%d.scp / m%d.scp do Grand Chase 1 (so o que precisa para achar as referencias de texto).
Tamanhos de operando tirados de script_vm_opcode_loop (0x33096) na decompilacao.  Texto: .dat (EUC-KR no KO, ASCII no PT)."""
import struct
SIZES = {0:5,1:5,2:5,3:5,4:5,5:5,6:5,7:5,8:5,9:4,0xa:1,0xb:1,0xc:8,0xd:6,0xe:7,0xf:7,0x10:6,0x11:10,0x12:6,0x13:2,0x14:2,
         0x15:2,0x16:1,0x17:3,0x18:1,0x19:1,0x1a:6,0x1b:17,0x1c:3,0x1d:2,0x1e:2,0x1f:5,0x20:5,0x21:7,0x22:6,0x23:5,0x24:1,0x25:2,0x26:4}
def walk(code):
    """-> lista de (pc, op, bytes). Levanta ValueError se sair do trilho."""
    out = []; pc = 0
    while pc < len(code):
        op = code[pc]
        if op not in SIZES: raise ValueError(f'opcode {op:#x} desconhecido em {pc}')
        n = SIZES[op]
        if pc + n > len(code): raise ValueError(f'opcode {op:#x} corta o arquivo em {pc}')
        out.append((pc, op, code[pc:pc+n])); pc += n
    return out
def text_refs(code):
    """-> lista de dicts {pc, op, off, len, field_off (posicao do s16 offset no scp), field_len}"""
    refs = []
    for pc, op, b in walk(code):
        if op == 0x11:   # u32 ator, u8 expr, s16 off, s16 len
            off, ln = struct.unpack_from('<hh', b, 6); refs.append(dict(pc=pc, op=op, off=off, len=ln, fo=pc+6, fl=pc+8, actor=struct.unpack_from('<I', b, 1)[0], expr=b[5]))
        elif op == 0x1a: # s16 off, s16 len, u8 modo
            off, ln = struct.unpack_from('<hh', b, 1); refs.append(dict(pc=pc, op=op, off=off, len=ln, fo=pc+1, fl=pc+3, mode=b[5]))
    return refs
