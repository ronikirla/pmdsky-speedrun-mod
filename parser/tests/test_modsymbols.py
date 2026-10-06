"""--mod-symbols file parsing tests (armips / equ / nm formats)."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from parser import symtab


class ModSymbolsTests(unittest.TestCase):
    def _parse(self, text: str):
        with tempfile.NamedTemporaryFile("w", suffix=".asm",
                                         delete=False, encoding="utf-8") as fh:
            fh.write(text)
            path = Path(fh.name)
        try:
            return symtab.parse_mod_symbols(path)
        finally:
            path.unlink(missing_ok=True)

    def test_definelabel(self):
        syms = self._parse(".definelabel CrashDumpWrite,0x023D8010\n")
        self.assertEqual(len(syms), 1)
        self.assertEqual(syms[0].name, "CrashDumpWrite")
        self.assertEqual(syms[0].address, 0x023D8010)

    def test_definelabel_with_comment(self):
        syms = self._parse(".definelabel Foo,0x1234 ; comment here\n")
        self.assertEqual(syms[0].address, 0x1234)

    def test_assign(self):
        syms = self._parse("PLAY_TIME = 0x022ABFD4;\n")
        self.assertEqual(syms[0].name, "PLAY_TIME")
        self.assertEqual(syms[0].address, 0x022ABFD4)

    def test_equ(self):
        syms = self._parse("arm9_start equ 0x02000000\n")
        self.assertEqual(syms[0].address, 0x02000000)

    def test_nm_style(self):
        syms = self._parse("023d8010 t CrashDumpWrite\n023d8100 D SomeData\n")
        self.assertEqual(len(syms), 2)
        self.assertEqual(syms[0].address, 0x023D8010)
        self.assertEqual(syms[0].kind, "function")
        self.assertEqual(syms[1].kind, "data")

    def test_comments_and_blanks_skipped(self):
        syms = self._parse(
            "; comment\n"
            "// comment\n"
            "\n"
            ".definelabel A,0x1\n"
        )
        self.assertEqual([s.name for s in syms], ["A"])

    def test_mixed_file(self):
        text = (
            ".definelabel HandleSoftReset,0x023DA0D4\n"
            "speedrun_hud_strings = 0x023DC12C;\n"
            "overlay29_start equ 0x022DCB80\n"
        )
        syms = self._parse(text)
        self.assertEqual({s.name for s in syms},
                         {"HandleSoftReset", "speedrun_hud_strings", "overlay29_start"})


if __name__ == "__main__":
    unittest.main()
