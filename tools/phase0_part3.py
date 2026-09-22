#!/usr/bin/env python3
# Phase 0 part 3: name unknown addresses, find ROM-accessor callers,
# and locate Card_LockRom/Card_LockBackup call sites with owner values
# in the game save function ReadQuickSaveInfo.
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

print("== naming unknown addresses ==")
for a in (0x207F77C, 0x207FF14, 0x207F9EC, 0x207FEBC, 0x207FED8, 0x207FBB0,
          0x207FAA4, 0x20823A4, 0x207C9E0, 0x207C4FC, 0x207BBEC, 0x2079C20,
          0x2079C70, 0x2079CD8, 0x207A63C, 0x207A674, 0x207A698, 0x207BBA8,
          0x207BBBC, 0x207BFB8, 0x207BB7C, 0x207BB90, 0x20491EC, 0x2034C68):
    print(f"  {a:08X}  {addr_name.get(a, '???')}")
print()

# Find all Cardi_ReadRom* / accessor functions
ACCESSOR = {}
for n, a in syms.items():
    if n.startswith("Cardi_") and any(k in n for k in
            ("ReadRom", "ReadFromCache", "TryReadCardDma", "SetRomOp",
             "ReadCard", "GetRomAccessor", "ReadRomSync")):
        ACCESSOR[a] = n
print("== ROM accessor functions ==")
for a in sorted(ACCESSOR):
    print(f"  {ACCESSOR[a]:32s} {a:08X}")
print()

# Disassemble all functions, collect calls (reuse approach)
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

print("== callers of ROM accessor functions ==")
for a in sorted(ACCESSOR):
    for start, site in callers.get(a, []):
        print(f"  {ACCESSOR[a]:28s} {a:08X}  called from {addr_name.get(start, hex(start))} @ {site:08X}")
print()

# Card lock call sites inside ReadQuickSaveInfo (0x2049960..0x204B300)
LOCK = {0x20837CC: "Card_LockRom", 0x20837E8: "Card_UnlockRom",
        0x2083804: "Card_LockBackup", 0x2083814: "Card_UnlockBackup"}
print("== Card_Lock*/Card_Unlock* call sites in ReadQuickSaveInfo ==")
seq = list(cs.disasm(data[0x2049960 - BASE:0x204B300 - BASE], 0x2049960))
insns = list(seq)
for idx, i in enumerate(insns):
    if i.mnemonic in ("bl", "blx") and i.op_str.startswith("#"):
        try:
            t = int(i.op_str[1:], 16)
        except ValueError:
            continue
        if t in LOCK:
            print(f"\n  @ {i.address:08X}  bl {LOCK[t]}")
            for j in range(max(0, idx - 8), idx):
                print(f"    {insns[j].address:08X}  {insns[j].mnemonic} {insns[j].op_str}")

with open(r"build/phase0_part3.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(LINES) + "\n")
import sys
sys.stderr.write(f"wrote {len(LINES)} lines\n")
