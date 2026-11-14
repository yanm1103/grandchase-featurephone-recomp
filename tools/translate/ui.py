"""Strings de UI na rodata do binary.mod (dentro do .text). Cada string = NUL-terminada; o slot vai ate o proximo inicio de string
(o compilador alinha em 4 e preenche com zeros). Substituir exige caber no slot, pois ponteiros no codigo apontam para o inicio."""
from elftools.elf.elffile import ELFFile
import re
TEXT_BASE=0x1000; LO=0x414c8
def load_text(path):
    with open(path,'rb') as f:
        e=ELFFile(f); s=e.get_section_by_name('.text'); return bytearray(s.data()), s['sh_offset']
def slots(text):
    """-> lista de (addr, bytes_da_string, capacidade) para cada string que comeca em LO..HI. Strings separadas por NUL; slot = ate o proximo inicio nao-NUL."""
    out=[]; a=LO; HI=TEXT_BASE+len(text)
    while a<HI:
        b=text[a-TEXT_BASE]
        if b==0: a+=1; continue
        e=a
        while e<HI and text[e-TEXT_BASE]!=0: e+=1
        n=e
        while n<HI and text[n-TEXT_BASE]==0: n+=1
        out.append((a,bytes(text[a-TEXT_BASE:e-TEXT_BASE]),n-a))   # capacidade = string + NULs ate a proxima (inclui o NUL final)
        a=n
    return out
