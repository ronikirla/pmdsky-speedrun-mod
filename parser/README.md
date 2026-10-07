# PMD Sky Speedrun Mod — Crash Dump Parser

A portable, dependency-light Python tool that reads the crash dump record the
speedrun mod writes into the save file's backup EEPROM, decodes it, resolves
addresses against the [`pmdsky-debug`](../pmdsky-debug) reverse-engineered
symbol tables, and prints a user-friendly report.

```text
python -m parser path\to\save.sav [options]
```

## What it shows

- **Record header**: crash type (`FatalError` / `OS_Panic` / manual
  `L+R+X+Y` trigger), record version, game version (auto-detected from the
  hooked `pc`), the recorded FatalError format-string message, the thread
  counts and a checksum verdict.
- **Crash site**: `pc`, `lr`, `sp`, decoded CPSR (mode, ARM/Thumb, IRQ/FIQ
  mask, NZCVQ flags) and the raw OS tick counter.
- **Registers**: `r0`–`r12`, with the FatalError ABI roles (`arg0` = prog_pos
  pointer, `arg1` = format string, `arg2`/`arg3` = the two captured variadic
  arguments).
- **Per-thread backtraces**: one section per thread record. The current
  thread (the crash/trigger site) is reported first; every other thread gets
  its id, priority, OS state, saved `pc`/`lr`/`sp`, stack bounds and its own
  best-effort call chain. Frame #0 is the crash site / saved `pc`, frame #1
  is `lr`, and further candidate return addresses are scanned from that
  thread's stack snapshot. Every frame is a *candidate*: stack words pointing
  into code regions are not guaranteed to be return addresses, and overlays
  that share a load address produce multiple candidates, all shown.
