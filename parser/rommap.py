"""Optional game-ROM dereferencing (requires ``ndspy``, imported lazily).

With a game ROM available (``rom.nds``, the clean base, or ``out.nds``, the
patched build), the parser can dereference pointers that live in the
cartridge address space:

- FatalError's ``arg0`` points at a ``struct prog_pos_info {char* file; int
  line;}`` (see ``pmdsky-debug/headers/types/common/common.h``); the ``file``
  string lives in the ROM, giving the original assert location.
- ``%s`` conversions in the recorded FatalError format string can be
  resolved against ROM string literals.

Overlays can share a load address, so when several segments contain an
address, reads prefer the segment that also contains the ``lr`` (caller)
address passed as ``prefer`` - the caller's binary is the most plausible
home of its own string literals.
"""

from __future__ import annotations

import re
import struct
from typing import Callable, Dict, List, Optional, Tuple

from .dump import CrashDump, HOOK_FATAL_ERROR

PRINTF_CONVERSION_RE = re.compile(
    r"%([-+ #0]*)(\*|\d+)?(?:\.(\*|\d+))?(hh|h|l|ll)?([diouxXcsp%])"
)

STRING_MAX_LEN = 256


class RomMapError(Exception):
    """Raised when the ROM cannot be used for dereferencing."""


class RomMap:
    """Address -> bytes mapping for the ROM's ARM9 code and overlays."""

    def __init__(self, rom_path, prefer: Optional[int] = None) -> None:
        try:
            from ndspy import rom as ndspy_rom  # type: ignore
        except ImportError as exc:
            raise RomMapError(
                "ndspy is required for --rom (install with: pip install ndspy): %s" % exc
            )
        self.prefer = prefer
        self.segments: List[Tuple[int, bytes]] = []
        rom = ndspy_rom.NintendoDSRom.fromFile(str(rom_path))
        arm9 = bytes(rom.arm9)
        if arm9 and rom.arm9RamAddress is not None:
            self.segments.append((rom.arm9RamAddress, arm9))
        try:
            overlays = rom.loadArm9Overlays()
        except Exception as exc:  # noqa: BLE001 - ndspy raises various types
            overlays = {}
            self.warnings = ["Could not load overlays: %s" % exc]
        else:
            self.warnings = []
        for overlay in overlays.values():
            if overlay.ramAddress is None:
                continue
            try:
                data = bytes(rom.files[overlay.fileID])
            except Exception:  # noqa: BLE001 - missing/unreadable file entry
                continue
            if data:
                self.segments.append((overlay.ramAddress, data))
        if not self.segments:
            raise RomMapError("no readable ARM9 segments found in the ROM")

    # -- raw reads --------------------------------------------------

    def _tail(self, addr: int, prefer: Optional[int]) -> Optional[bytes]:
        hits = [(start, data) for start, data in self.segments
                if start <= addr < start + len(data)]
        if not hits:
            return None
        if prefer is not None:
            for start, data in hits:
                if start <= prefer < start + len(data):
                    return data[addr - start:]
        return hits[0][1][addr - hits[0][0]:]

    def read(self, addr: int, size: int, prefer: Optional[int] = None) -> Optional[bytes]:
        tail = self._tail(addr, prefer if prefer is not None else self.prefer)
        if tail is None or len(tail) < size:
            return None
        return tail[:size]

    def read_u32(self, addr: int, prefer: Optional[int] = None) -> Optional[int]:
        raw = self.read(addr, 4, prefer)
        if raw is None:
            return None
        return struct.unpack("<I", raw)[0]

    def read_cstring(self, addr: int, prefer: Optional[int] = None,
                     max_len: int = STRING_MAX_LEN) -> Optional[str]:
        """Read a NUL-terminated string literal, rejecting implausible data.

        Returns ``None`` when the address is unmapped, no NUL appears within
        ``max_len`` bytes, or the bytes do not look like printable text.
        """
        tail = self._tail(addr, prefer if prefer is not None else self.prefer)
        if tail is None:
            return None
        window = tail[:max_len]
        nul = window.find(b"\x00")
        if nul < 0:
            return None
        raw = window[:nul]
        if not raw:
            return None
        try:
            text = raw.decode("ascii")
        except UnicodeDecodeError:
            return None
        if not all(ch == "\t" or ch == "\n" or 0x20 <= ord(ch) < 0x7F for ch in text):
            return None
        return text


