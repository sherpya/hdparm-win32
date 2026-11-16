#!/usr/bin/env python3
"""Validate the LE container, DDB export and internal fixups of HDPARM.VXD."""
import pathlib
import struct
import sys
b = pathlib.Path(sys.argv[1]).read_bytes()
u16 = lambda o: struct.unpack_from('<H', b, o)[0]
u32 = lambda o: struct.unpack_from('<I', b, o)[0]
assert b[:2] == b'MZ'
h = u32(0x3c)
assert b[h:h+2] == b'LE' and b[h+2:h+4] == b'\0\0'
assert u16(h+8) == 2 and u16(h+10) == 4  # 386 / Windows
assert u32(h+0x10) == 0x38000 and u32(h+0x44) == 1
pages, page_size, last = u32(h+0x14), u32(h+0x28), u32(h+0x2c)
assert page_size == 4096 and 0 < last <= page_size
obj = h+u32(h+0x40)
size, base, flags, first, count = struct.unpack_from('<5I', b, obj)
assert base == 0 and flags == 0x2047 and first == 1 and count == pages
assert u32(h+0x84) == pages and u32(h+0x74) == 0
entry = h+u32(h+0x5c)
assert b[entry:entry+5] == b'\x01\x03\x01\x00\x03'
ddb = u32(entry+5)
assert b[entry+9] == 0 and ddb+80 <= size
mapping = h+u32(h+0x48)
start = u32(h+0x80)
image = bytearray()
for i in range(pages):
    raw = b[mapping+i*4:mapping+i*4+4]
    assert raw[3] == 0
    number = int.from_bytes(raw[:3], 'big')
    assert number == i+1
    off = start+i*page_size
    n = last if i == pages-1 else page_size
    assert off+n <= len(b)
    image.extend(b[off:off+n])
assert len(image) >= size
assert struct.unpack_from('<H',image,ddb+4)[0] == 0x400
assert image[ddb+12:ddb+20] == b'HDPARM  '
assert image[ddb+60:ddb+80] == b'verP\x50\0\0\x001vsR2vsR3vsR'
fpt, frt = h+u32(h+0x68), h+u32(h+0x6c)
fixups = {}
for i in range(pages):
    p, end = frt+u32(fpt+i*4), frt+u32(fpt+(i+1)*4)
    assert p <= end <= h+u32(h+0x70)
    while p < end:
        kind, flags, offset, target_object = struct.unpack_from('<BBhB', b, p)
        assert kind == 7 and flags in (0, 0x10) and target_object == 1
        target = u32(p+5) if flags else u16(p+5)
        assert 0 <= target < size
        source = i*4096+offset
        assert -3 <= offset < 4096 and 0 <= source <= size-4
        if source in fixups:
            assert fixups[source] == target
        fixups[source] = target
        p += 9 if flags else 7
    assert p == end
assert ddb+24 in fixups
assert image[fixups[ddb+24]] != 0
for source, target in fixups.items():
    struct.pack_into('<I', image, source, 0xc1000000+target)
assert 0xc1000000 <= struct.unpack_from('<I',image,ddb+24)[0] < 0xc1000000+size
print(f'{sys.argv[1]}: LE/DDB valid, {pages} locked/preloaded pages, {len(fixups)} internal relocations')
