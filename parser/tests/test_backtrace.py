"""Backtrace reconstruction tests."""
from __future__ import annotations

import unittest

try:
    from .helpers import make_record, make_thread_record, symbols_dir
except ImportError:  # running the file directly
    from helpers import make_record, make_thread_record, symbols_dir  # type: ignore

from parser import backtrace
from parser import dump as dump_mod
from parser import symtab


def _table_with_stub_symbols():
    """A minimal table: real pmdsky symbols if available, else bare."""
    if symbols_dir() is not None and symtab.yaml is not None:
        db = symtab.SymbolDb.load(symbols_dir())
        table = db.table_for("EU")
    else:
        table = symtab.SymbolTable("EU", [])
    table.blocks.extend(symtab.builtin_mod_blocks("EU"))
    return table


_REAL_SYMBOLS = symbols_dir() is not None and symtab.yaml is not None


class BacktraceTests(unittest.TestCase):
    def test_pc_and_lr_frames(self):
        record = make_record(lr=0x20492B5)  # bit 0 set -> Thumb
        dump = dump_mod.parse_record(record)
        table = _table_with_stub_symbols()
        frames, truncated = backtrace.build_backtrace(dump, table)
        self.assertFalse(truncated)
        self.assertEqual(frames[0].origin, "pc")
        self.assertEqual(frames[0].address, 0x200C2E4)
        self.assertFalse(frames[0].thumb)
        self.assertEqual(frames[1].origin, "lr")
        self.assertEqual(frames[1].address, 0x20492B4)
        self.assertTrue(frames[1].thumb)
        # The real FatalError symbol resolves with real symbol tables.
        if _REAL_SYMBOLS:
            self.assertEqual(frames[0].candidates[0].symbol.name, "FatalError")

    def test_spill_words_are_skipped(self):
        # A code address placed among the spilled words (0..3 for FatalError)
        # must not become a frame.
        record = make_record(stack_words=[0x200C364, 0x200C364, 0x200C364, 0x200C364])
        dump = dump_mod.parse_record(record)
        table = _table_with_stub_symbols()
        frames, _ = backtrace.build_backtrace(dump, table)
        self.assertTrue(all(f.origin in ("pc", "lr") for f in frames))

    def test_manual_trigger_has_no_spill_skip(self):
        # The manual trigger has no hook stub, so a code address in the first
        # snapshot words is a legitimate frame.
        record = make_record(hook_id=dump_mod.HOOK_MANUAL, msg=b"",
                             stack_words=[0x20799F4])
        dump = dump_mod.parse_record(record)
        table = _table_with_stub_symbols()
        frames, _ = backtrace.build_backtrace(dump, table)
        stack_frames = [f for f in frames if f.origin == "stack"]
        self.assertEqual(len(stack_frames), 1)
        self.assertEqual(stack_frames[0].address, 0x20799F4)

    def test_stack_frames_found_and_deduped(self):
        # OS_ExitThread (EU 0x020799F4) placed twice on the caller stack.
        words = [0] * 8 + [0x20799F4, 0x20799F4]
        record = make_record(stack_words=words)
        dump = dump_mod.parse_record(record)
        table = _table_with_stub_symbols()
        frames, _ = backtrace.build_backtrace(dump, table)
        stack_frames = [f for f in frames if f.origin == "stack"]
        self.assertEqual(len(stack_frames), 1)
        self.assertEqual(stack_frames[0].address, 0x20799F4)
        self.assertEqual(stack_frames[0].stack_offset, 8 * 4)
        if _REAL_SYMBOLS:
            self.assertEqual(stack_frames[0].candidates[0].symbol.name, "OS_ExitThread")

    def test_max_frames_cap(self):
        words = [0x20799F4] * 2 + [0x200C364 + 2 * i for i in range(100)]
        record = make_record(stack_words=words)
        dump = dump_mod.parse_record(record)
        table = _table_with_stub_symbols()
        frames, truncated = backtrace.build_backtrace(dump, table, max_frames=5)
        self.assertEqual(len(frames), 5)
        self.assertTrue(truncated)

    def test_data_pointers_are_not_frames(self):
        # A data symbol (speedrun_hud_strings area, mod region) whose only
        # candidates are non-function symbols is skipped when the table
        # knows it is not a function.
        record = make_record()
        dump = dump_mod.parse_record(record)
        table = symtab.SymbolTable("EU", [])
        table.blocks.extend(symtab.builtin_mod_blocks("EU", [
            symtab.Symbol(name="SOME_DATA", address=0x023DC12C, length=0x100,
                          kind="data", block="mod symbols", file="mod"),
        ]))
        words = [0] * 4 + [0x23DC12C]
        record = make_record(stack_words=words)
        dump = dump_mod.parse_record(record)
        frames, _ = backtrace.build_backtrace(dump, table)
        self.assertTrue(all(f.origin in ("pc", "lr") for f in frames))

    def test_without_table_uses_fallback_ranges(self):
        record = make_record(stack_words=[0] * 4 + [0x20799F4])
        dump = dump_mod.parse_record(record)
        frames, _ = backtrace.build_backtrace(dump, None)
        self.assertEqual(frames[0].address, 0x200C2E4)
        self.assertEqual(frames[1].address, 0x20492B4)
        self.assertEqual(frames[2].address, 0x20799F4)


