#!/usr/bin/env python3
# Phase 0 part 4: dump the ROM accessor chain (does it self-lock?),
# name the save-path helpers, and list all Card_LockRom callers.
import struct
import yaml
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

BASE = 0x02000000
data = open(r"build/binaries/arm9.bin", "rb").read()
cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)

def parse_yml_symbols(path):
    names = {}
    doc = yaml.safe_load(open(path, "r", encoding="utf-8"))
    def walk(node):
        if isinstance(node, dict):
            if "name" in node and "address" in node and isinstance(node["address"], dict):
                addr = node["address"].get("EU")
                if addr is not None and isinstance(addr, (str, int)):
                    a = int(addr, 16) if isinstance(addr, str) else int(addr)
                    names.setdefault(node["name"], a)
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(doc)
    return names

syms = dict(parse_yml_symbols(r"pmdsky-debug/symbols/arm9/libs.yml"))
syms.update(parse_yml_symbols(r"pmdsky-debug/symbols/arm9.yml"))
addr_name = {a: n for n, a in syms.items()}

LINES = []
def print(*a):
    LINES.append(" ".join(str(x) for x in a))

def dump(name, start, end):
    print(f"== {name} @ {start:08X} ==")
    for i in cs.disasm(data[start - BASE:end - BASE], start):
        print(f"  {i.address:08X}  {i.mnemonic} {i.op_str}")
    print()

print("== naming ==")
for a in (0x20793C4, 0x2083ED4, 0x2083EAC, 0x2002580, 0x20027F8,
          0x207FCD8, 0x208469C):
    print(f"  {a:08X}  {addr_name.get(a, '???')}")
print()

# lock entry points
LOCK = {0x20837CC: "Card_LockRom", 0x20837E8: "Card_UnlockRom",
        0x2083804: "Card_LockBackup", 0x2083814: "Card_UnlockBackup",
        0x20834D0: "Cardi_LockResource", 0x2083554: "Cardi_UnlockResource",
        0x2079354: "OS_LockCard", 0x2079370: "OS_UnlockCard"}

# Disassemble the accessor chain
dump("Cardi_ReadFromCache", 0x2084024, 0x20840B0)
dump("Cardi_SetRomOp", 0x20840B0, 0x2084234)
dump("Cardi_TryReadCardDma", 0x2084234, 0x20843C0)
dump("Cardi_ReadCard", 0x20843C0, 0x208450C)
dump("Cardi_ReadRomSyncCore", 0x208450C, 0x20845A4)
dump("Cardi_ReadRom", 0x20845A4, 0x208470C)
dump("FSi_ReadRomCallback", 0x207FCD8, 0x207FDB0)

# find all callers of Card_LockRom / Card_LockBackup / OS_LockCard
func_starts = sorted(a for a in addr_name if BASE <= a < BASE + len(data))
next_of = {}
for i, a in enumerate(func_starts):
    next_of[a] = func_starts[i + 1] if i + 1 < len(func_starts) else BASE + len(data)

callers = {}
for start in func_starts:
    end = min(next_of[start], BASE + len(data))
    off = start - BASE
    seq = list(cs.disasm(data[off:end - BASE], start))
    for i in seq:
        if i.address >= end:
            break
        if i.mnemonic in ("bl", "blx") and i.op_str.startswith("#"):
            try:
                t = int(i.op_str[1:], 16)
            except ValueError:
                continue
            callers.setdefault(t, []).append((start, i.address))

print("== all callers of lock entry points ==")
for a, nm in sorted(LOCK.items()):
    cl = callers.get(a, [])
    print(f"\n-- {nm} {a:08X}: {len(cl)} call sites")
    for start, site in cl:
        print(f"   {addr_name.get(start, hex(start)):40s} {site:08X}")

with open(r"build/phase0_part4.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(LINES) + "\n")
import sys
sys.stderr.write(f"wrote {len(LINES)} lines\n")
