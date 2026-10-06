"""Symbol table tests (run against the real pmdsky-debug YAML files)."""
from __future__ import annotations

import unittest

try:
    from .helpers import symbols_dir
except ImportError:  # running the file directly
    from helpers import symbols_dir  # type: ignore

from parser import symtab


@unittest.skipIf(symbols_dir() is None or symtab.yaml is None,
                 "pmdsky-debug symbols or PyYAML not available")
class SymbolDbTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = symtab.SymbolDb.load(symbols_dir())

    def test_loads_files(self):
        self.assertGreater(self.db.files_loaded, 30)

    def test_hook_addresses_populated(self):
        self.assertEqual(self.db.hook_addresses[("EU", "FatalError")], 0x200C2E4)
        self.assertEqual(self.db.hook_addresses[("NA", "FatalError")], 0x200C25C)
        self.assertEqual(self.db.hook_addresses[("EU", "OS_Panic")], 0x207BFB8)
        self.assertEqual(self.db.hook_addresses[("NA", "OS_Panic")], 0x207BC20)
        self.assertEqual(self.db.hook_addresses[("JP", "OS_Panic")], 0x207BF08)

    def test_detect_version(self):
        # FatalError: EU entry is unique.
        self.assertEqual(self.db.detect_version(0x200C2E4, 1), "EU")
        # FatalError: NA and JP share an entry; NA must win (mod ships EU/US).
        self.assertEqual(self.db.detect_version(0x200C25C, 1), "NA")
        # OS_Panic: all three are distinct.
        self.assertEqual(self.db.detect_version(0x207BFB8, 2), "EU")
        self.assertEqual(self.db.detect_version(0x207BC20, 2), "NA")
        self.assertEqual(self.db.detect_version(0x207BF08, 2), "JP")
        # Unrelated pc -> no detection.
        self.assertIsNone(self.db.detect_version(0x20000000, 1))

    def test_resolve_fatal_error_exact(self):
        table = self.db.table_for("EU")
        hits = table.resolve(0x200C2E4, kind="function")
        self.assertTrue(hits)
        self.assertEqual(hits[0].symbol.name, "FatalError")
        self.assertEqual(hits[0].offset, 0)
        self.assertTrue(hits[0].exact)

    def test_resolve_os_panic_exact(self):
        table = self.db.table_for("EU")
        hits = table.resolve(0x207BFB8, kind="function")
        self.assertEqual(hits[0].symbol.name, "OS_Panic")

    def test_itcm_mirror_resolves(self):
        table = self.db.table_for("EU")
        hits = table.resolve(0x1FF8000, kind="function")
        self.assertTrue(hits)
        names = {h.symbol.name for h in hits}
        self.assertIn("CopyAndInterleave", names)

    def test_overlay_ambiguity_lists_blocks(self):
        # GetWeatherColorTable EU lives in overlay29; overlays share load
        # addresses, so other overlay blocks may also match.
        table = self.db.table_for("EU")
        hits = table.resolve(0x22DEF60)
        blocks = {h.symbol.block for h in hits}
        self.assertIn("overlay29", blocks)
        self.assertIn(
            "GetWeatherColorTable", {h.symbol.name for h in hits if h.exact}
        )

    def test_ram_block_not_executable(self):
        table = self.db.table_for("EU")
        # The ram.yml blocks cover (nearly) all of main RAM, including code
        # regions; they must never count as executable.
        ram_blocks = [b for b in table.blocks if b.file == "ram"]
        self.assertTrue(ram_blocks)
        self.assertFalse(any(b.executable for b in ram_blocks))
        self.assertTrue(table.is_code_address(0x200C2E4))  # arm9 function

    def test_arm9_code_region(self):
        table = self.db.table_for("EU")
        self.assertTrue(table.is_code_address(0x200C2E4))


class SymbolTableModTests(unittest.TestCase):
    def _table_with_mod(self, version="EU", mod_symbols=None):
        table = symtab.SymbolTable(version, [])
        table.blocks.extend(symtab.builtin_mod_blocks(version, mod_symbols))
        return table

    def test_mod_region_is_code(self):
        table = self._table_with_mod()
        self.assertTrue(table.is_code_address(0x023D7FF0))
        self.assertTrue(table.is_code_address(0x023DFFFF))
        # The mod block ends at 0x023E0000...
        mod_block = table.blocks[0]
        self.assertFalse(mod_block.contains(0x023E0000))
        # ...and an address outside every code region is not code.
        self.assertFalse(table.is_code_address(0x02500000))

    def test_mod_region_label(self):
        table = self._table_with_mod()
        self.assertEqual(
            table.region_label(0x023D8000),
            "speedrun mod (overlay 36 common area)",
        )

    def test_mod_symbols_resolve(self):
        syms = [
            symtab.Symbol(name="ModFunc", address=0x023DA000, length=None,
                          kind="function", block="mod symbols", file="mod"),
            symtab.Symbol(name="ModData", address=0x023DB000, length=0x10,
                          kind="data", block="mod symbols", file="mod"),
        ]
        table = self._table_with_mod(mod_symbols=syms)
        hits = table.resolve(0x023DA12C)
        self.assertEqual(hits[0].symbol.name, "ModFunc")
        self.assertEqual(hits[0].offset, 0x12C)
        exact = table.resolve(0x023DB004, kind="data")
        self.assertEqual(exact[0].symbol.name, "ModData")
        self.assertTrue(exact[0].exact)


if __name__ == "__main__":
    unittest.main()
