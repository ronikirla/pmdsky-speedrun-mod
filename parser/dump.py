"""Crash dump record parsing for the PMD Sky speedrun mod.

The record layout mirrors ``src/crash_dump.h`` in the mod source tree: a
0x100-byte header followed by packed variable-length thread records (one per
thread, each with its own stack snapshot), all values little-endian.  The mod
writes the record into the backup EEPROM at offset 0xB6B0, which maps 1:1
onto the ``.sav`` file.

Version 4 records add per-thread saved registers and OS wait-queue linkage
(queue/mutex/link.prev/link.next/thread pointer) captured before the dump's
own card writes, plus a header SYS block (uplink counters, frame flags, queue
samples).  Only version 4 is supported: older records have a different
layout.

This module is intentionally dependency-free (standard library only).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Optional

# --- Constants mirrored from src/crash_dump.h -------------------------------

EEPROM_BASE = 0xB6B0        # CRASH_DUMP_EEPROM_BASE
RECORD_SIZE = 0x1000        # CRASH_DUMP_SIZE
HEADER_SIZE = 0x100         # CRASH_DUMP_HEADER_SIZE
MSG_MAX = 0x3F              # CRASH_DUMP_MSG_MAX (0x40-byte field incl. NUL)

THREAD_TABLE_OFFSET = 0x100            # first thread record
THREAD_RECORD_HEADER_SIZE = 0x74       # CRASH_DUMP_THREAD_RECORD_HEADER_SIZE
MAX_SNAPSHOT_BYTES = 0xDC              # CRASH_DUMP_MAX_SNAPSHOT_BYTES

MAGIC = 0x48535243          # 'CRSH' when read little-endian
MAGIC_BYTES = b"CRSH"
THREAD_MAGIC = 0x44524854   # 'THRD'
THREAD_MAGIC_BYTES = b"THRD"
RECORD_VERSION = 4          # CRASH_DUMP_VERSION

HOOK_FATAL_ERROR = 1        # CRASH_DUMP_HOOK_FATAL_ERROR
HOOK_OS_PANIC = 2           # CRASH_DUMP_HOOK_OS_PANIC
HOOK_MANUAL = 3             # CRASH_DUMP_HOOK_MANUAL (watchdog: L+R+X+Y)
HOOK_NAMES = {
    HOOK_FATAL_ERROR: "FatalError",
    HOOK_OS_PANIC: "OS_Panic",
    HOOK_MANUAL: "Manual trigger (L+R+X+Y)",
}

# enum os_thread_state (pmdsky-debug/headers/types/common/enums.h)
THREAD_STATE_NAMES = {
    0: "waiting",
    1: "ready",
    2: "terminated",
}

THREAD_FLAG_SP_VALID = 1    # sp is inside the recorded stack bounds
THREAD_FLAG_CURRENT = 2     # record 0: the current/crashing thread
THREAD_FLAG_SP_OUTSIDE = 4  # sp outside the bounds; snapshot from sp anyway

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
OFF_THREAD_COUNT = 0x06C
OFF_THREADS_WRITTEN = 0x070
OFF_CRASHING_INDEX = 0x074
OFF_TRIGGER_BUTTONS = 0x078
OFF_MSG = 0x07C             # msg at 0x07C..0x0BB (0x40 bytes)
# SYS block (captured before the dump's first card write), 0x0BC..0x0FB.
OFF_SYS_BUSY_TIMEOUTS = 0x0BC
OFF_SYS_LOCK_SKIPS = 0x0C0
OFF_SYS_LOCK_WAITS = 0x0C4
OFF_SYS_WAKE_COUNT = 0x0C8
OFF_SYS_FRAME_FLAGS = 0x0CC
OFF_SYS_QUEUE_SAMPLES = 0x0D0   # 3 x {ptr, head, tail} words
SYS_QUEUE_SAMPLES = 3
OFF_SYS_RESERVED = 0x0F4
OFF_COMPLETE = 0x0FC

THREAD_OFF_MAGIC = 0x00
THREAD_OFF_THREAD_ID = 0x04
THREAD_OFF_PRIORITY = 0x08
THREAD_OFF_PC = 0x0C
THREAD_OFF_LR = 0x10
THREAD_OFF_SP = 0x14
THREAD_OFF_STACK_START = 0x18
THREAD_OFF_STACK_END = 0x1C
THREAD_OFF_STATE = 0x20
THREAD_OFF_SNAPSHOT_BYTES = 0x24
THREAD_OFF_FLAGS = 0x28
THREAD_OFF_REGS = 0x2C          # r0..r12 at 0x2C..0x5F (13 words)
THREAD_OFF_QUEUE = 0x60
THREAD_OFF_MUTEX = 0x64
THREAD_OFF_LINK_PREV = 0x68
THREAD_OFF_LINK_NEXT = 0x6C
THREAD_OFF_THREAD_PTR = 0x70
THREAD_OFF_SNAPSHOT = 0x74

HEADER_WORD_COUNT = HEADER_SIZE // 4
THREAD_HEADER_WORD_COUNT = THREAD_RECORD_HEADER_SIZE // 4
# The checksum covers the whole header except its own word and the complete
# flag (written last, after everything else).
CHECKSUM_EXCLUDED_INDICES = {OFF_CHECKSUM // 4, OFF_COMPLETE // 4}

# struct held_buttons (src/custom_headers.h): bit order of the trigger
# bitmask recorded for manual triggers.
BUTTON_BITS = [
    (0, "A"), (1, "B"), (2, "Select"), (3, "Start"),
    (4, "Right"), (5, "Left"), (6, "Up"), (7, "Down"),
    (8, "R"), (9, "L"), (10, "X"), (11, "Y"),
]


def decode_buttons(raw: int) -> List[str]:
    """Decode the raw held_buttons bitfield into button names."""
    return [name for bit, name in BUTTON_BITS if raw & (1 << bit)]


class DumpError(Exception):
    """Raised when the input cannot be interpreted as a crash dump record."""


class CleanSaveError(DumpError):
    """The dump region is erased (all 0xFF): no crash has been recorded."""


@dataclass
class ThreadRecord:
    """One thread's record: metadata, registers, wait-queue linkage and a
    stack snapshot.

    ``pc``/``lr``/``sp`` are the thread's saved state: for record 0 (the
    current thread) they are the crash/trigger site; for the other threads
    they come from the thread's ``os_context`` at capture time.  ``queue``,
    ``mutex``, ``link_prev``, ``link_next`` and ``state`` were sampled before
    the dump's first card write (see ``CrashDumpEmit`` in the mod source).
    """

    index: int
    record_offset: int              # offset inside the 0x1000 record
    thread_id: int
    priority: int                   # thread::sorting_order
    pc: int
    lr: int
    sp: int
    stack_start: int                # low end of the thread's stack area
    stack_end: int                  # high end (exclusive)
    state: int
    snapshot_bytes: int
    flags: int
    regs: List[int]                 # r0..r12 (13 entries)
    queue: int                      # thread::queue (wait queue, 0 = NULL)
    mutex: int                      # thread::mutex (0 = none)
    link_prev: int                  # thread::link.prev (queue chain)
    link_next: int                  # thread::link.next (queue chain)
    thread_ptr: int                 # struct thread* (queue-chain key)
    snapshot: bytes                 # starts at absolute address `sp`

    @property
    def is_current(self) -> bool:
        return bool(self.flags & THREAD_FLAG_CURRENT)

    @property
    def sp_valid(self) -> bool:
        return bool(self.flags & THREAD_FLAG_SP_VALID)

    @property
    def sp_outside(self) -> bool:
        return bool(self.flags & THREAD_FLAG_SP_OUTSIDE)

    @property
    def state_name(self) -> str:
        return THREAD_STATE_NAMES.get(self.state, "unknown (0x%X)" % self.state)

    @property
    def known(self) -> bool:
        """False when no thread struct was found (id/priority/state unknown)."""
        return self.thread_id != 0xFFFFFFFF

    def stack_words(self) -> List[int]:
        return list(struct.unpack("<%dI" % (len(self.snapshot) // 4), self.snapshot))


@dataclass
class QueueSample:
    """One captured wait queue (os_thread_queue: head/tail of the chain)."""

    queue: int      # struct os_thread_queue*
    head: int       # first struct thread* in the chain (0 = empty/unknown)
    tail: int       # last struct thread* in the chain


@dataclass
class SysInfo:
    """The header SYS block: system state sampled before the dump's writes."""

    uplink_card_busy_timeouts: int
    uplink_card_lock_skips: int
    uplink_card_lock_waits: int
    mod_wake_count: int
    frame_flags_raw: int            # DAT_02003aac[0],[7],[9],[10] as bytes
    queue_samples: List[QueueSample]

    @property
    def frame_flags(self) -> Optional[dict]:
        """Decoded frame-sync flags, or None when the capture was unavailable."""
        if self.frame_flags_raw == 0xFFFFFFFF:
            return None
        raw = self.frame_flags_raw
        return {
            "flag0": raw & 0xFF,
            "flag7": (raw >> 8) & 0xFF,
            "flag9": (raw >> 16) & 0xFF,
            "flag10": (raw >> 24) & 0xFF,
        }


