"""Crash dump parser for the PMD Sky speedrun mod.

Reads the crash dump record the mod writes into the backup EEPROM (layout in
``src/crash_dump.h``), decodes it, resolves addresses against the
``pmdsky-debug`` symbol tables and prints a user-friendly report.

Run with ``python -m parser SAV [options]`` - see ``parser/cli.py``.
"""

__version__ = "1.0.0"
