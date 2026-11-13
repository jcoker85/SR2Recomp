"""Smuggler's Run 2: Hostile Territory (SLUS-20204): keeps the driving HUD in 4:3 shapes on a 16:9 picture, each part at
its own edge.

    python -I hud43.py        writes hud43.pnach (a PCSX2-style patch file, section [HUD 4:3])

The same method as SR1Recomp's sr1-options/hud43.py, with this game's addresses (found 2026-11-13; the game has no
symbols). The HUD's draw function is 0x1CA958 (called once a frame from 0x103F8C). It sets up the drawing and then calls
the same part loop Smuggler's Run has, here at 0x20CFA8: the HUD is a container object with its parts in an array (count
at +4, pointers from +28), and the loop calls each part's draw: `lw v0,116(a1); lh a0,32(v0); lw v1,36(v0); jalr ra,v1;
addu a0,a1,a0`, s1 = the part's place in the array. Nothing else goes through that loop (the menus have their own
drawing; probed in the menus, on the loading screen, while driving and in the pause menu), so the one call at 0x20CFEC is
hooked:
  * It becomes `jal` to a stub that takes the function from v1 and the part's mark from s1, and goes on to WRAP. While the
    HUD option word at 0x000FFF04 and the widescreen word at 0x000FFF00 are both 1 (the launcher's boot.txt sets the
    latter with widescreen on; with Menu/Movie Aspect Ratio the runtime keeps it at 1 only while the picture is 16:9), WRAP
    adds one qword to the game's VIF1 packet chain before the part draws and one after: DMA tag cnt/0, VIF FLUSH, VIF MARK
    value. The toolkit's runtime (gif.cpp, hud_mark) squeezes the x of everything the part draws on PATH1/PATH2 while the
    mark is on, toward the part's edge, so the 16:9 picture shows it in 4:3 shape.
  * The packet chain: its write pointer is the scratchpad word 0x70000654 and its end 0x70000650; when the end is set and
    the next qword would not stay below it, the game calls 0x2EB0C0 and reads the pointer again (the game's own pattern,
    e.g. at 0x10C8A0).
  * mark = 0x8000 | part << 8 | side, part = the place in the array; side 0 = the third of the 640-pixel frame the part's
    first drawing is in (the launcher sets PS2_HUD_WIDTH=640; PS2_HUD_LOG=1 prints each part's box). Read off PS2_HUD_LOG=1
    in Joyridin': part 0 is the rear-view mirror at the top (its frame is x 192..447), which draws the view behind the car
    first, x -1700..2360 clipped to the frame, so its side is given, the centre; part 4 is the radar, the speed and the
    gear at the right edge (x 437..610), which picks its side right by itself, as the others do.
  * The game sends VIF MARKs of its own (513..1023, one with every texture it waits for, inside the parts too); the runtime
    leaves a part's mark alone for those (gif.cpp, hud_mark).
Memory used, all below the game's load address 0x100000 (nothing there is used by the game or the runtime):
  0x000FF000..  the new code;  0x000FFF00 widescreen word, 0x000FFF04 HUD option (16 bit).
"""
import os

R = {n: i for i, n in enumerate(['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
                                 's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra'])}
CAVE = 0x000FF000
WIDE_OPTION = -0x100  # 0x000FFF00, from 0x00100000
HUD_OPTION = -0xFC    # 0x000FFF04
PACKET_AT, PACKET_END = 0x0654, 0x0650  # the game's VIF1 packet pointer and its end, in the scratchpad (0x70000000)
PACKET_FLUSH = 0x2EB0C0  # sends the full buffer; the pointer is read again after it
MIRROR_SIDE = 2          # 1 left, 2 centre, 3 right
HUD_CALL = 0x20CFEC      # jalr ra,v1 in the HUD's part loop 0x20CFA8 (s1 = the part's place in the array)


class Asm:
    def __init__(self, base):
        self.base, self.items, self.labels = base, [], {}

    def label(self, name):
        self.labels[name] = self.base + 4 * len(self.items)

    def emit(self, f):
        self.items.append(f)

    def r(self, fn, rd='zero', rs='zero', rt='zero', sa=0):
        self.emit(lambda L, pc: (R[rs] << 21) | (R[rt] << 16) | (R[rd] << 11) | (sa << 6) | fn)

    def i(self, op, rt, rs, imm):
        self.emit(lambda L, pc: (op << 26) | (R[rs] << 21) | (R[rt] << 16) | (imm & 0xFFFF))

    def or_(self, d, s, t): self.r(37, d, s, t)
    def daddu(self, d, s, t): self.r(45, d, s, t)
    def sltu(self, d, s, t): self.r(43, d, s, t)
    def jr(self, s): self.r(8, 'zero', s)
    def jalr(self, s): self.r(9, 'ra', s)
    def nop(self): self.emit(lambda L, pc: 0)
    def addiu(self, t, s, imm): self.i(9, t, s, imm)
    def ori(self, t, s, imm): self.i(13, t, s, imm)
    def lui(self, t, imm): self.i(15, t, 'zero', imm)
    def lw(self, t, off, s): self.i(35, t, s, off)
    def lhu(self, t, off, s): self.i(37, t, s, off)
    def andi(self, t, s, imm): self.i(12, t, s, imm)
    def sll(self, d, t, sa): self.r(0, d, 'zero', t, sa)
    def sw(self, t, off, s): self.i(43, t, s, off)

    def branch(self, op, s, t, target):
        self.emit(lambda L, pc: (op << 26) | (R[s] << 21) | (R[t] << 16) | (((L[target] - pc - 4) >> 2) & 0xFFFF))

    def beq(self, s, t, target): self.branch(4, s, t, target)
    def bne(self, s, t, target): self.branch(5, s, t, target)
    def j(self, target): self.emit(lambda L, pc: (2 << 26) | (((L[target] if isinstance(target, str) else target) >> 2) & 0x3FFFFFF))
    def jal(self, target): self.emit(lambda L, pc: (3 << 26) | (((L[target] if isinstance(target, str) else target) >> 2) & 0x3FFFFFF))

    def words(self):
        return [(self.base + 4 * k, f(self.labels, self.base + 4 * k)) for k, f in enumerate(self.items)]


