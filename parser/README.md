# PMD Sky Speedrun Mod — Crash Dump Parser

A portable, dependency-light Python tool that reads the crash dump record the
speedrun mod writes into the save file's backup EEPROM, decodes it, resolves
addresses against the [`pmdsky-debug`](../pmdsky-debug) reverse-engineered
symbol tables, and prints a user-friendly report.

```text
python -m parser path\to\save.sav [options]
```

## What it shows

- **Record header**: crash type (`FatalError` / `OS_Panic`), record version,
  game version (auto-detected from the hooked `pc`), the recorded
  FatalError format-string message, and a checksum verdict.
- **Crash site**: `pc`, `lr`, `sp`, decoded CPSR (mode, ARM/Thumb, IRQ/FIQ
  mask, NZCVQ flags) and the raw OS tick counter.
- **Registers**: `r0`–`r12`, with the FatalError ABI roles (`arg0` = prog_pos
  pointer, `arg1` = format string, `arg2`/`arg3` = the two captured variadic
  arguments).
- **Backtrace**: a best-effort call chain. Frame #0 is the crash site (`pc`),
  frame #1 is its caller (`lr`), and further candidate return addresses are
  scanned from the 0xE00-byte stack snapshot. Every frame is a *candidate*:
  stack words pointing into code regions are not guaranteed to be return
  addresses, and overlays that share a load address produce multiple
  candidates, all shown.
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
  --raw-stack [N]     also print the annotated stack snapshot
                      (bare flag = all 0xE00/4 words)
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

## Record format (from `src/crash_dump.h`)

All values little-endian. The record lives at EEPROM offset `0xB6B0`
(`--offset`), 0x1000 bytes total.

| Offset | Size | Field |
| --- | --- | --- |
| 0x000 | 4 | magic `'CRSH'` (`0x48535243`) |
| 0x004 | 4 | record version (2) |
| 0x008 | 4 | hook id (1 = FatalError, 2 = OS_Panic) |
| 0x00C | 4 | tick (`OS_GetTickLo()`) |
| 0x010 | 4 | pc — address of the hooked instruction |
| 0x014 | 4 | lr — return address into the crashing caller |
| 0x018 | 4 | sp — stack pointer; the snapshot starts here |
| 0x01C | 4 | cpsr |
| 0x020 | 52 | r0–r12 |
| 0x054 | 4 | checksum (u32 sum of words 0x000..0x053) |
| 0x058 | 16 | arg0–arg3 (original r0–r3 at hook entry) |
| 0x068 | 4 | msg_len |
| 0x06C | 0x194 | FatalError format string (NUL-terminated) |
| 0x200 | 0xE00 | stack snapshot, from `sp` upward |

The first words of the stack snapshot are the hook stub's own register
spills, not caller stack: 4 words for FatalError (`r0`–`r3`) and 14 words for
OS_Panic (`r3`, `lr`, `r0`, `r1`, `r2`, `r4`–`r12`). The backtrace scanner
skips them; `--raw-stack` labels them.
