#!/usr/bin/env python3
# Phase 0 part 2: raw literals + disassembly of OS card lock implementations,
# Cardi_OnFifoRecv, and the game's save (ReadQuickSaveInfo) and overlay load
# (FS_LoadOverlayImageAsync) paths.
import struct
import sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

BASE = 0x02000000
data = open(r"build/binaries/arm9.bin", "rb").read()
cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)

LINES = []
def print(*args):
    LINES.append(" ".join(str(a) for a in args))

def w(addr):
    return struct.unpack_from("<I", data, addr - BASE)[0]

def dump_range(name, start, end):
    print(f"== {name} [{start:08X}..{end:08X}] ==")
    off = start - BASE
    for i in cs.disasm(data[off:end - BASE], start):
        print(f"  {i.address:08X}  {i.mnemonic} {i.op_str}")
    print()

print("---- raw literals ----")
for a in (0x207DF3C, 0x207DE44, 0x207E054, 0x207DE90, 0x207DEB4,
          0x207936C, 0x2079388, 0x20793A0, 0x2079350,
          0x2083550, 0x20835DC):
    print(f"  {a:08X} -> 0x{w(a):08X}")
print()

# Real implementations (PLT targets)
dump_range("OS_LockCard impl @0x20791D8", 0x20791D8, 0x20791D8 + 0x60)
dump_range("OS_UnlockCartridge impl @0x20791E8", 0x20791E8, 0x20791E8 + 0x70)
dump_range("OS_UnlockCard impl @0x207925C", 0x207925C, 0x207925C + 0x70)
dump_range("Cardi_OnFifoRecv @0x2084718", 0x2084718, 0x208479C)
dump_range("Card_TerminateForPulledOut @0x2084990", 0x2084990, 0x2084A1C)
dump_range("FS_LoadOverlayImageAsync @0x2080130", 0x2080130, 0x20801C0)
dump_range("ReadQuickSaveInfo @0x2049960", 0x2049960, 0x204B300)

with open(r"build/phase0_part2.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(LINES) + "\n")
sys.stderr.write(f"wrote {len(LINES)} lines to build/phase0_part2.txt\n")

