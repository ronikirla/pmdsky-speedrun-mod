"""CLI behaviour tests (exit codes and output modes)."""
from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

try:
    from .helpers import make_record, make_save, make_thread_record
except ImportError:  # running the file directly
    from helpers import make_record, make_save, make_thread_record  # type: ignore

from parser import cli
from parser import dump as dump_mod


class CliTests(unittest.TestCase):
    def _run(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(argv)
        return code, out.getvalue(), err.getvalue()

    def _write(self, blob: bytes):
        handle = tempfile.NamedTemporaryFile(suffix=".sav", delete=False)
        handle.write(blob)
        handle.close()
        self.addCleanup(Path(handle.name).unlink)
        return handle.name

    def test_clean_save_exit_code_2(self):
        path = self._write(bytes([0xFF] * 0x20000))
        code, out, _ = self._run([path])
        self.assertEqual(code, 2)
        self.assertIn("No crash dump present", out)

    def test_record_exit_code_0(self):
        path = self._write(make_save(make_record()))
        code, out, _ = self._run([path])
        self.assertEqual(code, 0)
        self.assertIn("Crash dump report", out)
        self.assertIn("FatalError", out)
        self.assertIn("test %d", out)

    def test_json_output(self):
        path = self._write(make_save(make_record()))
        code, out, _ = self._run([path, "--json"])
        self.assertEqual(code, 0)
        payload = json.loads(out)
        self.assertEqual(payload["hook"], "FatalError")
        self.assertEqual(payload["message"]["raw"], "test %d")
        self.assertEqual(payload["source"]["record_offset"], 0xB6B0)
        self.assertTrue(payload["checksum"]["ok"])

    def test_missing_file_exit_1(self):
        code, _, err = self._run(["definitely_missing_file.sav"])
        self.assertEqual(code, 1)
        self.assertIn("cannot read", err)

    def test_garbage_file_exit_1(self):
        path = self._write(b"\x00" * 0x20000)
        code, _, err = self._run([path])
        self.assertEqual(code, 1)
        self.assertIn("No crash dump record found", err)

    def test_bad_symbols_dir_exit_1(self):
        path = self._write(make_save(make_record()))
        code, _, err = self._run([path, "--symbols", "does/not/exist"])
        self.assertEqual(code, 1)
        self.assertIn("does not look like", err)

    def test_version_override(self):
        path = self._write(make_save(make_record()))
        code, out, _ = self._run([path, "--version", "NA"])
        self.assertEqual(code, 0)
        self.assertIn("North American (NA/US) - specified with --version", out)

    def test_raw_stack_section(self):
        path = self._write(make_save(make_record()))
        code, out, _ = self._run([path, "--raw-stack", "8"])
        self.assertEqual(code, 0)
        self.assertIn("Stack snapshot", out)
        self.assertIn("hook stub spill: r0", out)

    def test_thread_sections(self):
        current = make_thread_record(
            flags=dump_mod.THREAD_FLAG_SP_VALID | dump_mod.THREAD_FLAG_CURRENT)
        other = make_thread_record(thread_id=3, priority=30, state=1)
        path = self._write(make_save(make_record(threads=[current, other], msg=b"")))
        code, out, _ = self._run([path])
        self.assertEqual(code, 0)
        self.assertIn("Other threads (1", out)
        self.assertIn("id 3", out)
        self.assertIn("priority 30", out)
        self.assertIn("ready", out)

    def test_manual_trigger_report(self):
        path = self._write(make_save(make_record(hook_id=dump_mod.HOOK_MANUAL,
                                                 msg=b"", trigger_buttons=0x0F00)))
        code, out, _ = self._run([path])
        self.assertEqual(code, 0)
        self.assertIn("Manual trigger (L+R+X+Y)", out)
        self.assertIn("R L X Y", out)

    def test_json_threads(self):
        current = make_thread_record(
            flags=dump_mod.THREAD_FLAG_SP_VALID | dump_mod.THREAD_FLAG_CURRENT)
        other = make_thread_record(thread_id=3, priority=30, state=1)
        path = self._write(make_save(make_record(threads=[current, other], msg=b"")))
        code, out, _ = self._run([path, "--json"])
        self.assertEqual(code, 0)
        payload = json.loads(out)
        self.assertEqual(len(payload["threads"]), 2)
        self.assertTrue(payload["threads"][0]["is_current"])
        self.assertEqual(payload["threads"][1]["thread_id"], 3)
        self.assertEqual(payload["threads"][1]["state_name"], "ready")
        self.assertTrue(payload["complete"])

    def test_offset_override(self):
        record = make_record()
        blob = bytearray(b"\x00" * 0x20000)
        blob[0x2000:0x2000 + len(record)] = record
        path = self._write(bytes(blob))
        code, out, _ = self._run([path, "--offset", "0x2000"])
        self.assertEqual(code, 0)
        self.assertIn("@ 0x2000", out)


if __name__ == "__main__":
    unittest.main()
