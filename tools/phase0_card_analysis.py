#!/usr/bin/env python3
# Phase 0 analysis for the ARM9 DSpico uplink (no ARM7 modification).
#
# Goals:
#   1. Validate that the SDK symbol addresses (pmdsky-debug/symbols, EU) line up
#      with the user's actual ARM9 image (build/binaries/arm9.bin, extracted by
#      scripts/patch.py from rom.nds via ndspy).
#   2. Disassemble every known function and build a caller map.
#   3. Determine which functions call the card-request entry points
#      (Cardi_Request / Cardi_SendtoPxi / stream commands) and whether those
#      call paths are protected by the card lock
#      (Card_LockRom / Cardi_LockResource / OS_LockCard / backup lock).
#   4. Dump the disassembly of the lock / PXI / card functions to pin down:
#        - lock semantics (blocking? which lock word?),
#        - the PXI FIFO mailbox layout (base, per-tag stride, in-flight bit),
#        - the CARD tag number used by the card request path.
#   5. Find all literal-pool constants in the PXI/WRAM tail region
#      (0x023FF000-0x02400000) and attribute them to functions, to map which
#      mailbox words are used where.
#
# Output: build/phase0_report.txt (and a short summary on stdout).

import os
import re
import sys
import yaml
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARM9_PATH = os.path.join(REPO, "build", "binaries", "arm9.bin")
SYMS_GAME = os.path.join(REPO, "pmdsky-debug", "symbols", "arm9.yml")
SYMS_LIBS = os.path.join(REPO, "pmdsky-debug", "symbols", "arm9", "libs.yml")
REPORT = os.path.join(REPO, "build", "phase0_report.txt")

BASE = 0x02000000

REPORT_LINES = []
def out(s=""):
    REPORT_LINES.append(s)
    print(s)

def hexw(x):
    return "0x%08X" % x

# ---------------------------------------------------------------- symbols ---

def parse_yml_symbols(path):
    """Walk a pmdsky-debug symbol yml; return {name: eu_addr}."""
    names = {}
    with open(path, "r", encoding="utf-8") as f:
        doc = yaml.safe_load(f)

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

