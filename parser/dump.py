"""Crash dump record parsing for the PMD Sky speedrun mod.

The record layout mirrors ``src/crash_dump.h`` in the mod source tree: a
0x200-byte fixed header followed by a 0xE00-byte stack snapshot, all values
little-endian.  The mod writes the record into the backup EEPROM at offset
0xB6B0, which maps 1:1 onto the ``.sav`` file.

This module is intentionally dependency-free (standard library only).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Optional

# --- Constants mirrored from src/crash_dump.h -------------------------------

EEPROM_BASE = 0xB6B0        # CRASH_DUMP_EEPROM_BASE
RECORD_SIZE = 0x1000        # CRASH_DUMP_SIZE
HEADER_SIZE = 0x200         # CRASH_DUMP_HEADER_SIZE
STACK_OFFSET = 0x200        # CRASH_DUMP_STACK_OFFSET
STACK_SIZE = 0xE00          # CRASH_DUMP_STACK_SIZE

MAGIC = 0x48535243          # 'CRSH' when read little-endian
MAGIC_BYTES = b"CRSH"
RECORD_VERSION = 2          # CRASH_DUMP_VERSION

HOOK_FATAL_ERROR = 1        # CRASH_DUMP_HOOK_FATAL_ERROR
HOOK_OS_PANIC = 2           # CRASH_DUMP_HOOK_OS_PANIC
HOOK_NAMES = {
    HOOK_FATAL_ERROR: "FatalError",
    HOOK_OS_PANIC: "OS_Panic",
}

OFF_MAGIC = 0x000
OFF_VERSION = 0x004
OFF_HOOK_ID = 0x008
OFF_TICK = 0x00C
OFF_PC = 0x010
OFF_LR = 0x014
OFF_SP = 0x018
OFF_CPSR = 0x01C
OFF_R0 = 0x020              # r0..r12 at 0x020..0x050
OFF_CHECKSUM = 0x054
OFF_ARG0 = 0x058            # arg0..arg3 at 0x058..0x064
OFF_MSG_LEN = 0x068
OFF_MSG = 0x06C
MSG_MAX = HEADER_SIZE - OFF_MSG - 1   # CRASH_DUMP_MSG_MAX (0x193)

HEADER_WORD_COUNT = HEADER_SIZE // 4
STACK_WORD_COUNT = STACK_SIZE // 4
CHECKSUM_WORD_COUNT = OFF_CHECKSUM // 4


class DumpError(Exception):
    """Raised when the input cannot be interpreted as a crash dump record."""


class CleanSaveError(DumpError):
    """The dump region is erased (all 0xFF): no crash has been recorded."""


@dataclass
class CrashDump:
    """A parsed crash dump record.

    ``sp`` is the stack pointer captured at hook entry, which is also the
    absolute memory address the stack snapshot starts at (``sp_base`` in
    ``src/crash_dump.h``).
    """

    hook_id: int
    record_version: int
    tick: int
    pc: int
    lr: int
    sp: int
    cpsr: int
    regs: List[int]                   # r0..r12 (13 entries)
    checksum_stored: int
    checksum_computed: int
    args: List[int]                   # arg0..arg3
    msg_len: int
    msg: str
    stack: bytes                      # 0xE00 bytes, starts at absolute address `sp`
    source_offset: int
    warnings: List[str] = field(default_factory=list)

    @property
    def hook_name(self) -> str:
        return HOOK_NAMES.get(self.hook_id, "Unknown (hook id %d)" % self.hook_id)

    @property
    def is_fatal_error(self) -> bool:
        return self.hook_id == HOOK_FATAL_ERROR

    @property
    def checksum_ok(self) -> bool:
        return self.checksum_stored == self.checksum_computed

    def stack_words(self) -> List[int]:
        """The stack snapshot as 0x380 little-endian u32 words."""
        return list(struct.unpack("<%dI" % STACK_WORD_COUNT, self.stack))


def compute_checksum(record: bytes) -> int:
    """Sum of the header words at 0x000..0x053 (checksum field excluded)."""
    words = struct.unpack_from("<%dI" % CHECKSUM_WORD_COUNT, record, 0)
    return sum(words) & 0xFFFFFFFF


def _is_erased(blob: bytes, value: int = 0xFF) -> bool:
    return len(blob) > 0 and blob.count(value) == len(blob)


@dataclass
class LocatedRecord:
    """A raw record slice plus where it came from."""

    data: bytes
    offset: int
    warnings: List[str] = field(default_factory=list)


def locate_record(data: bytes, offset: Optional[int] = None) -> LocatedRecord:
    """Find the 0x1000-byte crash dump record inside a save file (or raw dump).

    Resolution order:

    1. ``offset`` given explicitly -> slice there, no questions asked.
    2. Input is exactly 0x1000 bytes -> treat it as a bare record that was
       extracted from a save.
    3. File is large enough -> use the default EEPROM location (0xB6B0).
       An all-0xFF region there means "no crash recorded".
    4. Otherwise scan the whole file for a ``'CRSH'`` record whose checksum
       validates (tolerates saves with unexpected layouts).

    Raises :class:`CleanSaveError` when the save simply has no crash dump and
    :class:`DumpError` when no plausible record can be found at all.
    """
    warnings: List[str] = []

    if offset is not None:
        if offset < 0 or offset + RECORD_SIZE > len(data):
            raise DumpError(
                "Requested offset %s with record size %s exceeds file size %d bytes"
                % (hex(offset), hex(RECORD_SIZE), len(data))
            )
        return LocatedRecord(data[offset:offset + RECORD_SIZE], offset, warnings)

    if len(data) == RECORD_SIZE:
        if _is_erased(data) or _is_erased(data, 0x00):
            raise CleanSaveError(
                "input is a bare 0x1000-byte record that is entirely erased "
                "(all 0xFF / all 0x00)"
            )
        warnings.append(
            "Input is exactly 0x1000 bytes: treating it as a bare crash dump record."
        )
        return LocatedRecord(data, 0, warnings)

    default_end = EEPROM_BASE + RECORD_SIZE
    if len(data) >= default_end:
        region = data[EEPROM_BASE:default_end]
        if _is_erased(region):
            raise CleanSaveError(
                "the crash dump region at file offset %s is erased (all 0xFF); "
                "the game has not crashed (or the dump was already consumed)"
                % hex(EEPROM_BASE)
            )
        if struct.unpack_from("<I", region, OFF_MAGIC)[0] == MAGIC:
            return LocatedRecord(region, EEPROM_BASE, warnings)
        warnings.append(
            "No 'CRSH' magic at the default offset %s; scanning the file instead."
            % hex(EEPROM_BASE)
        )

    # Scan for a record with valid magic and checksum at any alignment.
    idx = data.find(MAGIC_BYTES)
    while idx != -1:
        if idx + RECORD_SIZE <= len(data):
            candidate = data[idx:idx + RECORD_SIZE]
            if compute_checksum(candidate) == struct.unpack_from(
                "<I", candidate, OFF_CHECKSUM
            )[0]:
                if idx != EEPROM_BASE:
                    warnings.append(
                        "Crash dump record found at non-default offset %s "
                        "(expected %s)." % (hex(idx), hex(EEPROM_BASE))
                    )
                return LocatedRecord(candidate, idx, warnings)
        idx = data.find(MAGIC_BYTES, idx + 1)

    if len(data) < default_end:
        raise DumpError(
            "No crash dump record found. The file is %d bytes, too small for the "
            "default record location (offset %s + %s bytes), and no 'CRSH' record "
            "with a valid checksum exists anywhere in the file."
            % (len(data), hex(EEPROM_BASE), hex(RECORD_SIZE))
        )
    raise DumpError(
        "No crash dump record found. The region at the default offset %s holds "
        "data that is neither a valid record (magic mismatch) nor erased flash, "
        "and no 'CRSH' record with a valid checksum exists anywhere in the file."
        % hex(EEPROM_BASE)
    )


def parse_record(record: bytes, source_offset: int = 0) -> CrashDump:
    """Decode a raw 0x1000-byte record into a :class:`CrashDump`.

    Validation problems (bad magic, version or checksum) are collected into
    ``warnings`` instead of aborting: a partially corrupted record is still
    worth reporting.
    """
    if len(record) < RECORD_SIZE:
        raise DumpError(
            "Record too small: %d bytes (a record is %d bytes)"
            % (len(record), RECORD_SIZE)
        )

    warnings: List[str] = []
    words = struct.unpack_from("<%dI" % HEADER_WORD_COUNT, record, 0)

    magic = words[OFF_MAGIC // 4]
    if magic != MAGIC:
        warnings.append(
            "Bad magic: expected 'CRSH' (%s), got %s. The record is likely corrupted."
            % (hex(MAGIC), hex(magic))
        )

    record_version = words[OFF_VERSION // 4]
    if record_version != RECORD_VERSION:
        warnings.append(
            "Unexpected record version %d (parser supports %d); fields may be "
            "misinterpreted." % (record_version, RECORD_VERSION)
        )

    hook_id = words[OFF_HOOK_ID // 4]
    if hook_id not in HOOK_NAMES:
        warnings.append("Unknown hook id %d." % hook_id)

    tick = words[OFF_TICK // 4]
    pc = words[OFF_PC // 4]
    lr = words[OFF_LR // 4]
    sp = words[OFF_SP // 4]
    cpsr = words[OFF_CPSR // 4]
    regs = list(words[OFF_R0 // 4:OFF_R0 // 4 + 13])

    checksum_stored = words[OFF_CHECKSUM // 4]
    checksum_computed = compute_checksum(record)
    if checksum_stored != checksum_computed:
        warnings.append(
            "Checksum mismatch: stored %s, computed %s. The record is likely "
            "corrupted; treat all fields with suspicion."
            % (hex(checksum_stored), hex(checksum_computed))
        )

    args = list(words[OFF_ARG0 // 4:OFF_ARG0 // 4 + 4])

    msg_len = words[OFF_MSG_LEN // 4]
    if msg_len > MSG_MAX:
        warnings.append(
            "Message length %d exceeds the maximum of %d bytes; clamping."
            % (msg_len, MSG_MAX)
        )
        msg_len = MSG_MAX
    msg_bytes = record[OFF_MSG:OFF_MSG + msg_len]
    try:
        msg = msg_bytes.decode("ascii")
    except UnicodeDecodeError:
        msg = msg_bytes.decode("latin-1")
        warnings.append("Message contains non-ASCII bytes; decoded as latin-1.")
    if msg_len < MSG_MAX and record[OFF_MSG + msg_len] != 0x00:
        warnings.append(
            "Message is not NUL-terminated after %d bytes; it may be corrupt." % msg_len
        )

    stack = record[STACK_OFFSET:STACK_OFFSET + STACK_SIZE]

    return CrashDump(
        hook_id=hook_id,
        record_version=record_version,
        tick=tick,
        pc=pc,
        lr=lr,
        sp=sp,
        cpsr=cpsr,
        regs=regs,
        checksum_stored=checksum_stored,
        checksum_computed=checksum_computed,
        args=args,
        msg_len=msg_len,
        msg=msg,
        stack=stack,
        source_offset=source_offset,
        warnings=warnings,
    )
