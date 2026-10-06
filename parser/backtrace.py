"""Best-effort backtrace reconstruction from the crash dump's stack snapshot.

The snapshot starts at ``sp`` (``sp_base`` in ``src/crash_dump.h``) and runs
upward for 0xE00 bytes.  Its first few words are the hook stub's own register
spills (see ``patches/patch.asm``), not the crashed caller's stack frame:

- FatalError stub: ``sp_base`` lands on the stub's ``push {r0-r3}``, so words
  0-3 hold the original r0-r3 and the caller's stack starts at word 4
  (the original sp at hook entry).
- OS_Panic stub: ``sp_base`` lands on the stub's ``push {r3, lr}``; the stub
  spills r3, lr, r0, r1, r2 and r4-r12 (14 words total) before the caller's
  stack begins.

Every later word whose value points into an executable region is emitted as
a candidate return address (a potential caller frame).  This is necessarily
a heuristic: a word pointing into a code region is not guaranteed to be a
return address, so every frame is presented as a *candidate* together with
its symbol matches.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

from .dump import CrashDump, HOOK_FATAL_ERROR, HOOK_OS_PANIC
from .symtab import Resolution, SymbolTable, default_code_filter

# Words at the start of the stack snapshot that belong to the hook stub's
# register spills rather than to the crashed caller's stack frame.
SNAPSHOT_SKIP_WORDS = {
    HOOK_FATAL_ERROR: 4,
    HOOK_OS_PANIC: 14,
}

# Which register each skipped word holds (for annotated stack dumps).
SPILL_REGISTER_LABELS = {
    HOOK_FATAL_ERROR: ["r0", "r1", "r2", "r3"],
    HOOK_OS_PANIC: ["r3", "lr", "r0", "r1", "r2",
                    "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"],
}


@dataclass
class Frame:
    """One candidate entry in the reconstructed backtrace."""

    index: int
    origin: str                       # 'pc' | 'lr' | 'stack'
    raw: int                          # original word value (bit 0 may be set)
    address: int                      # code address (bit 0 stripped)
    thumb: bool                       # bit 0 was set: Thumb return address
    stack_offset: Optional[int]       # byte offset within the snapshot
    stack_address: Optional[int]      # absolute stack address of the word
    candidates: List[Resolution] = field(default_factory=list)


def build_backtrace(dump: CrashDump, table: Optional[SymbolTable],
                    max_frames: int = 32) -> Tuple[List[Frame], bool]:
    """Reconstruct the backtrace.

    Frame #0 is the crash site (``pc`` = the hooked FatalError/OS_Panic),
    frame #1 is its caller (``lr``), and further frames are candidate return
    addresses scanned from the stack snapshot.  Returns ``(frames,
    truncated)``.
    """
    code_filter = table.is_code_address if table is not None else default_code_filter

    frames: List[Frame] = []

    def add(raw: int, origin: str, stack_offset: Optional[int] = None) -> None:
        address = raw & ~1
        if not code_filter(address):
            return
        if table is not None:
            candidates = table.resolve(address, kind="function")
            if not candidates:
                candidates = [r for r in table.resolve(address) if r.exact]
        else:
            candidates = []
        frames.append(Frame(
            index=len(frames),
            origin=origin,
            raw=raw,
            address=address,
            thumb=bool(raw & 1),
            stack_offset=stack_offset,
            stack_address=(dump.sp + stack_offset) if (stack_offset is not None and dump.sp) else None,
            candidates=candidates,
        ))

    add(dump.pc, "pc")
    add(dump.lr, "lr")

    seen = {f.address for f in frames}
    words = dump.stack_words()
    skip = SNAPSHOT_SKIP_WORDS.get(dump.hook_id, 4)
    truncated = False
    for i in range(skip, len(words)):
        if len(frames) >= max_frames:
            truncated = True
            break
        raw = words[i]
        address = raw & ~1
        if raw == 0 or address in seen:
            continue
        if not code_filter(address):
            continue
        # A stack word is only a plausible *return address* when it matches a
        # function symbol; words whose best candidates are data symbols
        # (pointers to tables/structs) are not frames.  Strict filtering only
        # applies in regions the tables can actually resolve; elsewhere every
        # code-region word is kept so the raw trace still works.
        if table is not None and table.has_symbols_near(address) and not table.resolve(address, kind="function"):
            continue
        before = len(frames)
        add(raw, "stack", stack_offset=i * 4)
        if len(frames) > before:
            seen.add(address)
    return frames, truncated


def snapshot_rows(dump: CrashDump, table: Optional[SymbolTable],
                  limit: Optional[int] = None) -> List[dict]:
    """Annotated rows for ``--raw-stack``.

    Each row is a dict with the word index, absolute stack address, raw
    value, the spilled register label (for the hook stub's own words) and
    symbol candidates.  Code-address words get full candidates; other words
    only get *exact* symbol hits, to avoid labelling every RAM pointer with
    a nearest-below guess.
    """
    words = dump.stack_words()
    skip = SNAPSHOT_SKIP_WORDS.get(dump.hook_id, 4)
    spill = SPILL_REGISTER_LABELS.get(dump.hook_id, [])
    rows: List[dict] = []
    for i, raw in enumerate(words):
        if limit is not None and i >= limit:
            break
        address = raw & ~1
        candidates: List[Resolution] = []
        is_code = False
        if i >= skip:
            if table is not None:
                if table.is_code_address(address):
                    is_code = True
                    candidates = table.resolve(address, kind="function")
                    if not candidates:
                        candidates = [r for r in table.resolve(address) if r.exact]
                else:
                    candidates = [r for r in table.resolve(address) if r.exact]
            elif default_code_filter(address):
                is_code = True
        rows.append({
            "index": i,
            "stack_address": (dump.sp + i * 4) if dump.sp else None,
            "raw": raw,
            "spill": spill[i] if i < len(spill) else None,
            "is_code": is_code,
            "candidates": candidates,
        })
    return rows