@dataclass
class CrashDump:
    """A parsed crash dump record.

    ``sp`` is the stack pointer captured at hook entry, which is also the
    absolute memory address the snapshot of thread record 0 starts at
    (``sp_base`` in ``src/crash_dump.h``).
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
    thread_count: int
    threads_written: int
    crashing_index: int
    trigger_buttons: int
    complete: bool
    sys: SysInfo
    threads: List[ThreadRecord]
    source_offset: int
    warnings: List[str] = field(default_factory=list)

    @property
    def hook_name(self) -> str:
        return HOOK_NAMES.get(self.hook_id, "Unknown (hook id %d)" % self.hook_id)

    @property
    def is_fatal_error(self) -> bool:
        return self.hook_id == HOOK_FATAL_ERROR

    @property
    def is_manual(self) -> bool:
        return self.hook_id == HOOK_MANUAL

    @property
    def checksum_ok(self) -> bool:
        return self.checksum_stored == self.checksum_computed

    @property
    def crashing_record(self) -> Optional[ThreadRecord]:
        if 0 <= self.crashing_index < len(self.threads):
            return self.threads[self.crashing_index]
        return None

    @property
    def stack(self) -> bytes:
        """Snapshot of the current thread's record (empty when missing)."""
        record = self.crashing_record
        return record.snapshot if record is not None else b""

    def stack_words(self) -> List[int]:
        """The current thread's snapshot as little-endian u32 words."""
        record = self.crashing_record
        return record.stack_words() if record is not None else []


