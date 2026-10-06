"""Shared helpers for the parser test suite."""

from __future__ import annotations

import sys
from pathlib import Path

# Make the workspace root (parent of the parser package) importable no matter
# where the tests are discovered from.
_ROOT = Path(__file__).resolve().parents[2]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

import struct  # noqa: E402

from parser import dump as dump_mod  # noqa: E402


def make_record(hook_id: int = dump_mod.HOOK_FATAL_ERROR,
                pc: int = 0x200C2E4,
                lr: int = 0x20492B4,
                sp: int = 0x23EF0000,
                cpsr: int = 0x60000013,
                tick: int = 0x1234,
                regs=None,
                args=None,
                msg: bytes = b"test %d",
                stack_words=None,
                magic: int = dump_mod.MAGIC,
                version: int = dump_mod.RECORD_VERSION,
                msg_len: int = None,
                checksum: int = None) -> bytes:
    """Build a synthetic 0x1000-byte crash dump record."""
    header = bytearray(dump_mod.HEADER_SIZE)

    def put(offset: int, value: int) -> None:
        struct.pack_into("<I", header, offset, value & 0xFFFFFFFF)

    put(dump_mod.OFF_MAGIC, magic)
    put(dump_mod.OFF_VERSION, version)
    put(dump_mod.OFF_HOOK_ID, hook_id)
    put(dump_mod.OFF_TICK, tick)
    put(dump_mod.OFF_PC, pc)
    put(dump_mod.OFF_LR, lr)
    put(dump_mod.OFF_SP, sp)
    put(dump_mod.OFF_CPSR, cpsr)
    if regs is None:
        regs = [0x11111100 + i for i in range(13)]
    for i, value in enumerate(regs[:13]):
        put(dump_mod.OFF_R0 + 4 * i, value)
    if args is None:
        args = list(regs[:4])
    for i, value in enumerate(args[:4]):
        put(dump_mod.OFF_ARG0 + 4 * i, value)
    if msg is not None:
        put(dump_mod.OFF_MSG_LEN, msg_len if msg_len is not None else len(msg))
        header[dump_mod.OFF_MSG:dump_mod.OFF_MSG + len(msg)] = msg
    if checksum is None:
        checksum = dump_mod.compute_checksum(bytes(header))
    put(dump_mod.OFF_CHECKSUM, checksum)

    stack = bytearray(dump_mod.STACK_SIZE)
    if stack_words:
        for i, word in enumerate(stack_words):
            if 4 * i + 4 <= dump_mod.STACK_SIZE:
                struct.pack_into("<I", stack, 4 * i, word & 0xFFFFFFFF)
    return bytes(header) + bytes(stack)


def make_save(record: bytes, offset: int = dump_mod.EEPROM_BASE,
              size: int = 0x20000, filler: int = 0xFF) -> bytes:
    """A save file with the record placed at ``offset``."""
    blob = bytearray([filler] * size)
    blob[offset:offset + len(record)] = record
    return bytes(blob)


def symbols_dir() -> Path:
    """The real pmdsky-debug symbols directory, if present."""
    candidate = _ROOT / "pmdsky-debug" / "symbols"
    return candidate if (candidate / "arm9.yml").exists() else None
