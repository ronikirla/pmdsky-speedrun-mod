#!/usr/bin/env python3
# Dump raw literal-pool u32 words for specific PMDSky ARM9 addresses.
import struct

BASE = 0x02000000
data = open(r"build/binaries/arm9.bin", "rb").read()

def w(addr):
    o = addr - BASE
    return struct.unpack_from("<I", data, o)[0]

groups = {
    # Pxi_SendWordByFifo @0x207DEB8: 0x207DEC0 ldr r3,[pc,#0x74]
    "Pxi_SendWordByFifo lit 16-bit reg": [0x207DEC4 + 0x74],
    # Pxi_InitFifo @0x207DD48
    "Pxi_InitFifo lits": [0x207DD54 + 0xD4, 0x207DD74 + 0xB8, 0x207DD80 + 0xB0,
                          0x207DD94 + 0xA0, 0x207DD98 + 0xA0, 0x207DDA8 + 0x94,
                          0x207DDC0 + 0x80],
    # Pxii_HandlerRecvFifoNotEmpty @0x207DF40
    "Pxii_HandlerRecvFifoNotEmpty lits": [0x207DF54 + 0xF8, 0x207DF58 + 0xF8],
    # Pxi_SetFifoRecvCallback @0x207DE48
    "Pxi_SetFifoRecvCallback lits": [0x207DE5C + 0x2C, 0x207DE60 + 0x2C],
    # Pxi_IsCallbackReady @0x207DE94
    "Pxi_IsCallbackReady lit": [0x207DE98 + 0x18],
    # PLT stubs
    "OS_LockCard PLT": [0x2079358 + 8, 0x207935C + 8, 0x2079360 + 8],
    "OS_UnlockCard PLT": [0x2079374 + 8, 0x2079378 + 8, 0x207937C + 8],
    "OSi_AllocateCardBus lit": [0x2079390 + 0xC],
    "OSi_FreeCartridgeBus lit": [0x2079340 + 0xC],
    # OS_UnlockCartridge (found at ~0x20792D8): dump its pool
    "OS_UnlockCartridge pool": [0x20792D8, 0x20792DC, 0x20792E0, 0x20792E4,
                                0x20792E8, 0x20792EC, 0x20792F0, 0x20792F4,
                                0x20792F8, 0x20792FC, 0x2079300],
    # Cardi_LockResource @0x20834D0 / Cardi_UnlockResource @0x2083554: global state ptr
    "Cardi_LockResource lit": [0x20834D4 + 0x74],
    "Cardi_UnlockResource lit": [0x2083558 + 0x7C],
}

for name, addrs in groups.items():
    print(f"== {name} ==")
    for a in addrs:
        v = w(a)
        print(f"   {a:08X}  ->  0x{v:08X}")
    print()