def build():
    a = Asm(CAVE)

    # WRAP (t8 = this part's mark, v0 = the function its call goes to; a0 = its argument, set by the call's delay slot)
    a.label('wrap')
    a.addiu('sp', 'sp', -32)
    a.sw('ra', 0, 'sp')
    a.sw('v0', 4, 'sp')
    a.sw('a0', 8, 'sp')
    a.sw('t8', 12, 'sp')
    a.lui('t9', 0x0010)
    a.lhu('t9', HUD_OPTION, 't9')
    a.beq('t9', 'zero', 'wrap_call')
    a.sw('t9', 16, 'sp')
    a.lui('t9', 0x0010)
    a.lhu('t9', WIDE_OPTION, 't9')
    a.beq('t9', 'zero', 'wrap_call')
    a.sw('t9', 16, 'sp')
    a.jal('mark')
    a.lw('a0', 12, 'sp')
    a.label('wrap_call')
    a.lw('v0', 4, 'sp')
    a.jalr('v0')
    a.lw('a0', 8, 'sp')
    a.lw('t9', 16, 'sp')
    a.beq('t9', 'zero', 'wrap_out')
    a.sw('v0', 4, 'sp')       # the part's own return value, kept for the caller
    a.jal('mark')
    a.daddu('a0', 'zero', 'zero')
    a.label('wrap_out')
    a.lw('v0', 4, 'sp')
    a.lw('ra', 0, 'sp')
    a.jr('ra')
    a.addiu('sp', 'sp', 32)

    # MARK (a0 = the value): one qword on the VIF1 chain, DMA tag cnt with no data, VIF FLUSH, VIF MARK a0
    a.label('mark')
    a.addiu('sp', 'sp', -16)
    a.sw('ra', 0, 'sp')
    a.sw('a0', 4, 'sp')
    a.lui('a3', 0x7000)
    a.lw('v1', PACKET_END, 'a3')
    a.beq('v1', 'zero', 'mark_write')    # no end set: the game does not check either
    a.lw('a2', PACKET_AT, 'a3')
    a.addiu('v0', 'a2', 16)
    a.sltu('v0', 'v0', 'v1')             # pointer + 16 < end: room (the game's own test)
    a.bne('v0', 'zero', 'mark_write')
    a.nop()
    a.jal(PACKET_FLUSH)
    a.nop()
    a.label('mark_write')
    a.lui('a3', 0x7000)
    a.lw('a2', PACKET_AT, 'a3')
    a.lui('v0', 0x1000)
    a.sw('v0', 0, 'a2')       # DMA tag: cnt, qwc 0 (its upper half goes to the VIF)
    a.sw('zero', 4, 'a2')
    a.lui('v0', 0x1100)
    a.sw('v0', 8, 'a2')       # VIF FLUSH
    a.lw('a0', 4, 'sp')
    a.lui('v0', 0x0700)
    a.or_('v0', 'v0', 'a0')
    a.sw('v0', 12, 'a2')      # VIF MARK value
    a.addiu('a2', 'a2', 16)
    a.sw('a2', PACKET_AT, 'a3')
    a.lw('ra', 0, 'sp')
    a.jr('ra')
    a.addiu('sp', 'sp', 16)

    # the stub the HUD's call goes to: the part's draw in v0 (from v1), its mark in t8 (from s1), then WRAP
    a.label('stub')
    a.daddu('v0', 'v1', 'zero')
    a.andi('t8', 's1', 0x1F)
    a.sll('t8', 't8', 8)
    a.bne('s1', 'zero', 'stub_go')
    a.ori('t8', 't8', 0x8000)
    a.ori('t8', 't8', MIRROR_SIDE)   # part 0, the rear-view mirror: its side is given (see above)
    a.label('stub_go')
    a.j('wrap')
    a.nop()

    code = a.words()
    return code, [(HUD_CALL, (3 << 26) | ((a.labels['stub'] >> 2) & 0x3FFFFFF))]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    code, hooks = build()
    lines = ["gametitle=Smuggler's Run 2: Hostile Territory (NTSC-U) SLUS-20204 0EF2C216", '', '[HUD 4:3]',
             'author=made for this recompilation (sr2-options/hud43.py)',
             'description=Keeps the driving HUD in 4:3 shapes at the screen edges with 16:9 on; switched by the 16-bit words at 000FFF04 (option) and 000FFF00 (16:9 now).']
    lines += ['patch=0,EE,%08X,word,%08X' % (addr, w) for addr, w in code]
    lines += ['patch=0,EE,%08X,word,%08X' % (addr, w) for addr, w in hooks]
    with open(os.path.join(here, 'hud43.pnach'), 'w', newline='\n') as out:
        out.write('\n'.join(lines) + '\n')
    print('%d words of new code at %08X..%08X, %d hooked draw call' % (len(code), code[0][0], code[-1][0] + 4, len(hooks)))


main()
