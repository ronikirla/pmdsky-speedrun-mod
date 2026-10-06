"""ARM (ARMv5) CPSR decoding for crash dump reports."""

from __future__ import annotations

from typing import Dict, List

# ARM processor mode bits (CPSR[4:0]).
MODE_NAMES = {
    0x10: "User",
    0x11: "FIQ",
    0x12: "IRQ",
    0x13: "Supervisor (SVC)",
    0x17: "Abort",
    0x1B: "Undefined",
    0x1F: "System",
}

# Condition flag bits, high to low.
FLAG_BITS = [
    ("N", 1 << 31),
    ("Z", 1 << 30),
    ("C", 1 << 29),
    ("V", 1 << 28),
    ("Q", 1 << 27),
]

BIT_T = 1 << 5   # Thumb state
BIT_F = 1 << 6   # FIQ mask
BIT_I = 1 << 7   # IRQ mask


def decode(cpsr: int) -> Dict:
    """Decode a CPSR value into a friendly dictionary."""
    mode = cpsr & 0x1F
    return {
        "raw": cpsr,
        "mode": mode,
        "mode_name": MODE_NAMES.get(mode, "Unknown (%#04x)" % mode),
        "instruction_set": "Thumb" if cpsr & BIT_T else "ARM",
        "thumb": bool(cpsr & BIT_T),
        "irq_masked": bool(cpsr & BIT_I),
        "fiq_masked": bool(cpsr & BIT_F),
        "flags": [name for name, bit in FLAG_BITS if cpsr & bit],
    }


def format_cpsr(cpsr: int) -> str:
    """One-line human-readable description, e.g.
    ``Supervisor (SVC), ARM, IRQ enabled, FIQ enabled; flags: Z, C``."""
    d = decode(cpsr)
    parts = [
        d["mode_name"],
        d["instruction_set"],
        "IRQ masked" if d["irq_masked"] else "IRQ enabled",
        "FIQ masked" if d["fiq_masked"] else "FIQ enabled",
    ]
    line = ", ".join(parts)
    flags = d["flags"]
    line += "; flags: " + (", ".join(flags) if flags else "none")
    return line
