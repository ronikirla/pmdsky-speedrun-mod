"""Crash dump record parsing tests."""
from __future__ import annotations

import unittest

try:
    from .helpers import make_record, make_save, make_thread_record
except ImportError:  # running the file directly
    from helpers import make_record, make_save, make_thread_record  # type: ignore

from parser import dump as dump_mod


class ParseRecordTests(unittest.TestCase):
    def test_roundtrip(self):
        dump = dump_mod.parse_record(make_record())
        self.assertEqual(dump.hook_id, dump_mod.HOOK_FATAL_ERROR)
        self.assertEqual(dump.record_version, dump_mod.RECORD_VERSION)
        self.assertEqual(dump.pc, 0x200C2E4)
        self.assertEqual(dump.lr, 0x20492B4)
        self.assertEqual(dump.sp, 0x23EF0000)
        self.assertEqual(dump.cpsr, 0x60000013)
        self.assertEqual(dump.tick, 0x1234)
        self.assertEqual(dump.regs[0], 0x11111100)
        self.assertEqual(dump.regs[12], 0x1111110C)
        self.assertEqual(dump.msg, "test %d")
        self.assertEqual(dump.args[2], 0x11111102)
        self.assertTrue(dump.checksum_ok)
        self.assertTrue(dump.complete)
        self.assertEqual(dump.thread_count, 1)
        self.assertEqual(dump.threads_written, 1)
        self.assertEqual(dump.crashing_index, 0)
        self.assertEqual(dump.warnings, [])
        self.assertEqual(len(dump.threads), 1)
        record = dump.threads[0]
        self.assertTrue(record.is_current)
        self.assertTrue(record.sp_valid)
        self.assertEqual(record.pc, 0x200C2E4)
        self.assertEqual(dump.stack, record.snapshot)
        self.assertEqual(dump.stack_words()[0], 0)

    def test_os_panic_hook(self):
        dump = dump_mod.parse_record(make_record(hook_id=dump_mod.HOOK_OS_PANIC, msg=b""))
        self.assertEqual(dump.hook_name, "OS_Panic")
        self.assertFalse(dump.is_fatal_error)
        self.assertEqual(dump.msg, "")

    def test_manual_trigger(self):
        # L+R+X+Y held = bits 8..11 of struct held_buttons.
        dump = dump_mod.parse_record(make_record(hook_id=dump_mod.HOOK_MANUAL, msg=b"",
                                                 trigger_buttons=0x0F00))
        self.assertEqual(dump.hook_name, "Manual trigger (L+R+X+Y)")
        self.assertTrue(dump.is_manual)
        self.assertEqual(dump.msg, "")
        self.assertEqual(dump_mod.decode_buttons(dump.trigger_buttons),
                         ["R", "L", "X", "Y"])

    def test_checksum_mismatch_warns(self):
        record = bytearray(make_record())
        record[0x10] ^= 0xFF  # corrupt the pc word
        dump = dump_mod.parse_record(bytes(record))
        self.assertFalse(dump.checksum_ok)
        self.assertTrue(any("Checksum mismatch" in w for w in dump.warnings))
        self.assertEqual(dump.pc, 0x200C2E4 ^ 0xFF)  # still parsed

    def test_bad_magic_warns(self):
        dump = dump_mod.parse_record(make_record(magic=0x12345678))
        self.assertTrue(any("Bad magic" in w for w in dump.warnings))

    def test_unsupported_version_rejected(self):
        # Valid magic but a version this parser does not know: old or foreign
        # format, reject instead of misparsing it.
        with self.assertRaises(dump_mod.DumpError):
            dump_mod.parse_record(make_record(version=99))

    def test_v3_record_rejected(self):
        # Version 3 (the previous format) is no longer supported.
        with self.assertRaises(dump_mod.DumpError) as ctx:
            dump_mod.parse_record(make_record(version=3))
        self.assertIn("version 3", str(ctx.exception))

    def test_v2_record_rejected(self):
        with self.assertRaises(dump_mod.DumpError) as ctx:
            dump_mod.parse_record(make_record(version=2))
        self.assertIn("version 2", str(ctx.exception))

    def test_unknown_hook_warns(self):
        dump = dump_mod.parse_record(make_record(hook_id=7))
        self.assertTrue(any("Unknown hook id 7" in w for w in dump.warnings))

    def test_msg_len_clamped(self):
        record = make_record(msg=b"x" * 8, msg_len=dump_mod.MSG_MAX + 10)
        dump = dump_mod.parse_record(record)
        self.assertTrue(any("clamping" in w for w in dump.warnings))
        self.assertEqual(dump.msg_len, dump_mod.MSG_MAX)
        self.assertEqual(len(dump.msg), dump_mod.MSG_MAX)

    def test_non_ascii_message(self):
        dump = dump_mod.parse_record(make_record(msg=b"caf\xe9"))
        self.assertTrue(any("latin-1" in w for w in dump.warnings))
        self.assertEqual(dump.msg, "caf\xe9")


