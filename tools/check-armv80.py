#!/usr/bin/env python3
"""Look for ARMv8.1+ instructions the A30s's Cortex-A53/A73 (ARMv8.0) lack in an
arm64 Mach-O kernel: LSE atomics (CAS*, CASP*, LD<op>, ST<op>, SWP*) and
LDAPR* (RCpc). Scans every section marked as instructions.

    check-armv80.py kernel.development.sweet

topaz's README expects one cluster of hits: the SHA-256 K256 constant table,
data in a code section that happens to decode as atomics.
"""
import struct
import sys

LC_SEGMENT_64 = 0x19
S_ATTR_PURE_INSTRUCTIONS = 0x80000000
S_ATTR_SOME_INSTRUCTIONS = 0x00000400


def kind(insn):
    if (insn & 0x3F200C00) == 0x38200000:	# atomic memory ops, LDAPR (o3=1 opc=100)
        o3_opc = (insn >> 12) & 0xF
        return 'ldapr' if o3_opc == 0b1100 else 'lse-op'
    if (insn & 0x3FA07C00) == 0x08A07C00:	# CAS, CASA, CASL, CASAL (+B/H)
        return 'cas'
    if (insn & 0xBFA07C00) == 0x08207C00:	# CASP family
        return 'casp'
    return None


def sections(data):
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from('<8I', data, 0)
    if magic != 0xFEEDFACF:
        sys.exit('not a 64-bit Mach-O')
    off = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, off)
        if cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from('<I', data, off + 64)[0]
            s = off + 72
            for _ in range(nsects):
                sect, seg = (x.rstrip(b'\0').decode() for x in struct.unpack_from('<16s16s', data, s))
                addr, sz, foff = struct.unpack_from('<QQI', data, s + 32)
                flags = struct.unpack_from('<I', data, s + 64)[0]
                if flags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS) and foff:
                    yield f'{seg},{sect}', addr, data[foff:foff + sz]
                s += 80
        off += size


def main():
    data = open(sys.argv[1], 'rb').read()
    total = 0
    for name, addr, body in sections(data):
        hits = []
        for i in range(0, len(body) - 3, 4):
            k = kind(struct.unpack_from('<I', body, i)[0])
            if k:
                hits.append((addr + i, k))
        total += len(hits)
        print(f'{name}: {len(body)} bytes, {len(hits)} ARMv8.1+ encodings')
        # group neighbouring hits: a table shows up as one dense cluster
        groups, start, prev = [], None, None
        for a, _ in hits:
            if prev is None or a - prev > 64:
                if start is not None:
                    groups.append((start, prev))
                start = a
            prev = a
        if start is not None:
            groups.append((start, prev))
        for lo, hi in groups[:20]:
            n = sum(1 for a, _ in hits if lo <= a <= hi)
            kinds = sorted({k for a, k in hits if lo <= a <= hi})
            print(f'  {lo:#x}-{hi:#x}: {n} ({", ".join(kinds)})')
    print(f'total: {total}')


if __name__ == '__main__':
    main()