def main():
    game_syms = parse_yml_symbols(SYMS_GAME)
    lib_syms = parse_yml_symbols(SYMS_LIBS)
    all_syms = dict(game_syms)
    for k, v in lib_syms.items():
        all_syms.setdefault(k, v)
    addr_to_names = {}
    for n, a in all_syms.items():
        addr_to_names.setdefault(a, []).append(n)

    data = open(ARM9_PATH, "rb").read()
    size = len(data)
    out("# Phase 0 report: card lock coverage + PXI layout")
    out()
    out(f"image: {ARM9_PATH}")
    out(f"size:  {hexw(size)}  ({size} bytes), base {hexw(BASE)}")
    out(f"symbols: {len(game_syms)} game + {len(lib_syms)} sdk = {len(all_syms)} total")
    out()

    # ------------------------------------------------- symbol validation ----
    KEY_FUNCS = [
        "Card_LockRom", "Card_UnlockRom", "Card_LockBackup", "Card_UnlockBackup",
        "Cardi_LockResource", "Cardi_UnlockResource",
        "OS_LockCard", "OS_UnlockCard", "OSi_AllocateCardBus", "OSi_FreeCartridgeBus",
        "Pxi_InitFifo", "Pxi_SendWordByFifo", "Pxi_IsCallbackReady",
        "Pxii_HandlerRecvFifoNotEmpty",
        "Cardi_Request", "Cardi_SendtoPxi", "Cardi_WaitAsync", "Cardi_TryWaitAsync",
        "FS_LoadOverlayImageAsync",
    ]
    out("== symbol prologue validation (expect PUSH/STMDB or similar at each) ==")
    cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    for name in KEY_FUNCS:
        addr = all_syms.get(name)
        if addr is None or not (BASE <= addr < BASE + size):
            out(f"  {name:32s} MISSING")
            continue
        word = int.from_bytes(data[addr - BASE:addr - BASE + 4], "little")
        insns = list(cs.disasm(word.to_bytes(4, "little"), addr))
        if insns:
            i = insns[0]
            ok = i.mnemonic in ("push", "stmdb", "sub", "mov", "nop", "and")
            out(f"  {name:32s} {hexw(addr)}  {i.mnemonic} {i.op_str}  {'OK' if ok else '??'}")
        else:
            out(f"  {name:32s} {hexw(addr)}  <not ARM-decodable>  BAD")
    out()

    # -------------------------------------------------- disassembly map -----
    # Sequential disasm of each known function, stopping at the next function
    # boundary, invalid instruction, or padding.
    func_starts = sorted(a for a in addr_to_names if BASE <= a < BASE + size)
    next_of = {}
    for i, a in enumerate(func_starts):
        next_of[a] = func_starts[i + 1] if i + 1 < len(func_starts) else BASE + size

    func_insns = {}   # start_addr -> list of (addr, mnemonic, op_str)
    func_calls = {}   # start_addr -> list of (call_site, target or None, kind)
    bad = 0
    for start in func_starts:
        end = min(next_of[start], BASE + size)
        off = start - BASE
        chunk = data[off:min(end - BASE, off + 0x40000)]
        seq = list(cs.disasm(chunk, start))
        if not seq:
            bad += 1
            continue
        insns = []
        calls = []
        for i in seq:
            if i.address >= end:
                break
            if i.mnemonic in ("bl", "blx"):
                op = i.op_str
                if op.startswith("#"):
                    try:
                        calls.append((i.address, int(op[1:], 16), "direct"))
                    except ValueError:
                        pass
                else:
                    calls.append((i.address, None, "indirect"))
            elif i.mnemonic == "bx" and not i.op_str.startswith("#"):
                calls.append((i.address, None, "bx-reg"))
            elif i.mnemonic == "mov" and i.op_str.startswith("pc,"):
                calls.append((i.address, None, "mov-pc"))
            insns.append((i.address, i.mnemonic, i.op_str))
        func_insns[start] = insns
        func_calls[start] = calls
    out(f"disassembled {len(func_insns)} functions ({bad} undecodable)")
    out()

    def name_at(addr):
        if addr in addr_to_names:
            return ",".join(addr_to_names[addr])
        best = None
        for a in func_starts:
            if a <= addr and (best is None or a > best):
                best = a
        if best is not None:
            return f"{addr_to_names[best][0]}+0x{addr - best:X}"
        return "???"

    # ------------------------------------------------------ caller map ------
    callers = {}   # target_addr -> [(caller_start, call_site, kind)]
    for start, calls in func_calls.items():
        for site, tgt, kind in calls:
            if tgt is not None:
                callers.setdefault(tgt, []).append((start, site, kind))

    REQ_FUNCS = {}
    for n, a in all_syms.items():
        if n.startswith("Cardi_") and any(k in n for k in ("Request", "Send", "Command")):
            REQ_FUNCS.setdefault(a, []).append(n)
    LOCK_FUNCS = {}
    for n, a in all_syms.items():
        if any(k in n for k in ("LockRom", "LockBackup", "LockResource", "LockCard",
                                "AllocateCardBus", "FreeCartridgeBus")):
            LOCK_FUNCS.setdefault(a, []).append(n)
    lock_addrs = set(LOCK_FUNCS.keys())

    out("== card request entry points ==")
    for a in sorted(REQ_FUNCS):
        out(f"  {','.join(REQ_FUNCS[a]):36s} {hexw(a)}")
    out()
    out("== lock entry points ==")
    for a in sorted(LOCK_FUNCS):
        out(f"  {','.join(LOCK_FUNCS[a]):36s} {hexw(a)}")
    out()

    def has_lock_call(start):
        for _, tgt, kind in func_calls.get(start, []):
            if tgt in lock_addrs:
                return True
        return False

    def lock_note(start):
        return "LOCKS" if has_lock_call(start) else "no-lock"

    out("== callers of card request entry points ==")
    for a in sorted(REQ_FUNCS):
        cl = callers.get(a, [])
        out(f"\n-- {','.join(REQ_FUNCS[a])} {hexw(a)}: {len(cl)} direct call sites")
        for start, site, kind in sorted(cl):
            up = callers.get(start, [])
            uplock = all(has_lock_call(u) for u, _, _ in up) if up else None
            upnames = ";".join(name_at(u) for u, _, _ in up[:8])
            verdict = "covered(via caller)" if uplock else ("COVERED" if has_lock_call(start) else "UNCHECKED")
            out(f"   {name_at(start):40s} {hexw(site)}  [{lock_note(start)}]  <- {verdict}  callers: {upnames[:90]}")
    out()

    # Indirect references to request funcs via literal pools (branch tables).
    out("== literal-pool constants == card request / lock addresses ==")
    tset = set(REQ_FUNCS) | lock_addrs
    for i in range(0, size - 4, 4):
        w = int.from_bytes(data[i:i + 4], "little")
        if w in tset:
            addr = BASE + i
            fn = name_at(addr)
            out(f"   const {hexw(w)} at {hexw(addr)}  in ~{fn[:70]}")
    out()



    # ------------------------------------------------ key disassembly -------
    def dump(name, max_lines=400):
        addr = all_syms.get(name)
        if addr is None:
            out(f"!! {name}: not in symbols")
            return
        out(f"\n== {name} @ {hexw(addr)} ==")
        insns = func_insns.get(addr, [])
        for (a, m, o) in insns[:max_lines]:
            out(f"  {hexw(a)}  {m} {o}")
        if len(insns) > max_lines:
            out(f"  ... ({len(insns) - max_lines} more lines)")

    for n in ("Card_LockRom", "Card_UnlockRom", "Card_LockBackup",
              "Cardi_LockResource", "Cardi_UnlockResource",
              "OS_LockCard", "OS_UnlockCard", "OSi_AllocateCardBus",
              "OSi_FreeCartridgeBus"):
        dump(n)
    for n in ("Pxi_InitFifo", "Pxi_SendWordByFifo", "Pxi_IsCallbackReady",
              "Pxii_HandlerRecvFifoNotEmpty", "Pxi_SetFifoRecvCallback"):
        dump(n)
    for n in ("Cardi_Request", "Cardi_SendtoPxi", "Cardi_WaitAsync",
              "Cardi_TryWaitAsync"):
        dump(n)
    out()

    # ---------------------------- PXI region constant attribution ----------
    out("== literal constants in 0x023FF000..0x02400000 (PXI mailbox area) ==")
    seen = {}
    for i in range(0, size - 4, 4):
        w = int.from_bytes(data[i:i + 4], "little")
        if 0x023FF000 <= w < 0x02400000:
            seen.setdefault(w, []).append(BASE + i)
    for w in sorted(seen):
        locs = seen[w]
        fns = sorted(set(name_at(l) for l in locs))
        out(f"   {hexw(w)}  x{len(locs):3d}  in ~{';'.join(fns[:6])[:100]}")
    out()

    with open(REPORT, "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT_LINES) + "\n")
    print(f"\nreport written to {REPORT} ({len(REPORT_LINES)} lines)")

if __name__ == "__main__":
    main()