def _render_int(value: int, conv: str) -> str:
    if conv in ("d", "i"):
        signed = value - (1 << 32) if value >= (1 << 31) else value
        return str(signed)
    if conv == "u":
        return str(value & 0xFFFFFFFF)
    if conv == "x":
        return "%x" % (value & 0xFFFFFFFF)
    if conv == "X":
        return "%X" % (value & 0xFFFFFFFF)
    if conv == "o":
        return "%o" % (value & 0xFFFFFFFF)
    return str(value & 0xFFFFFFFF)


def _apply_width_prec(text: str, conv: str, flags: str,
                      width: Optional[str], prec: Optional[str]) -> str:
    if prec is not None and prec.isdigit():
        digits = int(prec)
        if conv in "diouxX":
            negative = text.startswith("-")
            body = text[1:] if negative else text
            text = ("-" if negative else "") + body.rjust(digits, "0")
        elif conv == "s":
            text = text[:digits]
    if width is not None and width.isdigit():
        pad = int(width)
        if "-" in flags:
            text = text.ljust(pad)
        elif "0" in flags and conv in "diouxX":
            negative = text.startswith("-")
            body = text[1:] if negative else text
            text = ("-" if negative else "") + body.rjust(pad, "0")
        else:
            text = text.rjust(pad)
    return text


def format_message(fmt: str, args: List[int],
                   read_cstring: Callable[[int], Optional[str]]) -> Tuple[str, int]:
    """Best-effort printf formatting of the recorded FatalError format string.

    Only the two captured variadic arguments (``arg2``, ``arg3``) are
    available; conversions that need more are left marked as unresolved.
    ``%s`` arguments are dereferenced through ``read_cstring``.  Returns
    ``(text, unresolved_count)``.
    """
    out: List[str] = []
    pos = 0
    arg_index = 0
    unresolved = 0

    def next_arg() -> Optional[int]:
        nonlocal arg_index
        value = args[arg_index] if arg_index < len(args) else None
        arg_index += 1
        return value

    for match in PRINTF_CONVERSION_RE.finditer(fmt):
        out.append(fmt[pos:match.start()])
        pos = match.end()
        flags, width, prec, _length, conv = match.groups()
        if conv == "%":
            out.append("%")
            continue
        if width == "*":
            width_value = next_arg()
            width = str(width_value) if width_value is not None else None
        if prec == "*":
            prec_value = next_arg()
            prec = str(prec_value) if prec_value is not None else None
        value = next_arg()
        if value is None:
            unresolved += 1
            out.append(match.group(0) + "<arg %d unavailable>" % (arg_index - 1))
            continue
        if conv == "s":
            text = read_cstring(value)
            if text is None:
                unresolved += 1
                text = "<?0x%08X>" % value
        elif conv == "c":
            code = value & 0xFF
            text = chr(code) if 0x20 <= code < 0x7F else "\\x%02X" % code
        elif conv == "p":
            text = "0x%08X" % value
        else:
            text = _render_int(value, conv)
        text = _apply_width_prec(text, conv, flags or "", width, prec)
        out.append(text)
    out.append(fmt[pos:])
    return "".join(out), unresolved


def extract_rom_info(dump: CrashDump, rom: RomMap) -> Dict:
    """Dereference everything the ROM can tell us about this dump."""
    info: Dict = {"notes": []}
    notes: List[str] = info["notes"]

    if dump.hook_id == HOOK_FATAL_ERROR:
        prog_pos_ptr = dump.args[0]
        raw = rom.read(prog_pos_ptr, 8)
        if raw is not None:
            file_ptr, line = struct.unpack("<Ii", raw)
            name = rom.read_cstring(file_ptr)
            if name is not None:
                info["assert_file"] = name
                info["assert_line"] = line
            else:
                notes.append(
                    "prog_pos file pointer %s is not readable as a string "
                    "from the ROM." % hex(file_ptr)
                )
        else:
            notes.append(
                "arg0 (prog_pos pointer %s) does not point into the ROM; the "
                "assert location cannot be recovered (the struct may have "
                "lived on the stack or heap)." % hex(prog_pos_ptr)
            )

    if dump.msg:
        text, unresolved = format_message(
            dump.msg, dump.args[2:4], lambda addr: rom.read_cstring(addr)
        )
        info["formatted_message"] = text
        info["unresolved_conversions"] = unresolved

    return info
