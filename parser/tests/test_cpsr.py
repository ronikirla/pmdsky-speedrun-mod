"""CPSR decoding tests."""
from __future__ import annotations

import unittest

from parser import cpsr


class CpsrTests(unittest.TestCase):
    def test_supervisor_arm(self):
        d = cpsr.decode(0x60000013)
        self.assertEqual(d["mode_name"], "Supervisor (SVC)")
        self.assertEqual(d["instruction_set"], "ARM")
        self.assertFalse(d["irq_masked"])
        self.assertFalse(d["fiq_masked"])
        self.assertEqual(d["flags"], ["Z", "C"])

    def test_system_mode(self):
        d = cpsr.decode(0x6000001F)
        self.assertEqual(d["mode_name"], "System")

    def test_user_thumb_irq_masked(self):
        d = cpsr.decode(0x800000FF)
        self.assertEqual(d["mode_name"], "System")
        self.assertTrue(d["thumb"])
        self.assertEqual(d["instruction_set"], "Thumb")
        self.assertTrue(d["irq_masked"])
        self.assertTrue(d["fiq_masked"])
        self.assertEqual(d["flags"], ["N"])

    def test_irq_mode(self):
        d = cpsr.decode(0x12)
        self.assertEqual(d["mode_name"], "IRQ")

    def test_format_line(self):
        line = cpsr.format_cpsr(0x60000013)
        self.assertIn("Supervisor", line)
        self.assertIn("ARM", line)
        self.assertIn("flags: Z, C", line)

    def test_format_no_flags(self):
        self.assertIn("flags: none", cpsr.format_cpsr(0x13))


if __name__ == "__main__":
    unittest.main()
