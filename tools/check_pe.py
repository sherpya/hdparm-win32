#!/usr/bin/env python3
"""Inspect imports/subsystem without executing a PE. No third-party modules."""
import argparse
import json
import pathlib
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('image', type=pathlib.Path)
p.add_argument('--legacy', action='store_true', help='check x86 subsystem and import regression baseline')
a = p.parse_args()
b = a.image.read_bytes()
u16 = lambda off: struct.unpack_from('<H', b, off)[0]
u32 = lambda off: struct.unpack_from('<I', b, off)[0]
pe = u32(0x3c)
assert b[:2] == b'MZ' and b[pe:pe+4] == b'PE\0\0'
opt = pe + 24
bits = 32 if u16(opt) == 0x10b else 64
assert u16(opt) in (0x10b, 0x20b)
sections = []
for i in range(u16(pe+6)):
    o = opt + u16(pe+20) + 40*i
    sections.append((u32(o+12), max(u32(o+8), u32(o+16)), u32(o+20)))
def offset(rva):
    for va, size, raw in sections:
        if va <= rva < va+size:
            return raw+rva-va
    raise ValueError(f'unmapped RVA {rva:x}')
def string(rva):
    o = offset(rva)
    return b[o:b.index(0, o)].decode('ascii')
imports = {}
directory = opt + (96 if bits == 32 else 112)
entry = offset(u32(directory+8))
while u32(entry+12):
    dll = string(u32(entry+12)).lower()
    thunk = offset(u32(entry) or u32(entry+16))
    names = []
    while True:
        v = struct.unpack_from('<I' if bits == 32 else '<Q', b, thunk)[0]
        if not v:
            break
        assert not (v >> (bits-1)), 'ordinal imports need manual review'
        names.append(string(v+2))
        thunk += bits//8
    imports[dll] = names
    entry += 20
print(f'{a.image}: PE{bits}, subsystem {u16(opt+48)}.{u16(opt+50)}, imports:')
for dll, names in imports.items():
    print(f'  {dll}: {", ".join(names)}')
if a.legacy:
    assert bits == 32 and u16(pe+4) == 0x14c
    assert (u16(opt+48), u16(opt+50)) <= (4, 0)
    baseline = json.loads(pathlib.Path(__file__).with_name('legacy-imports.json').read_text())
    for dll, names in imports.items():
        unknown = set(names) - set(baseline.get(dll, []))
        assert not unknown, f'unreviewed imports in {dll}: {sorted(unknown)}'
    assert u32(directory+13*8) == 0, 'review delay imports too'
    print('Legacy import regression check passed; actual OS loader tests are still required.')
