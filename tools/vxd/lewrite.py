#!/usr/bin/env python3
# lewrite.py - generic Windows 9x LE (VxD) writer + "repack" self-test.
#   lewrite.py repack <in.vxd> <out.vxd> [opts]   parse a stock VxD, re-emit it
# Used to bisect which layout property Win95's VxD loader depends on.
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

def write_le(dos, objects, resnames, entry, fpt_and_frt, nonres, winres, devid, ddkver,
             mflags=0x38000, pagesz=0x1000, data_align=0x200, npreload=None, pages=None):
    """objects: list of (bytes, flags, cls4). Each object occupies ceil(len/pagesz)
    pages (min 1). fpt_and_frt: (list_of_per_page_record_bytes) -> builds tables."""
    LE = len(dos); HDRSZ = 0xc4
    # pages
    plist = []          # (bytes)
    objtab = b''
    for data, flags, cls in objects:
        n = max(1, (len(data) + pagesz - 1) // pagesz)
        first = len(plist) + 1
        for k in range(n):
            plist.append(data[k*pagesz:(k+1)*pagesz])
        objtab += struct.pack('<IIIII', len(data), 0, flags, first, n) + cls
    npages = len(plist)
    pagemap = b''.join(bytes([0, 0, i, 0]) for i in range(1, npages + 1))
    recs = fpt_and_frt
    assert len(recs) == npages
    fpt = b''; frt = b''
    for r in recs:
        fpt += struct.pack('<I', len(frt)); frt += r
    fpt += struct.pack('<I', len(frt))
    imptab = b''
    o_objtab = HDRSZ
    o_pagemap = o_objtab + len(objtab)
    o_res = o_pagemap + len(pagemap)
    o_ent = o_res + len(resnames)
    o_fpt = o_ent + len(entry)
    o_frt = o_fpt + len(fpt)
    o_imp = o_frt + len(frt)
    loader = objtab + pagemap + resnames + entry + fpt + frt + imptab
    buf = bytearray(dos) + bytearray(HDRSZ) + loader
    dpo = (len(buf) + data_align - 1) // data_align * data_align
    buf += b'\0' * (dpo - len(buf))
    for i, p in enumerate(plist):
        buf += p if i == npages - 1 else p.ljust(pagesz, b'\0')
    last = len(plist[-1])
    nro = len(buf); buf += nonres
    wro = 0
    if winres: wro = len(buf); buf += winres
    h = bytearray(HDRSZ)
    def W(o, v, s=4): struct.pack_into('<I' if s == 4 else '<H', h, o, v)
    h[0:2] = b'LE'; W(0x08, 2, 2); W(0x0a, 4, 2)
    W(0x10, mflags); W(0x14, npages); W(0x28, pagesz); W(0x2c, last)
    W(0x30, o_imp - o_fpt); W(0x38, o_fpt - o_objtab)
    W(0x40, o_objtab); W(0x44, len(objects)); W(0x48, o_pagemap)
    W(0x58, o_res); W(0x5c, o_ent); W(0x68, o_fpt); W(0x6c, o_frt)
    W(0x70, o_imp); W(0x78, o_imp)
    W(0x80, dpo)
    W(0x84, npreload if npreload is not None else
          sum(max(1, (len(d)+pagesz-1)//pagesz) for d, f, c in objects if f & 0x40))
    W(0x88, nro); W(0x8c, len(nonres))
    W(0xb8, wro); W(0xbc, len(winres)); W(0xc0, devid, 2); W(0xc2, ddkver, 2)
    buf[LE:LE+HDRSZ] = h
    return bytes(buf)
