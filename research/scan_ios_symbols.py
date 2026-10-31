"""List Mach-O symbols in the iOS main binary that mention seed/fingerprint/dispatch."""
import struct
import zipfile
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Genshin-Bilibili-iOS'))
from local_config import local_path

IPA = local_path('source_ipa_relative', 'inputs/original.ipa')
MEMBER = 'Payload/Yuanshen.app/Yuanshen'


def parse(data):
    magic, cpu, sub, filetype, ncmds, sizeofcmds = struct.unpack_from('<IiiIII', data, 0)
    assert magic == 0xfeedfacf
    pos = 32
    symtab = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, pos)
        if cmd == 0x2:  # LC_SYMTAB
            symoff, nsyms, stroff, strsize = struct.unpack_from('<IIII', data, pos + 8)
            symtab = (symoff, nsyms, stroff, strsize)
        pos += size
    return symtab


with zipfile.ZipFile(IPA) as z:
    data = z.read(MEMBER)
print('binary bytes', len(data))
symoff, nsyms, stroff, strsize = parse(data)
print('symtab', symoff, nsyms, stroff, strsize)
strtab = data[stroff:stroff + strsize]
hits = []
for index in range(nsyms):
    base = symoff + index * 16
    name_offset, ntype, sect, desc, value = struct.unpack_from('<IBBHQ', data, base)
    if name_offset >= len(strtab):
        continue
    end = strtab.find(b'\0', name_offset)
    name = strtab[name_offset:end].decode('latin1', 'replace')
    low = name.lower()
    if any(token in low for token in ('fingerprint', 'createrandom', 'devicefp', 'seed_id', 'seed_time', 'dispatchseed')):
        hits.append((value, name))
print('hits', len(hits))
for value, name in hits[:60]:
    print(hex(value), name[:140])