class ThreadBacktraceTests(unittest.TestCase):
    def _dump_with_threads(self, threads):
        return dump_mod.parse_record(make_record(threads=threads, msg=b""))

    def test_thread_frames_from_saved_pc_lr(self):
        other = make_thread_record(thread_id=3, priority=30,
                                   pc=0x2079C30, lr=0x207A0E4,
                                   sp=0x23EE0100, stack_start=0x23EE0000,
                                   stack_end=0x23EE0400,
                                   snapshot_words=[0x20799F4])
        dump = self._dump_with_threads([make_thread_record(), other])
        table = _table_with_stub_symbols()
        frames, truncated = backtrace.build_thread_backtrace(dump.threads[1], table)
        self.assertFalse(truncated)
        self.assertEqual(frames[0].origin, "pc")
        self.assertEqual(frames[0].address, 0x2079C30)
        self.assertEqual(frames[1].origin, "lr")
        self.assertEqual(frames[1].address, 0x207A0E4)
        self.assertEqual(frames[2].origin, "stack")
        self.assertEqual(frames[2].address, 0x20799F4)
        self.assertEqual(frames[2].stack_address, 0x23EE0100)
        if _REAL_SYMBOLS:
            self.assertEqual(frames[0].candidates[0].symbol.name, "OS_SleepThread")

    def test_thread_snapshot_has_no_spill_skip(self):
        # A code address in the first snapshot word is a frame for a
        # non-current thread (no hook stub spilled registers there).
        other = make_thread_record(pc=0x2079C30, lr=0x207A0E4,
                                   snapshot_words=[0x20799F4])
        dump = self._dump_with_threads([make_thread_record(), other])
        table = _table_with_stub_symbols()
        frames, _ = backtrace.build_thread_backtrace(dump.threads[1], table)
        self.assertEqual(frames[2].address, 0x20799F4)

    def test_current_thread_record_uses_hook_skip(self):
        # The current thread's record is scanned with the hook-aware skip
        # (FatalError: 4 spill words) when the hook id is passed in.
        current = make_thread_record(
            flags=dump_mod.THREAD_FLAG_SP_VALID | dump_mod.THREAD_FLAG_CURRENT,
            snapshot_words=[0x200C364, 0x200C364, 0x200C364, 0x200C364, 0x20799F4])
        dump = self._dump_with_threads([current])
        table = _table_with_stub_symbols()
        frames, _ = backtrace.build_thread_backtrace(
            dump.threads[0], table, hook_id=dump.hook_id)
        stack_frames = [f for f in frames if f.origin == "stack"]
        self.assertEqual(len(stack_frames), 1)
        self.assertEqual(stack_frames[0].stack_offset, 4 * 4)


if __name__ == "__main__":
    unittest.main()
