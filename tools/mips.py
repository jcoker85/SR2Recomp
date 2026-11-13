#!/usr/bin/env python3
"""Tiny R5900 helper for tracing the game's program: find references to an address and disassemble a range.

    python mips.py refs <elf> <hex address>          lui/addiu|ori|lw|sw... pairs that build the address
    python mips.py dis <elf> <hex start> <count>     a plain listing (common opcodes only)
"""
import struct, sys

R = ['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7',
     's0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
OPS = {0x08:'addi',0x09:'addiu',0x0a:'slti',0x0b:'sltiu',0x0c:'andi',0x0d:'ori',0x0e:'xori',0x0f:'lui',
       0x20:'lb',0x21:'lh',0x23:'lw',0x24:'lbu',0x25:'lhu',0x28:'sb',0x29:'sh',0x2b:'sw',0x37:'ld',0x3f:'sd',
       0x31:'lwc1',0x39:'swc1',0x1e:'lq',0x1f:'sq',0x19:'daddiu',0x04:'beq',0x05:'bne',0x06:'blez',0x07:'bgtz',
       0x14:'beql',0x15:'bnel',0x02:'j',0x03:'jal'}
FN = {0x00:'sll',0x02:'srl',0x03:'sra',0x08:'jr',0x09:'jalr',0x21:'addu',0x23:'subu',0x24:'and',0x25:'or',
      0x26:'xor',0x27:'nor',0x2a:'slt',0x2b:'sltu',0x2d:'daddu',0x0a:'movz',0x0b:'movn',0x18:'mult',0x1a:'div'}

def load(path):
    d = open(path, 'rb').read()
    if d[:4] != b'\x7fELF':  # a raw memory image (an IOP RAM dump, say) that starts at address 0
        return [(0, d)]
    phoff, = struct.unpack_from('<I', d, 0x1c); n, = struct.unpack_from('<H', d, 0x2c)
    segs = []
    for i in range(n):
        t, off, va, pa, fs, ms = struct.unpack_from('<6I', d, phoff + i * 32)
        if t == 1 and fs: segs.append((va, d[off:off + fs]))
    return segs

def word(segs, a):
    for va, b in segs:
        if va <= a < va + len(b) - 3: return struct.unpack_from('<I', b, a - va)[0]
    return None

def dis(w, a):
    op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; sa = (w >> 6) & 31
    imm = w & 0xffff; simm = imm - 0x10000 if imm & 0x8000 else imm
    if w == 0: return 'nop'
    if op == 0:
        f = w & 63; n = FN.get(f, 'special.%02x' % f)
        if f in (0, 2, 3): return f'{n} {R[rd]},{R[rt]},{sa}'
        if f == 8: return f'jr {R[rs]}'
        if f == 9: return f'jalr {R[rd]},{R[rs]}'
        return f'{n} {R[rd]},{R[rs]},{R[rt]}'
    if op in (2, 3): return f'{OPS[op]} 0x{((a + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2):08x}'
    if op == 1: return f'regimm.{rt} {R[rs]},0x{a + 4 + simm * 4:08x}'
    n = OPS.get(op)
    if not n: return f'.word 0x{w:08x}'
    if op == 0x0f: return f'lui {R[rt]},0x{imm:04x}'
    if op in (4, 5, 0x14, 0x15): return f'{n} {R[rs]},{R[rt]},0x{a + 4 + simm * 4:08x}'
    if op in (6, 7): return f'{n} {R[rs]},0x{a + 4 + simm * 4:08x}'
    if op >= 0x19 and op not in (0x0c, 0x0d, 0x0e): return f'{n} {R[rt] if op not in (0x31,0x39) else "f%d" % rt},{simm}({R[rs]})'
    if op in (0x0c, 0x0d, 0x0e): return f'{n} {R[rt]},{R[rs]},0x{imm:x}'
    return f'{n} {R[rt]},{R[rs]},{simm}'

def refs(segs, target):
    hi = (target + 0x8000) >> 16 & 0xffff; lo = target & 0xffff
    for va, b in segs:
        for i in range(0, len(b) - 3, 4):
            w = struct.unpack_from('<I', b, i)[0]
            if w >> 26 == 0x0f and w & 0xffff == hi:
                reg = (w >> 16) & 31
                for j in range(1, 24):
                    if i + j * 4 + 3 >= len(b): break
                    w2 = struct.unpack_from('<I', b, i + j * 4)[0]
                    if (w2 >> 21) & 31 == reg and w2 & 0xffff == lo and (w2 >> 26) != 0x0f:
                        print(f'{va + i:08x} .. {va + i + j * 4:08x}: {dis(w2, va + i + j * 4)}')
                        break
            if w >> 26 == 0x0d and w & 0xffff == (target & 0xffff):  # ori form
                pass

if __name__ == '__main__':
    segs = load(sys.argv[2])
    if sys.argv[1] == 'refs':
        refs(segs, int(sys.argv[3], 16))
    else:
        a = int(sys.argv[3], 16)
        for k in range(int(sys.argv[4])):
            w = word(segs, a + k * 4)
            print(f'{a + k * 4:08x}: {w:08x}  {dis(w, a + k * 4)}')