def compute_checksum(record: bytes) -> int:
    """Sum of the header words (checksum and complete words excluded)."""
    words = struct.unpack_from("<%dI" % HEADER_WORD_COUNT, record, 0)
    total = 0
    for index, word in enumerate(words):
        if index in CHECKSUM_EXCLUDED_INDICES:
            continue
        total += word
    return total & 0xFFFFFFFF


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


def parse_thread_records(record: bytes, expected: int,
                         warnings: List[str]) -> List[ThreadRecord]:
    """Decode the packed thread records starting at ``THREAD_TABLE_OFFSET``.

    Stops early (with a warning) when a record's magic is missing or the data
    runs past the end of the record: that means the table was truncated or
    corrupted.
    """
    threads: List[ThreadRecord] = []
    offset = THREAD_TABLE_OFFSET
    for index in range(expected):
        if offset + THREAD_RECORD_HEADER_SIZE > RECORD_SIZE:
            warnings.append(
                "Thread table truncated: record %d would start at %s, past the "
                "end of the record." % (index, hex(offset))
            )
            break
        if struct.unpack_from("<I", record, offset + THREAD_OFF_MAGIC)[0] != THREAD_MAGIC:
            warnings.append(
                "Thread table truncated or corrupt: record %d at %s has no "
                "'THRD' magic." % (index, hex(offset))
            )
            break
        fields = struct.unpack_from("<%dI" % THREAD_HEADER_WORD_COUNT, record, offset)
        (magic, thread_id, priority, pc, lr, sp, stack_start, stack_end,
         state, snapshot_bytes, flags) = fields[:11]
        regs = list(fields[11:24])
        (queue, mutex, link_prev, link_next, thread_ptr) = fields[24:29]
        if snapshot_bytes % 4 != 0:
            warnings.append(
                "Thread %d: snapshot_bytes %d is not a multiple of 4; "
                "rounding down." % (index, snapshot_bytes)
            )
            snapshot_bytes &= ~3
        available = RECORD_SIZE - (offset + THREAD_RECORD_HEADER_SIZE)
        if snapshot_bytes > available:
            warnings.append(
                "Thread %d: snapshot of %s bytes runs past the end of the "
                "record; clamping." % (index, hex(snapshot_bytes))
            )
            snapshot_bytes = available & ~3
        snapshot = record[offset + THREAD_RECORD_HEADER_SIZE:
                          offset + THREAD_RECORD_HEADER_SIZE + snapshot_bytes]
        threads.append(ThreadRecord(
            index=index,
            record_offset=offset,
            thread_id=thread_id,
            priority=priority,
            pc=pc,
            lr=lr,
            sp=sp,
            stack_start=stack_start,
            stack_end=stack_end,
            state=state,
            snapshot_bytes=snapshot_bytes,
            flags=flags,
            regs=regs,
            queue=queue,
            mutex=mutex,
            link_prev=link_prev,
            link_next=link_next,
            thread_ptr=thread_ptr,
            snapshot=snapshot,
        ))
        offset += THREAD_RECORD_HEADER_SIZE + snapshot_bytes
    return threads


