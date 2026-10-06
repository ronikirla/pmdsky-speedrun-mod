"""Crash dump record parsing tests."""
from __future__ import annotations

import unittest

try:
    from .helpers import make_record, make_save
except ImportError:  # running the file directly
    from helpers import make_record, make_save  # type: ignore

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
        self.assertEqual(dump.warnings, [])
        self.assertEqual(len(dump.stack), dump_mod.STACK_SIZE)
        self.assertEqual(dump.stack_words()[0], 0)

    def test_os_panic_hook(self):
        dump = dump_mod.parse_record(make_record(hook_id=dump_mod.HOOK_OS_PANIC, msg=b""))
        self.assertEqual(dump.hook_name, "OS_Panic")
        self.assertFalse(dump.is_fatal_error)
        self.assertEqual(dump.msg, "")

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

    def test_unknown_version_warns(self):
        dump = dump_mod.parse_record(make_record(version=99))
        self.assertTrue(any("record version 99" in w for w in dump.warnings))

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

    def test_checksum_ignores_message_and_stack(self):
        # The checksum covers only words 0x000..0x053: changing the message
        # or the stack must not change it.
        a = make_record(msg=b"one")
        b = make_record(msg=b"two")
        self.assertEqual(dump_mod.compute_checksum(a), dump_mod.compute_checksum(b))


if __name__ == "__main__":
    unittest.main()
