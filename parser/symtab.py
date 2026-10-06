"""resymgen symbol table loading and address resolution.

Loads the YAML symbol catalogs shipped with ``pmdsky-debug``
(``pmdsky-debug/symbols/*.yml`` plus their subregion files) and resolves
memory addresses to symbol names for one game version (EU / NA / JP).

Format summary (see ``pmdsky-debug/docs/resymgen.md``):

- A file contains one or more named *blocks*.
- Each block has a (possibly version-dependent) ``address`` and ``length``
  and two symbol lists, ``functions`` and ``data``.
- Symbol addresses are absolute in-game addresses for the documented
  version, not offsets into the binary.
- Version-dependent values are either a scalar (applies to every version of
  the block) or a mapping like ``{EU: 0x200C2E4, NA: 0x200C25C}``.  An
  address may also be a *list* of scalars (the same symbol exists at several
  addresses).
- Blocks may declare *subregions*: further YAML files that live in a
  sibling directory named after the parent file's stem
  (``arm9.yml`` -> ``arm9/libs.yml``).
- Version keys come in two flavours: plain (``EU``) and ``-ITCM`` suffixed
  (``EU-ITCM``).  The latter documents where ITCM-resident code lives in
  the runtime ITCM mirror at 0x1FF8000 instead of its storage location
  inside the arm9 binary.  Both variants are indexed for the selected
  version; symbols missing an explicit ``-ITCM`` address are rebased from
  their plain address using the block addresses.

Overlays can share a load address (e.g. EU overlays 11/29/34 all map at
0x22DCB80), so :meth:`SymbolTable.resolve` returns *all* matching
candidates, each labelled with the block it came from; callers pick.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

from .dump import HOOK_NAMES

try:
    import yaml  # type: ignore
except ImportError:  # pragma: no cover - environment without PyYAML
    yaml = None

VERSIONS = ("EU", "NA", "JP")
# Tie-break order for version auto-detection.  The mod ships EU and US
# builds; FatalError's NA and JP entry addresses are identical, so NA must
# win that tie.
VERSION_PREFERENCE = ("NA", "EU", "JP")

# Hook functions whose entry address doubles as the recorded crash pc; used
# for version auto-detection.
_HOOK_SYMBOL_NAMES = set(HOOK_NAMES.values())

# Names recognised in a --mod-symbols file.
_NM_TYPE_KINDS = {
    "t": "function", "T": "function", "w": "function", "W": "function",
    "d": "data", "D": "data", "b": "data", "B": "data",
    "r": "data", "R": "data",
}


# --- Data model --------------------------------------------------------------


@dataclass
class Symbol:
    name: str
    address: int
    length: Optional[int]
    kind: str                       # 'function' | 'data'
    block: str
    file: str                       # top-level file stem, e.g. 'arm9'
    description: Optional[str] = None


@dataclass
class Resolution:
    symbol: Symbol
    offset: int                     # addr - symbol.address
    exact: bool                     # address is inside/equals the symbol extent


@dataclass
class BlockInstance:
    """One block as instantiated for a particular version key."""

    name: str
    version_key: str                # 'EU', 'EU-ITCM', '*' for version-agnostic
    address: int
    length: int
    symbols: List[Symbol]
    file: str                       # top-level file stem
    executable: bool

    def contains(self, addr: int) -> bool:
        return self.address <= addr < self.address + self.length

    def resolve(self, addr: int) -> List[Tuple[Symbol, int, bool]]:
        """Best symbol matches within this block for ``addr``.

        Exact hits (the address equals a symbol's start or falls within its
        documented length) win.  Otherwise the nearest symbol below the
        address is returned as an approximate match.
        """
        if not self.contains(addr):
            return []
        exact: List[Tuple[Symbol, int, bool]] = []
        below: Optional[Tuple[Symbol, int]] = None
        for sym in self.symbols:
            if sym.address == addr:
                exact.append((sym, 0, True))
            elif sym.length and sym.address < addr < sym.address + sym.length:
                exact.append((sym, addr - sym.address, True))
            elif sym.address < addr:
                if below is None or sym.address > below[0].address:
                    below = (sym, addr - sym.address)
        if exact:
            return exact
        if below is not None:
            return [(below[0], below[1], False)]
        return []


# --- YAML value helpers ------------------------------------------------------


def _num(value) -> Optional[int]:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            # nm output prints plain 8-hex-digit addresses ("023d8010"),
            # which int(x, 0) rejects due to the leading zero.  Fall back to
            # hex only for unambiguous long hex strings.
            lowered = value.lower()
            if len(value) >= 6 and all(ch in "0123456789abcdef" for ch in lowered):
                try:
                    return int(lowered, 16)
                except ValueError:
                    return None
            return None
    return None


def _version_values(value, key: str) -> List[int]:
    """Resolve ``MaybeVersionDep[ScalarOrList[number]]`` for one version key."""
    if value is None:
        return []
    if isinstance(value, dict):
        if key not in value:
            return []
        value = value[key]
    if isinstance(value, list):
        nums = [_num(v) for v in value]
        return [n for n in nums if n is not None]
    n = _num(value)
    return [n] if n is not None else []


def _version_scalar(value, key: str) -> Optional[int]:
    values = _version_values(value, key)
    return values[0] if values else None


def _rebased_values(entry_value, key: str, block_addr: Dict[str, int],
                    block_len: Dict[str, Optional[int]]) -> List[int]:
    """Version values for an ``-ITCM`` key, rebasing from the plain key.

    A symbol documented only at its in-binary address (plain version key)
    has a well-defined runtime address in the ITCM mirror when the block's
    plain and ``-ITCM`` variants describe the *same* content (same length,
    e.g. the ``itcm`` subregion: binary 0x20B3CC0 / runtime 0x1FF8000, both
    0x4000 bytes).  When the lengths differ (e.g. the ``arm9`` block, whose
    ``-ITCM`` variant only covers the 0x4000-byte ITCM-resident portion),
    rebasing would fabricate wrong addresses and is skipped.
    """
    values = _version_values(entry_value, key)
    if values or not key.endswith("-ITCM"):
        return values
    base = key[: -len("-ITCM")]
    base_values = _version_values(entry_value, base)
    if not base_values:
        return []
    block_base = block_addr.get(base)
    block_key = block_addr.get(key)
    len_base = block_len.get(base)
    len_key = block_len.get(key)
    if (block_base is None or block_key is None
            or len_base is None or len_key is None
            or len_base != len_key):
        return []
    delta = block_key - block_base
    return [v + delta for v in base_values]


def _is_executable_file(stem: str) -> bool:
    if stem in ("arm9", "arm7"):
        return True
    return bool(re.fullmatch(r"overlay\d+", stem))


def _estimate_length(address: int, symbols: List[Symbol]) -> int:
    end = address
    for sym in symbols:
        sym_end = sym.address + (sym.length or 0)
        end = max(end, sym_end)
    return max(4, end - address)


# --- Database ----------------------------------------------------------------


class SymbolDb:
    """All symbol blocks from a symbols directory, across game versions."""

    def __init__(self) -> None:
        self.blocks: List[BlockInstance] = []
        self.hook_addresses: Dict[Tuple[str, str], int] = {}
        self.warnings: List[str] = []
        self.files_loaded = 0

    # -- loading ----------------------------------------------------

    @classmethod
    def load(cls, symbols_dir: Path) -> "SymbolDb":
        if yaml is None:
            db = cls()
            db.warnings.append(
                "PyYAML is not installed; symbol resolution is disabled "
                "(install with: pip install pyyaml)."
            )
            return db
        db = cls()
        symbols_dir = Path(symbols_dir)
        visited = set()
        for yml in sorted(symbols_dir.glob("*.yml")):
            if yml.name.startswith(".") or yml.name == "literals.yml":
                continue
            db._load_file(yml, visited)
        return db

    def _load_file(self, path: Path, visited: set) -> None:
        path = path.resolve()
        if path in visited or not path.exists():
            return
        visited.add(path)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                doc = yaml.safe_load(fh)
        except (OSError, yaml.YAMLError) as exc:
            self.warnings.append("Failed to parse %s: %s" % (path.name, exc))
            return
        if not isinstance(doc, dict):
            return
        self.files_loaded += 1
        stem = path.stem
        # Subregions live in a sibling directory named after this file's stem.
        for block_doc in doc.values():
            if not isinstance(block_doc, dict):
                continue
            for sub in block_doc.get("subregions") or []:
                self._load_file(path.parent / stem / str(sub), visited)
        for block_name, block_doc in doc.items():
            if not isinstance(block_doc, dict):
                continue
            self._load_block(str(block_name), block_doc, stem)

    def _load_block(self, name: str, block: dict, stem: str) -> None:
        addr_doc = block.get("address")
        length_doc = block.get("length")

        if isinstance(addr_doc, dict):
            keys = [k for k, v in addr_doc.items() if _num(v) is not None]
        else:
            scalar = _num(addr_doc)
            if scalar is None:
                return
            keys = [str(v) for v in (block.get("versions") or [])] or ["*"]
            addr_doc = {k: scalar for k in keys}
            if isinstance(length_doc, int):
                length_doc = {k: length_doc for k in keys}
            elif not isinstance(length_doc, dict):
                length_doc = {k: (length_doc if _num(length_doc) is not None else None)
                              for k in keys}

        addr_by_key = {k: _num(addr_doc[k]) for k in keys}
        len_by_key = {k: _version_scalar(length_doc, k) for k in keys}

        executable = _is_executable_file(stem)
        for key in keys:
            address = addr_by_key.get(key)
            if address is None:
                continue
            symbols: List[Symbol] = []
            for kind, list_key in (("function", "functions"), ("data", "data")):
                for entry in block.get(list_key) or []:
                    if not isinstance(entry, dict):
                        continue
                    symbols.extend(self._load_symbols(
                        entry, kind, name, stem, key, addr_by_key, len_by_key
                    ))
            length = len_by_key.get(key)
            if length is None:
                length = _estimate_length(address, symbols)
            self.blocks.append(BlockInstance(
                name=name,
                version_key=key,
                address=address,
                length=length,
                symbols=symbols,
                file=stem,
                executable=executable,
            ))

    def _load_symbols(self, entry: dict, kind: str, block_name: str,
                      stem: str, key: str, block_addr: Dict[str, int],
                      block_len: Dict[str, Optional[int]]) -> List[Symbol]:
        name = entry.get("name")
        if not name:
            return []
        addresses = _rebased_values(entry.get("address"), key, block_addr, block_len)
        length = _rebased_values(entry.get("length"), key, block_addr, block_len)
        length_value = length[0] if length else None
        description = entry.get("description")
        if isinstance(description, str):
            description = description.strip() or None
        else:
            description = None
        out = []
        for addr in addresses:
            out.append(Symbol(
                name=str(name),
                address=addr,
                length=length_value,
                kind=kind,
                block=block_name,
                file=stem,
                description=description,
            ))
        # Record hook entry addresses (per plain version) for auto-detection.
        if kind == "function" and str(name) in _HOOK_SYMBOL_NAMES and key in VERSIONS:
            for addr in addresses:
                self.hook_addresses.setdefault((key, str(name)), addr)
        return out

    # -- queries ----------------------------------------------------

    def table_for(self, version: str) -> SymbolTable:
        wanted = {version, version + "-ITCM", "*"}
        blocks = [b for b in self.blocks if b.version_key in wanted]
        return SymbolTable(version, blocks, self.warnings,
                           source_loaded=self.files_loaded > 0)

    def detect_version(self, pc: int, hook_id: int) -> Optional[str]:
        """Auto-detect the game version from the hooked function entry address.

        ``pc`` in a crash dump is the address of the hooked instruction, i.e.
        the entry of FatalError or OS_Panic for the ROM that crashed.
        """
        func = HOOK_NAMES.get(hook_id)
        if func is None:
            return None
        target = pc & ~1
        hits = []
        for version in VERSIONS:
            addr = self.hook_addresses.get((version, func))
            if addr is not None and (addr == pc or addr == target):
                hits.append(version)
        if not hits:
            return None
        for version in VERSION_PREFERENCE:
            if version in hits:
                return version
        return hits[0]


# --- Per-version table -------------------------------------------------------


# Fallback executable ranges when no symbol tables could be loaded: the
# ARM9 main-memory code areas (ITCM mirror, arm9, overlays, mod's overlay 36).
FALLBACK_CODE_RANGES = [
    (0x01FF8000, 0x02000000),   # ITCM mirror
    (0x02000000, 0x02400000),   # arm9 + overlays + mod (main RAM code)
]


def default_code_filter(addr: int) -> bool:
    return any(lo <= addr < hi for lo, hi in FALLBACK_CODE_RANGES)


class SymbolTable:
    """Resolved symbol blocks for one game version, plus mod-specific labels."""

    def __init__(self, version: str, blocks: List[BlockInstance],
                 warnings: Optional[List[str]] = None,
                 source_loaded: bool = False) -> None:
        self.version = version
        self.blocks = blocks
        self.warnings = warnings if warnings is not None else []
        # True when real symbol tables (YAML) were loaded.  When False, code
        # detection falls back to static ranges so a bare trace still works.
        self.source_loaded = source_loaded

    @property
    def has_symbols(self) -> bool:
        """True when any block carries symbols at all."""
        return any(block.symbols for block in self.blocks)

    def has_symbols_near(self, addr: int) -> bool:
        """True when any block containing ``addr`` carries symbols.

        Backtrace filtering uses this: strict "function-only" frame
        filtering only makes sense in regions the tables can actually
        resolve.
        """
        return any(block.symbols for block in self.blocks if block.contains(addr))

    # -- resolution -------------------------------------------------

    def resolve(self, addr: int, kind: Optional[str] = None) -> List[Resolution]:
        """All symbol candidates for ``addr`` across blocks.

        Results are ordered: exact matches first (most specific block first),
        then approximate (nearest-symbol-below) matches.  ``kind`` filters to
        ``'function'`` or ``'data'`` when given.
        """
        candidates: List[Tuple[Symbol, int, bool, int]] = []
        seen = set()
        for block in self.blocks:
            for sym, offset, exact in block.resolve(addr):
                if kind is not None and sym.kind != kind:
                    continue
                dedupe_key = (sym.name, sym.address, sym.kind)
                if dedupe_key in seen:
                    continue
                seen.add(dedupe_key)
                candidates.append((sym, offset, exact, block.length))
        candidates.sort(key=lambda c: (
            not c[2],                       # exact matches first
            c[0].kind != "function",        # then functions before data
            c[0].length is None,            # known extents before unknown
            c[0].length or 0,               # tighter extent = more specific
            c[3],                           # smaller block = more specific
            c[0].name,
        ))
        return [Resolution(sym, offset, exact) for sym, offset, exact, _ in candidates]

    def is_code_address(self, addr: int) -> bool:
        """True when ``addr`` falls inside an executable block.

        Executable blocks are the code binaries (arm9, arm7, overlays) and
        the mod's own code; heap/stack bookkeeping from ``ram.yml`` does not
        count, so data words are not mistaken for return addresses.  When no
        symbol tables could be loaded at all, a static set of code ranges is
        used instead so a bare trace still works.
        """
        executable = [b for b in self.blocks if b.executable]
        if self.source_loaded:
            return any(b.contains(addr) for b in executable)
        return default_code_filter(addr) or any(b.contains(addr) for b in executable)

    def region_label(self, addr: int) -> Optional[str]:
        for block in self.blocks:
            if block.executable and block.contains(addr):
                return block.name
        for block in self.blocks:
            if block.contains(addr):
                return block.name
        return None

    def function_at(self, addr: int) -> Optional[Resolution]:
        cands = self.resolve(addr, kind="function")
        return cands[0] if cands else None


# --- Mod-specific symbols ----------------------------------------------------


MOD_BINARY_BASE = 0x023D7FF0     # linker.ld: overlay 36 common area origin
MOD_BINARY_SIZE = 0x8010
OVERLAY36_BASE = 0x023A7080      # scripts/patch.py: overlay 36 RAM start
OVERLAY36_END = 0x023E0000

# Mod code patched into verified-free arm9 space (patches/patch.asm).  The
# addresses are from the EU layout; the stubs are only applied to EU builds.
MOD_PATCH_STUBS = [
    (0x02094850, "mod_vcount0_alarm_hook"),
    (0x02094910, "mod_waittillvblank_trampoline"),
    (0x02094950, "mod_delay_rand16_trampoline"),
    (0x02094968, "mod_crash_dump_fatalerror_stub"),
    (0x020949F4, "mod_crash_dump_os_panic_stub"),
]
MOD_PATCH_STUBS_END = 0x02094A5C


def _make_symbol(name: str, address: int, kind: str, block: str,
                 length: Optional[int] = None,
                 description: Optional[str] = None) -> Symbol:
    return Symbol(name=name, address=address, length=length, kind=kind,
                  block=block, file="mod", description=description)


def builtin_mod_blocks(version: str,
                       mod_symbols: Optional[List[Symbol]] = None) -> List[BlockInstance]:
    """Synthetic blocks covering the mod's own code regions.

    Always present: the mod binary inside overlay 36 and the rest of
    overlay 36.  When ``mod_symbols`` is given (from ``--mod-symbols``), the
    individual mod functions are resolved by name as well.
    """
    blocks: List[BlockInstance] = [
        BlockInstance(
            name="speedrun mod (overlay 36 common area)",
            version_key=version,
            address=MOD_BINARY_BASE,
            length=MOD_BINARY_SIZE,
            symbols=list(mod_symbols or []),
            file="mod",
            executable=True,
        ),
        BlockInstance(
            name="overlay 36 (preserved original)",
            version_key=version,
            address=OVERLAY36_BASE,
            length=MOD_BINARY_BASE - OVERLAY36_BASE,
            symbols=[],
            file="mod",
            executable=True,
        ),
    ]
    stubs = [
        _make_symbol(name, addr, "function", "mod patches (arm9)")
        for addr, name in MOD_PATCH_STUBS
    ]
    blocks.append(BlockInstance(
        name="mod patches (arm9)",
        version_key=version,
        address=MOD_PATCH_STUBS[0][0],
        length=MOD_PATCH_STUBS_END - MOD_PATCH_STUBS[0][0],
        symbols=stubs,
        file="mod",
        executable=True,
    ))
    return blocks


# --- --mod-symbols file parsing ----------------------------------------------


_DEFINE_LABEL_RE = re.compile(
    r"^\.definelabel\s+([A-Za-z_.$][\w.$]*)\s*,\s*(0[xX][0-9a-fA-F]+|\d+)\s*(?:[;@/].*)?$"
)
_ASSIGN_RE = re.compile(
    r"^([A-Za-z_.$][\w.$]*)\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;?\s*(?:[;@/].*)?$"
)
_EQU_RE = re.compile(
    r"^([A-Za-z_.$][\w.$]*)\s+equ\s+(0[xX][0-9a-fA-F]+|\d+)\s*(?:[;@/].*)?$",
    re.IGNORECASE,
)
_NM_RE = re.compile(
    r"^(0[xX][0-9a-fA-F]+|[0-9a-fA-F]{8})\s+([A-Za-z])\s+(\S+)\s*$"
)


def parse_mod_symbols(path: Path) -> List[Symbol]:
    """Parse armips ``.definelabel`` / ``NAME = addr`` / ``equ`` / nm-style text.

    This is the format of ``build/binaries/symbols.asm`` (written by
    ``scripts/patch.py`` from the linked ELF) and of ``arm-none-eabi-nm``
    output, so either file works.
    """
    symbols: List[Symbol] = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw_line in fh:
            line = raw_line.strip()
            if not line or line.startswith((";", "//", "/*", "*", "#")):
                continue
            match = _DEFINE_LABEL_RE.match(line) or _EQU_RE.match(line)
            if match:
                name, addr = match.group(1), _num(match.group(2))
                if addr is not None:
                    symbols.append(_make_symbol(name, addr, "function", "mod symbols"))
                continue
            match = _ASSIGN_RE.match(line)
            if match:
                name, addr = match.group(1), _num(match.group(2))
                if addr is not None:
                    symbols.append(_make_symbol(name, addr, "function", "mod symbols"))
                continue
            match = _NM_RE.match(line)
            if match:
                addr = _num(match.group(1))
                kind = _NM_TYPE_KINDS.get(match.group(2), "function")
                name = match.group(3)
                if addr is not None:
                    symbols.append(_make_symbol(name, addr, kind, "mod symbols"))
                continue
    return symbols
