"""Normalizacoes finais de termos (aplicadas por build.py sobre todas as traducoes) e filtro de strings de UI."""
import re
NORM = [
    (r'\bCaco de Jarro\b', 'Fragmento de Jarro'), (r'\bPedaco de Jarro\b', 'Fragmento de Jarro'), (r'\bCaco de Vaso\b', 'Fragmento de Jarro'),
    (r'\bSabreira\b', 'Saber'), (r'\bSalvadora\b', 'Saber'), (r'\bSalvador\b', 'Saber'),
    (r'\bOak\b', 'Orc'),
    (r'\b(Feiticeiros|Bruxos|Magos Sombrios) Negros?\b', 'Magos Negros'), (r'\b(Feiticeiro|Bruxo) Negro\b', 'Mago Negro'), (r'\bMago Sombrio\b', 'Mago Negro'),
    (r'\bPonte do Inferno\b', 'Ponte Infernal'), (r'\bLuna Lutador\b', 'Lunatic'), (r'\bAgua Cristalina\b', 'Agua de Nascente'),
    (r'\bSheets\b', 'Sids'), (r'\bSheet\b', 'Sid'), (r'\bPenas Quentes\b', 'Penas Quentinhas'), (r'\bPena Quente\b', 'Pena Quentinha'),
    (r'\bPantano dos Mortos\b', 'Pantano do Esquecimento'), (r'\bCapacetes Velhos\b', 'Elmos Velhos'), (r'\bCapacete Velho\b', 'Elmo Velho'),
    (r'\bCerberus\b', 'Cerbero'), (r'\bMachadinho de Arremesso\b', 'Machadinha de Arremesso'),
]


def norm(t):
    for p, r in NORM:
        t = re.sub(p, r, t)
    return t


def is_text(ko):
    """True se a string do .text decodificada como cp949 parece texto (e nao bytes de tabela)."""
    if '�' in ko:
        return False
    for c in ko:
        o = ord(c)
        if c in '\r\n':
            continue
        if o < 0x20:
            return False
        if o < 0x7f:
            continue
        if 0xAC00 <= o <= 0xD7A3 or 0x3130 <= o <= 0x318F or c in '※·…○●◆▶▲▼★☆♥♡':
            continue
        return False
    return True


def ui_ok(c):
    return is_text(c['ko']) and c['addr'] < 0x44180