def parse_sys_block(record: bytes) -> SysInfo:
    """Decode the header SYS block (system state pre-dump-writes)."""
    words = struct.unpack_from("<16I", record, OFF_SYS_BUSY_TIMEOUTS)
    samples: List[QueueSample] = []
    for i in range(SYS_QUEUE_SAMPLES):
        q, head, tail = words[5 + i * 3:8 + i * 3]
        if q != 0:
            samples.append(QueueSample(queue=q, head=head, tail=tail))
    return SysInfo(
        uplink_card_busy_timeouts=words[0],
        uplink_card_lock_skips=words[1],
        uplink_card_lock_waits=words[2],
        mod_wake_count=words[3],
        frame_flags_raw=words[4],
        queue_samples=samples,
    )


def parse_record(record: bytes, source_offset: int = 0) -> CrashDump:
    """Decode a raw 0x1000-byte record into a :class:`CrashDump`.

    Validation problems (bad magic, checksum, truncated thread table) are
    collected into ``warnings`` instead of aborting: a partially corrupted
    record is still worth reporting.  A record with valid magic but a
    different version is rejected outright: only version 4 is supported.
    """
    if len(record) < RECORD_SIZE:
        raise DumpError(
            "Record too small: %d bytes (a record is %d bytes)"
            % (len(record), RECORD_SIZE)
        )

    warnings: List[str] = []
    words = struct.unpack_from("<%dI" % HEADER_WORD_COUNT, record, 0)

    magic = words[OFF_MAGIC // 4]
    record_version = words[OFF_VERSION // 4]
    if magic == MAGIC and record_version != RECORD_VERSION:
        raise DumpError(
            "Unsupported crash dump record version %d (this parser reads "
            "version %d only). Records written by older mod builds have a "
            "different layout and can no longer be decoded."
            % (record_version, RECORD_VERSION)
        )
    if magic != MAGIC:
        warnings.append(
            "Bad magic: expected 'CRSH' (%s), got %s. The record is likely corrupted."
            % (hex(MAGIC), hex(magic))
        )
        if record_version != RECORD_VERSION:
            warnings.append(
                "Unexpected record version %d (parser supports %d); fields may "
                "be misinterpreted." % (record_version, RECORD_VERSION)
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

    thread_count = words[OFF_THREAD_COUNT // 4]
    threads_written = words[OFF_THREADS_WRITTEN // 4]
    crashing_index = words[OFF_CRASHING_INDEX // 4]
    trigger_buttons = words[OFF_TRIGGER_BUTTONS // 4]
    complete = words[OFF_COMPLETE // 4] != 0
    sys_block = parse_sys_block(record)
    if not complete:
        warnings.append(
            "The complete flag is not set: the dump was interrupted while it "
            "was being written; fields and thread records may be partial."
        )
    if threads_written > thread_count:
        warnings.append(
            "threads_written (%d) exceeds thread_count (%d); the header is "
            "inconsistent." % (threads_written, thread_count)
        )
    if thread_count > 0 and threads_written == 0:
        warnings.append("thread_count is %d but no thread records were written."
                        % thread_count)

    threads = parse_thread_records(record, threads_written, warnings)
    if len(threads) < threads_written:
        warnings.append(
            "Only %d of the %d expected thread records could be read."
            % (len(threads), threads_written)
        )
    if not threads:
        warnings.append("No thread records found in the record.")
    if crashing_index >= max(1, len(threads)):
        warnings.append(
            "crashing_index %d is out of range (%d thread records); treating "
            "record 0 as the current thread." % (crashing_index, len(threads))
        )
        crashing_index = 0

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
        thread_count=thread_count,
        threads_written=threads_written,
        crashing_index=crashing_index,
        trigger_buttons=trigger_buttons,
        complete=complete,
        sys=sys_block,
        threads=threads,
        source_offset=source_offset,
        warnings=warnings,
    )


# --- Wait-queue chain analysis ---------------------------------------------
#
# The bug this analysis exists for: a direct wake (OS_WakeupThreadDirect) on a
# thread that was sleeping on a queue does NOT unlink it, so the stale node
# stays in that queue's chain and the thread's single link node gets rewritten
# by its next sleep elsewhere - splicing two queue lists together. Later
# OS_WakeupThread walks then lose waiters forever. The signature of that
# corruption shows up in the captured linkage: a thread unreachable from its
# queue's head, chains crossing between queues, or link pairs that do not
# reciprocate.


@dataclass
class QueueFinding:
    """One anomaly found while walking the captured wait-queue chains."""

    kind: str       # 'lost_waiter', 'cross_splice', 'broken_link', 'cycle'
    detail: str
    thread_ptrs: List[int] = field(default_factory=list)


@dataclass
class QueueAnalysis:
    """Result of analyze_thread_queues()."""

    findings: List[QueueFinding]
    # queue pointer -> thread_ptr chain as walked from the captured head
    chains: dict

    @property
    def clean(self) -> bool:
        return not self.findings


def analyze_thread_queues(dump: CrashDump) -> QueueAnalysis:
    """Walk the captured wait-queue chains and flag corruption signatures.

    Uses only data frozen before the dump's own card writes: per-thread
    ``queue``/``link_prev``/``link_next``/``thread_ptr`` and the SYS block's
    queue samples (queue pointer + chain head/tail).
    """
    findings: List[QueueFinding] = []
    by_ptr = {t.thread_ptr: t for t in dump.threads if t.thread_ptr}

    # 1. Internal consistency of every recorded link pair: if A.link_next == B
    #    then B.link_prev must == A and both must name the same queue.
    for t in dump.threads:
        checks = ((t.link_next, "next"), (t.link_prev, "prev"))
        for other_ptr, direction in checks:
            if not other_ptr:
                continue
            other = by_ptr.get(other_ptr)
            if other is None:
                findings.append(QueueFinding(
                    kind="broken_link",
                    detail=("thread %s: link.%s -> 0x%08X, which is not one of "
                            "the dumped threads (terminated, or the dump is "
                            "truncated)"
                            % (_thread_label(t), direction, other_ptr)),
                    thread_ptrs=[t.thread_ptr],
                ))
                continue
            back = other.link_prev if direction == "next" else other.link_next
            if back != t.thread_ptr:
                findings.append(QueueFinding(
                    kind="broken_link",
                    detail=("thread %s: link.%s -> %s but the back link is "
                            "0x%08X, expected 0x%08X (node rewritten by a "
                            "second sleep - the queue-splice signature)"
                            % (_thread_label(t), direction, _thread_label(other),
                               back, t.thread_ptr)),
                    thread_ptrs=[t.thread_ptr, other.thread_ptr],
                ))
            if t.queue and other.queue and t.queue != other.queue:
                findings.append(QueueFinding(
                    kind="cross_splice",
                    detail=("thread %s (queue 0x%08X) links to thread %s "
                            "(queue 0x%08X): two queue lists are spliced "
                            "together" % (_thread_label(t), t.queue,
                                          _thread_label(other), other.queue)),
                    thread_ptrs=[t.thread_ptr, other.thread_ptr],
                ))

    # 2. Walk each captured queue sample from its head and compare the reached
    #    chain against the threads that claim that queue.
    chains: dict = {}
    sampled = {s.queue: s for s in dump.sys.queue_samples}
    queues = set(sampled) | {t.queue for t in dump.threads if t.queue}
    for q in sorted(queues):
        members = [t for t in dump.threads if t.queue == q]
        sample = sampled.get(q)
        chain: List[int] = []
        seen = set()
        node = sample.head if sample else _chain_head(members)
        while node:
            if node in seen:
                findings.append(QueueFinding(
                    kind="cycle",
                    detail=("queue 0x%08X: chain loops back at thread "
                            "0x%08X" % (q, node)),
                    thread_ptrs=sorted(seen),
                ))
                break
            seen.add(node)
            chain.append(node)
            t = by_ptr.get(node)
            if t is None:
                findings.append(QueueFinding(
                    kind="broken_link",
                    detail=("queue 0x%08X: chain node 0x%08X is not one of "
                            "the dumped threads" % (q, node)),
                    thread_ptrs=[],
                ))
                break
            node = t.link_next
        chains[q] = chain

        lost = [t.thread_ptr for t in members if t.thread_ptr not in seen]
        if lost:
            findings.append(QueueFinding(
                kind="lost_waiter",
                detail=("queue 0x%08X: thread(s) %s claim this queue but are "
                        "unreachable from its head - the lost-waiter signature "
                        "of the stale-node bug"
                        % (q, ", ".join("0x%08X" % p for p in lost))),
                thread_ptrs=lost,
            ))
        if sample and chain and sample.tail and chain[-1] != sample.tail:
            findings.append(QueueFinding(
                kind="broken_link",
                detail=("queue 0x%08X: chain ends at 0x%08X but the captured "
                        "tail is 0x%08X" % (q, chain[-1], sample.tail)),
                thread_ptrs=[chain[-1]],
            ))

    return QueueAnalysis(findings=findings, chains=chains)


def _thread_label(t: ThreadRecord) -> str:
    if t.known:
        return "id %d (0x%08X)" % (t.thread_id, t.thread_ptr)
    return "0x%08X" % t.thread_ptr


def _chain_head(members: List[ThreadRecord]) -> int:
    """Head of the chain among threads claiming a queue (no sample available)."""
    ptrs = {t.thread_ptr for t in members}
    for t in members:
        if t.link_prev not in ptrs:
            return t.thread_ptr
    return members[0].thread_ptr if members else 0