class ThreadTableTests(unittest.TestCase):
    def test_multiple_threads(self):
        current = make_thread_record(
            thread_id=7, priority=5, pc=0x23D8D64, lr=0x23D88F0,
            flags=dump_mod.THREAD_FLAG_SP_VALID | dump_mod.THREAD_FLAG_CURRENT,
            snapshot_words=[0x10, 0x20])
        other = make_thread_record(
            thread_id=3, priority=30, pc=0x2079C30, lr=0x207A0E4,
            sp=0x23EE0100, stack_start=0x23EE0000, stack_end=0x23EE0400,
            state=1, snapshot_words=[0x200C364, 0x20799F4])
        dump = dump_mod.parse_record(make_record(threads=[current, other], msg=b""))
        self.assertEqual(dump.thread_count, 2)
        self.assertEqual(dump.threads_written, 2)
        self.assertEqual(len(dump.threads), 2)
        self.assertEqual(dump.warnings, [])
        self.assertTrue(dump.threads[0].is_current)
        self.assertEqual(dump.threads[0].thread_id, 7)
        self.assertEqual(dump.threads[0].priority, 5)
        self.assertEqual(dump.threads[1].thread_id, 3)
        self.assertEqual(dump.threads[1].state_name, "ready")
        self.assertEqual(dump.threads[1].snapshot_bytes, 8)
        self.assertEqual(dump.threads[1].stack_words(), [0x200C364, 0x20799F4])
        self.assertEqual(dump.threads[1].stack_start, 0x23EE0000)
        self.assertEqual(dump.threads[1].stack_end, 0x23EE0400)

    def test_unknown_thread_metadata(self):
        rec = make_thread_record(thread_id=0xFFFFFFFF, priority=0xFFFFFFFF,
                                 state=0xFFFFFFFF)
        dump = dump_mod.parse_record(make_record(threads=[rec], msg=b""))
        self.assertFalse(dump.threads[0].known)
        self.assertIn("unknown (0xFFFFFFFF)", dump.threads[0].state_name)

    def test_truncated_thread_table_warns(self):
        good = make_thread_record()
        bad = make_thread_record(magic=0xDEADBEEF)
        dump = dump_mod.parse_record(make_record(threads=[good, bad], msg=b""))
        self.assertEqual(len(dump.threads), 1)
        self.assertTrue(any("truncated or corrupt" in w for w in dump.warnings))
        self.assertTrue(any("Only 1 of the 2" in w for w in dump.warnings))

    def test_snapshot_clamped_to_record_end(self):
        rec = make_thread_record(snapshot_words=[0] * 8, snapshot_bytes_field=0x10000)
        dump = dump_mod.parse_record(make_record(threads=[rec], msg=b""))
        record = dump.threads[0]
        self.assertEqual(record.snapshot_bytes, 0xE8C)
        self.assertTrue(any("runs past the end" in w for w in dump.warnings))

    def test_snapshot_bytes_rounded_down(self):
        rec = make_thread_record(snapshot_words=[0] * 4, snapshot_bytes_field=13)
        dump = dump_mod.parse_record(make_record(threads=[rec], msg=b""))
        self.assertEqual(dump.threads[0].snapshot_bytes, 12)
        self.assertTrue(any("multiple of 4" in w for w in dump.warnings))

    def test_incomplete_flag_warns(self):
        dump = dump_mod.parse_record(make_record(complete=False))
        self.assertFalse(dump.complete)
        self.assertTrue(any("interrupted" in w for w in dump.warnings))

    def test_threads_written_mismatch_warns(self):
        dump = dump_mod.parse_record(make_record(thread_count=1, threads_written=3))
        self.assertTrue(any("exceeds thread_count" in w for w in dump.warnings))
        self.assertTrue(any("Only 1 of the 3" in w for w in dump.warnings))

    def test_crashing_index_out_of_range(self):
        dump = dump_mod.parse_record(make_record(crashing_index=5))
        self.assertEqual(dump.crashing_index, 0)
        self.assertTrue(any("out of range" in w for w in dump.warnings))