- **ROM-derived info** (with `--rom`): the original assert location
  (`source_file.c:line`, from FatalError's `struct prog_pos_info`) and the
  format string with its `%s`/`%d`/`%x` arguments resolved (only the two
  captured variadic args are available).

## Requirements

| Feature | Needs | Fallback without it |
| --- | --- | --- |
| Parsing + report | Python 3.8+ (stdlib only) | — |
| Symbol resolution | [PyYAML](https://pypi.org/project/PyYAML/) (`pip install pyyaml`) | raw addresses + region labels |
| `--rom` dereferencing | [ndspy](https://pypi.org/project/ndspy/) (`pip install ndspy`) | note explaining what was skipped |

The package is self-contained (no imports from the mod's build system), so
the `parser/` folder can be copied elsewhere and used standalone.

## Options

```text
python -m parser SAV
  --offset N          record offset in the file (default 0xB6B0; 0x hex ok)
  --symbols DIR       pmdsky-debug symbol directory
                      (default: auto-detected next to this script or in cwd)
  --version EU|NA|JP  game version (default: auto-detected from pc)
  --mod-symbols FILE  mod symbol file: build/binaries/symbols.asm (armips
                      ".definelabel NAME,0xADDR" / "NAME = addr" / "equ"),
                      or nm-style "ADDR TYPE NAME" text
  --rom FILE          game ROM (rom.nds or out.nds) for pointer dereferencing
  --max-frames N      backtrace frame cap (default 32)
  --raw-stack [N]     also print the annotated stack snapshot of the current
                      thread (bare flag = the whole snapshot)
  --json              machine-readable JSON output
```

Exit codes: `0` report printed · `1` error · `2` clean save (no crash dump).

The symbol directory is found automatically when the command runs inside this
repository (`pmdsky-debug/symbols`). A file that is exactly 0x1000 bytes is
treated as a bare dump record; other layouts are searched for a valid
`'CRSH'` record.

## Examples

```sh
python -m parser out.sav
python -m parser out.sav --mod-symbols build\binaries\symbols.asm --rom out.nds
python -m parser out.sav --json
python -m parser out.sav --raw-stack 64
python -m unittest discover -s parser/tests        # run the test suite
```

Notes:

- Prefer `out.nds` (the patched ROM) with `--rom`: dumps may reference
  mod-owned strings (e.g. the crash-dump test trigger), which only exist in
  the patched build.
- `--mod-symbols` labels come from `build/binaries/symbols.asm`, which has no
  function/data type information — every label is offered as a candidate, so
  data labels may appear among candidate frames.
- The recorded `tick` is `OS_GetTickLo()` — a free-running hardware timer
  snapshot, **not** a wall-clock timestamp; it is displayed raw.
- The manual trigger (hold L+R+X+Y) writes the same record with hook id 3
  while the game keeps running; use it when the game hangs without reaching
  FatalError/OS_Panic. The dump fires once per boot.

## Record format (from `src/crash_dump.h`)

All values little-endian. The record lives at EEPROM offset `0xB6B0`
(`--offset`), 0x1000 bytes total. Since format version 3 it holds a fixed
0x100-byte header followed by packed variable-length thread records, one per
thread, each with its own stack snapshot.

Header:

| Offset | Size | Field |
| --- | --- | --- |
| 0x000 | 4 | magic `'CRSH'` (`0x48535243`) |
| 0x004 | 4 | record version (3) |
| 0x008 | 4 | hook id (1 = FatalError, 2 = OS_Panic, 3 = manual L+R+X+Y) |
| 0x00C | 4 | tick (`OS_GetTickLo()`) |
| 0x010 | 4 | pc — hooked instruction / trigger site (current thread) |
| 0x014 | 4 | lr |
| 0x018 | 4 | sp — snapshot base of thread record 0 |
| 0x01C | 4 | cpsr |
| 0x020 | 52 | r0–r12 |
| 0x054 | 4 | checksum (u32 sum of the header words except 0x054 and 0x0FC) |
| 0x058 | 16 | arg0–arg3 (original r0–r3 at hook entry) |
| 0x068 | 4 | msg_len |
| 0x06C | 4 | thread_count (threads intended for the dump) |
| 0x070 | 4 | threads_written (thread records actually written) |
| 0x074 | 4 | crashing_index (index of the current thread's record; 0) |
| 0x078 | 4 | trigger_buttons (raw `held_buttons` for manual triggers) |
| 0x07C | 0x80 | FatalError format string (NUL-terminated) |
| 0x0FC | 4 | complete flag (written last; 0 = the dump was interrupted) |

Thread records (packed from 0x100, variable length):

| Offset | Size | Field |
| --- | --- | --- |
| +0x00 | 4 | magic `'THRD'` (`0x44524854`) |
| +0x04 | 4 | thread id (`0xFFFFFFFF` when unknown) |
| +0x08 | 4 | priority (`thread::sorting_order`) |
| +0x0C | 4 | pc (current thread: crash site; others: saved `os_context` pc) |
| +0x10 | 4 | lr (saved `os_context` lr) |
| +0x14 | 4 | sp — the snapshot starts here |
| +0x18 | 4 | stack_start (low end of the thread's stack area) |
| +0x1C | 4 | stack_end (high end, exclusive) |
| +0x20 | 4 | state (`enum os_thread_state`) |
| +0x24 | 4 | snapshot_bytes (multiple of 4) |
| +0x28 | 4 | flags (bit 0 = sp inside the stack bounds, bit 1 = current thread) |
| +0x2C | n | stack snapshot, from `sp` upward |

Thread record 0 is always the current thread (the crashing thread, or the
watchdog for a manual trigger); the other records follow the game's thread
list order (highest priority first). Each snapshot gets a fair share of the
space that is left, so all threads always fit.

The first words of the *current* thread's snapshot are the hook stub's own
register spills, not caller stack: 4 words for FatalError (`r0`–`r3`) and 14
words for OS_Panic (`r3`, `lr`, `r0`, `r1`, `r2`, `r4`–`r12`). The backtrace
scanner skips them; `--raw-stack` labels them. Other threads' snapshots have
no spill prefix.

Records written by mod versions older than the format change (version 2: a
0x200-byte header plus one 0xE00-byte stack snapshot) are rejected with an
error.
