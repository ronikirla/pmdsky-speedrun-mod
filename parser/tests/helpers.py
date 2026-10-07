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


def make_thread_record(thread_id: int = 1,
                       priority: int = 30,
                       pc: int = 0x200C2E4,
                       lr: int = 0x20492B4,
                       sp: int = 0x23EF0000,
                       stack_start: int = 0x23EF0000,
                       stack_end: int = 0x23EF0800,
                       state: int = 0,
                       flags: int = None,
                       snapshot_words=None,
                       snapshot_bytes: int = None,
                       snapshot_bytes_field: int = None,
                       magic: int = dump_mod.THREAD_MAGIC) -> bytes:
    """Build one packed thread record ('THRD' header + stack snapshot).

    ``snapshot_bytes_field`` overrides the length stored in the record header
    (for truncation tests) while the actual payload keeps its real size.
    """
    if snapshot_words is None:
        snapshot_words = [0] * 8
    snap = b"".join(struct.pack("<I", word & 0xFFFFFFFF) for word in snapshot_words)
    if snapshot_bytes is not None:
        snap = snap[:snapshot_bytes].ljust(snapshot_bytes, b"\x00")
    if flags is None:
        flags = dump_mod.THREAD_FLAG_SP_VALID
    stored = len(snap) if snapshot_bytes_field is None else snapshot_bytes_field
    header = struct.pack("<11I", magic, thread_id, priority, pc, lr, sp,
                         stack_start, stack_end, state, stored, flags)
    return header + snap


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
                checksum: int = None,
                threads=None,
                thread_count: int = None,
                threads_written: int = None,
                crashing_index: int = 0,
                trigger_buttons: int = 0,
                complete: bool = True,
                stack_start: int = 0x23EF0000,
                stack_end: int = 0x23EF0800) -> bytes:
    """Build a synthetic 0x1000-byte crash dump record (format version 3).

    ``threads`` is a list of raw thread record bytes (see make_thread_record);
    by default one record is generated for the current thread, using
    ``stack_words`` as its snapshot.
    """
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

    if threads is None:
        threads = [make_thread_record(
            pc=pc, lr=lr, sp=sp, stack_start=stack_start, stack_end=stack_end,
            flags=dump_mod.THREAD_FLAG_SP_VALID | dump_mod.THREAD_FLAG_CURRENT,
            snapshot_words=stack_words if stack_words is not None else [0] * 8,
        )]
    body = b"".join(threads)
    if thread_count is None:
        thread_count = len(threads)
    if threads_written is None:
        threads_written = len(threads)
    put(dump_mod.OFF_THREAD_COUNT, thread_count)
    put(dump_mod.OFF_THREADS_WRITTEN, threads_written)
    put(dump_mod.OFF_CRASHING_INDEX, crashing_index)
    put(dump_mod.OFF_TRIGGER_BUTTONS, trigger_buttons)
    put(dump_mod.OFF_COMPLETE, 1 if complete else 0)

    if checksum is None:
        checksum = dump_mod.compute_checksum(bytes(header))
    put(dump_mod.OFF_CHECKSUM, checksum)

    record = bytes(header) + body
    if len(record) < dump_mod.RECORD_SIZE:
        record += bytes(dump_mod.RECORD_SIZE - len(record))
    else:
        record = record[:dump_mod.RECORD_SIZE]
    return record


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