class V4FieldsTests(unittest.TestCase):
    def test_thread_registers_and_linkage(self):
        rec = make_thread_record(
            regs=[0x100 + i for i in range(13)],
            queue=0x23D90000, mutex=0x23D90100,
            link_prev=0x22B9200, link_next=0x22B9300,
            thread_ptr=0x22B9100)
        dump = dump_mod.parse_record(make_record(threads=[rec], msg=b""))
        t = dump.threads[0]
        self.assertEqual(t.regs[0], 0x100)
        self.assertEqual(t.regs[12], 0x10C)
        self.assertEqual(t.queue, 0x23D90000)
        self.assertEqual(t.mutex, 0x23D90100)
        self.assertEqual(t.link_prev, 0x22B9200)
        self.assertEqual(t.link_next, 0x22B9300)
        self.assertEqual(t.thread_ptr, 0x22B9100)

    def test_sp_outside_flag(self):
        rec = make_thread_record(flags=dump_mod.THREAD_FLAG_SP_OUTSIDE)
        dump = dump_mod.parse_record(make_record(threads=[rec], msg=b""))
        t = dump.threads[0]
        self.assertFalse(t.sp_valid)
        self.assertTrue(t.sp_outside)

    def test_sys_block(self):
        dump = dump_mod.parse_record(make_record(
            msg=b"",
            sys_busy_timeouts=1, sys_lock_skips=2, sys_lock_waits=3,
            sys_wake_count=4,
            sys_frame_flags=0x0A090700,
            sys_queue_samples=[(0x23D90000, 0x22B9100, 0x22B9300),
                               (0x23D90400, 0, 0)]))
        self.assertEqual(dump.sys.uplink_card_busy_timeouts, 1)
        self.assertEqual(dump.sys.uplink_card_lock_skips, 2)
        self.assertEqual(dump.sys.uplink_card_lock_waits, 3)
        self.assertEqual(dump.sys.mod_wake_count, 4)
        self.assertEqual(dump.sys.frame_flags,
                         {"flag0": 0x00, "flag7": 0x07, "flag9": 0x09, "flag10": 0x0A})
        self.assertEqual(len(dump.sys.queue_samples), 2)
        self.assertEqual(dump.sys.queue_samples[0].head, 0x22B9100)

    def test_frame_flags_unavailable(self):
        dump = dump_mod.parse_record(make_record(msg=b"", sys_frame_flags=0xFFFFFFFF))
        self.assertIsNone(dump.sys.frame_flags)


class QueueAnalysisTests(unittest.TestCase):
    def _three_thread_dump(self, link_next_a, link_next_b, link_prev_b,
                           queue_b, sample_head=0x22B9100):
        a = make_thread_record(thread_id=1, queue=0x23D90000,
                               link_prev=0, link_next=link_next_a,
                               thread_ptr=0x22B9100)
        b = make_thread_record(thread_id=2, queue=queue_b,
                               link_prev=link_prev_b, link_next=link_next_b,
                               thread_ptr=0x22B9200)
        c = make_thread_record(thread_id=3, queue=0x23D90000,
                               link_prev=0x22B9200, link_next=0,
                               thread_ptr=0x22B9300)
        return dump_mod.parse_record(make_record(
            threads=[a, b, c], msg=b"",
            sys_queue_samples=[(0x23D90000, sample_head, 0x22B9300)]))

    def test_clean_chain(self):
        dump = self._three_thread_dump(0x22B9200, 0x22B9300, 0x22B9100, 0x23D90000)
        analysis = dump_mod.analyze_thread_queues(dump)
        self.assertTrue(analysis.clean)
        self.assertEqual(analysis.chains[0x23D90000], [0x22B9100, 0x22B9200, 0x22B9300])

    def test_lost_waiter(self):
        # b claims the queue but is unreachable from its head (a -> c directly).
        dump = self._three_thread_dump(0x22B9300, 0, 0x22B9100, 0x23D90000)
        analysis = dump_mod.analyze_thread_queues(dump)
        lost = [f for f in analysis.findings if f.kind == "lost_waiter"]
        self.assertEqual(len(lost), 1)
        self.assertIn(0x22B9200, lost[0].thread_ptrs)

    def test_cross_splice(self):
        # a (queue 0x23D90000) links to b which claims a different queue.
        dump = self._three_thread_dump(0x22B9200, 0x22B9300, 0x22B9100, 0x23D90999)
        analysis = dump_mod.analyze_thread_queues(dump)
        splices = [f for f in analysis.findings if f.kind == "cross_splice"]
        self.assertTrue(splices)

    def test_broken_back_link(self):
        # b.link_prev points at something other than a: the stale-node signature.
        dump = self._three_thread_dump(0x22B9200, 0x22B9300, 0x22B9DEAD, 0x23D90000)
        analysis = dump_mod.analyze_thread_queues(dump)
        broken = [f for f in analysis.findings if f.kind == "broken_link"]
        self.assertTrue(broken)

    def test_cycle(self):
        # a -> b -> a.
        dump = self._three_thread_dump(0x22B9200, 0x22B9100, 0x22B9100, 0x23D90000)
        analysis = dump_mod.analyze_thread_queues(dump)
        cycles = [f for f in analysis.findings if f.kind == "cycle"]
        self.assertTrue(cycles)


