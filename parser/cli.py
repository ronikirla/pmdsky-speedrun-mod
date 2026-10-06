"""Command-line interface: ``python -m parser SAV [options]``."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import List, Optional

from . import backtrace as backtrace_mod
from . import dump as dump_mod
from . import report as report_mod
from . import symtab as symtab_mod

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_CLEAN_SAVE = 2


def _int_arg(text: str) -> int:
    try:
        return int(text, 0)
    except ValueError:
        raise argparse.ArgumentTypeError("not a number: %r" % text)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m parser",
        description="Parse the PMD Sky speedrun mod's crash dump record from a "
                    "save file and print a user-friendly report.",
    )
    parser.add_argument("sav", help="path to the .sav file (or a bare 0x1000-byte dump record)")
    parser.add_argument(
        "--offset", type=_int_arg, default=None, metavar="N",
        help="offset of the crash dump record in the file (default: %s; "
             "'0x' hex prefixes accepted)" % hex(dump_mod.EEPROM_BASE),
    )
    parser.add_argument(
        "--symbols", default=None, metavar="DIR",
        help="pmdsky-debug symbol directory (default: auto-detected next to this script "
             "or in the current directory)",
    )
    parser.add_argument(
        "--version", choices=("EU", "NA", "JP"), default=None,
        help="game version (default: auto-detected from the hooked pc)",
    )
    parser.add_argument(
        "--mod-symbols", default=None, metavar="FILE",
        help="symbol file for the mod itself (build/binaries/symbols.asm, armips "
             ".definelabel / NAME = addr / equ / nm text)",
    )
    parser.add_argument(
        "--rom", default=None, metavar="FILE",
        help="game ROM (rom.nds or out.nds) used to dereference pointers: recovers "
             "the FatalError assert file/line and formats %%s arguments (needs ndspy)",
    )
    parser.add_argument(
        "--max-frames", type=_int_arg, default=32, metavar="N",
        help="maximum number of backtrace frames to report (default: 32)",
    )
    parser.add_argument(
        "--raw-stack", nargs="?", type=_int_arg, const=-1, default=0, metavar="WORDS",
        help="also print an annotated stack snapshot; give WORDS to cap the number "
             "of words shown (bare --raw-stack prints all %d words)"
             % dump_mod.STACK_WORD_COUNT,
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    return parser


def _auto_symbols_dir() -> Optional[Path]:
    candidates = [
        Path.cwd() / "pmdsky-debug" / "symbols",
        Path(__file__).resolve().parent.parent / "pmdsky-debug" / "symbols",
    ]
    for candidate in candidates:
        if (candidate / "arm9.yml").exists():
            return candidate
    return None


def main(argv: Optional[List[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)
    warnings: List[str] = []

    # --- read + locate + parse the record -----------------------------------
    try:
        data = Path(args.sav).read_bytes()
    except OSError as exc:
        print("error: cannot read %s: %s" % (args.sav, exc), file=sys.stderr)
        return EXIT_ERROR

    try:
        located = dump_mod.locate_record(data, args.offset)
    except dump_mod.CleanSaveError as exc:
        print("No crash dump present: %s." % exc)
        print("The save looks clean - the game has not crashed since the region "
              "was last erased.")
        return EXIT_CLEAN_SAVE
    except dump_mod.DumpError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return EXIT_ERROR

    try:
        dump = dump_mod.parse_record(located.data, located.offset)
    except dump_mod.DumpError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return EXIT_ERROR
    warnings.extend(located.warnings)
    warnings.extend(dump.warnings)

    # --- symbols -------------------------------------------------------------
    table: Optional[symtab_mod.SymbolTable] = None
    if args.symbols:
        symbols_dir = Path(args.symbols)
        if not (symbols_dir / "arm9.yml").exists():
            print("error: --symbols %s does not look like a pmdsky-debug symbols "
                  "directory (no arm9.yml found)" % args.symbols, file=sys.stderr)
            return EXIT_ERROR
    else:
        symbols_dir = _auto_symbols_dir()

    version = args.version
    version_source = "specified with --version"
    if symbols_dir is not None:
        try:
            db = symtab_mod.SymbolDb.load(symbols_dir)
        except Exception as exc:  # noqa: BLE001 - symbol problems must not kill the report
            warnings.append("Failed to load symbol tables from %s: %s" % (symbols_dir, exc))
            db = None
        if db is not None:
            warnings.extend(db.warnings)
            if version is None:
                version = db.detect_version(dump.pc, dump.hook_id)
                if version is not None:
                    version_source = ("auto-detected from pc (= hooked %s entry)"
                                      % dump.hook_name)
                else:
                    version = "EU"
                    version_source = "not detected; defaulted to EU"
            table = db.table_for(version)
            mod_symbols = None
            if args.mod_symbols:
                try:
                    mod_symbols = symtab_mod.parse_mod_symbols(Path(args.mod_symbols))
                except OSError as exc:
                    warnings.append("Failed to read --mod-symbols %s: %s"
                                    % (args.mod_symbols, exc))
                else:
                    if not mod_symbols:
                        warnings.append("--mod-symbols %s contained no recognizable "
                                        "symbol lines." % args.mod_symbols)
            table.blocks.extend(symtab_mod.builtin_mod_blocks(version, mod_symbols))
    else:
        warnings.append(
            "pmdsky-debug symbol tables not found; addresses are shown without "
            "symbol names. Use --symbols to point at the directory."
        )
        if version is None:
            version = "EU"
            version_source = "defaulted (no symbol tables available for detection)"

    # --- backtrace + ROM dereferencing ---------------------------------------
    frames, truncated = backtrace_mod.build_backtrace(dump, table, max_frames=args.max_frames)

    rom_info = None
    if args.rom:
        try:
            from . import rommap as rommap_mod
            try:
                rom = rommap_mod.RomMap(args.rom, prefer=dump.lr)
                rom_info = rommap_mod.extract_rom_info(dump, rom)
            except rommap_mod.RomMapError as exc:
                warnings.append("ROM dereferencing disabled: %s" % exc)
        except Exception as exc:  # noqa: BLE001 - ndspy parse errors etc.
            warnings.append("Failed to read ROM %s: %s" % (args.rom, exc))

    # --- render ---------------------------------------------------------------
    ctx = report_mod.ReportContext(
        source_path=str(args.sav),
        source_offset=located.offset,
        version=version,
        version_source=version_source,
        symbols_dir=str(symbols_dir) if symbols_dir is not None else None,
        warnings=warnings,
        rom_path=str(args.rom) if args.rom else None,
    )
    if args.json:
        payload = report_mod.build_json(dump, table, frames, truncated, rom_info, ctx)
        print(json.dumps(payload, indent=2))
    else:
        raw_stack_limit = args.raw_stack if args.raw_stack else None
        print(report_mod.render_text(dump, table, frames, truncated, rom_info, ctx,
                                     raw_stack_limit=raw_stack_limit))
    return EXIT_OK