class LocateRecordTests(unittest.TestCase):
    def test_default_offset(self):
        located = dump_mod.locate_record(make_save(make_record()))
        self.assertEqual(located.offset, dump_mod.EEPROM_BASE)
        self.assertEqual(located.warnings, [])

    def test_explicit_offset(self):
        located = dump_mod.locate_record(make_save(make_record()), offset=0x1234)
        self.assertEqual(located.offset, 0x1234)

    def test_explicit_offset_out_of_bounds(self):
        with self.assertRaises(dump_mod.DumpError):
            dump_mod.locate_record(make_save(make_record()), offset=0x1FF00)

    def test_clean_save_region(self):
        with self.assertRaises(dump_mod.CleanSaveError):
            dump_mod.locate_record(bytes([0xFF] * 0x20000))

    def test_clean_save_bare(self):
        with self.assertRaises(dump_mod.CleanSaveError):
            dump_mod.locate_record(bytes([0xFF] * dump_mod.RECORD_SIZE))

    def test_bare_record(self):
        record = make_record()
        located = dump_mod.locate_record(record)
        self.assertEqual(located.offset, 0)
        self.assertTrue(any("bare crash dump record" in w for w in located.warnings))

    def test_scan_fallback(self):
        record = make_record()
        blob = bytearray(b"\x00" * 0xC6B0)
        blob[0x100:0x100 + len(record)] = record
        located = dump_mod.locate_record(bytes(blob))
        self.assertEqual(located.offset, 0x100)
        self.assertTrue(any("non-default offset" in w for w in located.warnings))

    def test_no_record_found(self):
        with self.assertRaises(dump_mod.DumpError):
            dump_mod.locate_record(b"\x00" * 0x20000)

    def test_file_too_small(self):
        with self.assertRaises(dump_mod.DumpError):
            dump_mod.locate_record(b"\x00" * 0x1000)


class ChecksumTests(unittest.TestCase):
    def test_checksum_excludes_field(self):
        record = make_record()
        self.assertEqual(dump_mod.compute_checksum(record),
                         dump_mod.parse_record(record).checksum_computed)

    def test_checksum_covers_message_but_not_complete(self):
        # The checksum spans the whole header (message included) except its
        # own word and the complete flag.
        a = make_record(msg=b"one")
        b = make_record(msg=b"two")
        self.assertNotEqual(dump_mod.compute_checksum(a), dump_mod.compute_checksum(b))
        self.assertEqual(dump_mod.compute_checksum(a),
                         dump_mod.compute_checksum(make_record(msg=b"one",
                                                               complete=False)))

    def test_checksum_ignores_thread_table(self):
        # Thread records live past the header and are not checksummed.
        a = make_record(threads=[make_thread_record(snapshot_words=[1, 2])])
        b = make_record(threads=[make_thread_record(snapshot_words=[3, 4])])
        self.assertEqual(dump_mod.compute_checksum(a), dump_mod.compute_checksum(b))


if __name__ == "__main__":
    unittest.main()
