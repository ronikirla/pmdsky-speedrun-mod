"""Report rendering: a human-readable text report and a JSON payload."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from . import cpsr as cpsr_mod
from .backtrace import Frame, snapshot_rows
from .dump import (
    RECORD_SIZE,
    CrashDump,
    HOOK_FATAL_ERROR,
    HOOK_OS_PANIC,
    QueueAnalysis,
    ThreadRecord,
    analyze_thread_queues,
    decode_buttons,
)
from .symtab import Resolution, SymbolTable

VERSION_LABELS = {
    "EU": "European (EU)",
    "NA": "North American (NA/US)",
    "JP": "Japanese (JP)",
}

# Short labels for the synthetic mod blocks (their full names are verbose).
_BLOCK_SHORT_LABELS = {
    "speedrun mod (overlay 36 common area)": "mod",
    "overlay 36 (preserved original)": "overlay36",
    "mod patches (arm9)": "mod-patches",
    "mod symbols": "mod",
}

_MAX_MSG_DISPLAY = 300


@dataclass
class ReportContext:
    source_path: str
    source_offset: int
    version: Optional[str]
    version_source: str
    symbols_dir: Optional[str]
    warnings: List[str] = field(default_factory=list)
    rom_path: Optional[str] = None


def _addr(value: Optional[int]) -> str:
    if value is None:
        return "????????"
    return "0x%08X" % (value & 0xFFFFFFFF)


def _block_label(symbol) -> str:
    return _BLOCK_SHORT_LABELS.get(symbol.block, symbol.block)


def _format_candidate(res: Resolution) -> str:
    sym = res.symbol
    prefix = "" if res.exact else "~"
    if res.offset:
        body = "%s%s+0x%X" % (prefix, sym.name, res.offset)
    else:
        body = prefix + sym.name
    return "%s [%s]" % (body, _block_label(sym))


def describe_candidates(candidates: List[Resolution]) -> str:
    if not candidates:
        return "(no symbol match)"
    return _format_candidate(candidates[0])


def describe_address(table: Optional[SymbolTable], addr: int) -> str:
    """Human-readable description of one address (candidates + region)."""
    if table is not None:
        candidates = table.resolve(addr, kind="function")
        if not candidates:
            # Fall back to exact data symbols only: a nearest-below guess
            # from the catch-all ram.yml block would be noise for code
            # addresses.
            candidates = [r for r in table.resolve(addr) if r.exact]
        if candidates:
            text = _format_candidate(candidates[0])
            extra = candidates[1:]
            if extra:
                shown = ", ".join(_format_candidate(c) for c in extra[:3])
                ellipsis = ", ..." if len(extra) > 3 else ""
                text += " (also: %s%s)" % (shown, ellipsis)
            return text
        region = table.region_label(addr)
        if region:
            return "(no symbol match; region: %s)" % _BLOCK_SHORT_LABELS.get(region, region)
        return "(no symbol match)"
    return "(symbols unavailable)"


def _truncate(text: str, limit: int = _MAX_MSG_DISPLAY) -> str:
    if len(text) <= limit:
        return text
    return text[:limit] + "...(truncated)"


def _frame_lines(table: Optional[SymbolTable], frames: List[Frame],
                 indent: str = "  ") -> List[str]:
    lines: List[str] = []
    for frame in frames:
        if frame.origin == "stack":
            location = "sp+0x%04X" % (frame.stack_offset or 0)
        else:
            location = frame.origin.ljust(9)
        note = " (Thumb)" if frame.thumb else ""
        lines.append("%s#%-2d %s %s  %s%s"
                     % (indent, frame.index, location.ljust(9), _addr(frame.address),
                        describe_address(table, frame.address), note))
    return lines


def describe_thread_line(table: Optional[SymbolTable], record: ThreadRecord) -> str:
    """One-line summary of a thread record (id, priority, state, pc)."""
    if record.known:
        head = "#%-2d id %-3d priority %-3d %-11s" % (
            record.index, record.thread_id, record.priority, record.state_name)
    else:
        head = "#%-2d id ?   priority ?   %-11s" % (record.index, record.state_name)
    if record.is_current:
        head += " [current]"
    return "%s pc %s  %s" % (head, _addr(record.pc),
                             describe_address(table, record.pc & ~1))


def describe_wait_line(record: ThreadRecord) -> str:
    """One line of OS wait-queue linkage for a thread record."""
    parts = ["queue %s" % _addr(record.queue)]
    if record.mutex:
        parts.append("mutex %s" % _addr(record.mutex))
    parts.append("link <- %s -> %s" % (_addr(record.link_prev),
                                       _addr(record.link_next)))
    return "    %s" % "  ".join(parts)


def describe_regs_line(record: ThreadRecord) -> str:
    """Registers r0-r12 of a thread record, two per line."""
    lines: List[str] = []
    regs = record.regs
    for i in range(0, len(regs), 2):
        chunk = "  ".join("r%-2d %s" % (i + j, _addr(regs[i + j]))
                          for j in range(min(2, len(regs) - i)))
        lines.append("    %s" % chunk)
    return "\n".join(lines)


def render_text(dump: CrashDump, table: Optional[SymbolTable],
                frames: List[Frame], trace_truncated: bool,
                rom_info: Optional[Dict], ctx: ReportContext,
                thread_traces: Optional[List[Tuple[ThreadRecord, List[Frame], bool]]] = None,
                raw_stack_limit: Optional[int] = None) -> str:
    lines: List[str] = []
    push = lines.append
    bar = "=" * 78

    push(bar)
    push(" Crash dump report - PMD Sky speedrun mod")
    push(bar)
    push("")
    push("Record")
    push("  source      %s @ %s (%s-byte crash dump record)"
         % (ctx.source_path, hex(ctx.source_offset), hex(RECORD_SIZE)))
    push("  crash type  %s (hook id %d)" % (dump.hook_name, dump.hook_id))
    push("  game        %s - %s"
         % (VERSION_LABELS.get(ctx.version, ctx.version), ctx.version_source))
    if dump.msg:
        push('  message     "%s"' % _truncate(dump.msg))
    elif dump.is_fatal_error:
        push('  message     (empty; the format-string pointer was not in the '
             'cartridge region)')
    elif dump.is_manual:
        push('  message     (none; manual triggers record no message)')
    else:
        push('  message     (none; OS_Panic records no message)')
    if dump.is_manual:
        held = decode_buttons(dump.trigger_buttons)
        push("  trigger     buttons held: %s (raw %s)"
             % (" ".join(held) if held else "(none)", hex(dump.trigger_buttons)))
    push("  threads     %d in the game's list, %d records written%s"
         % (dump.thread_count, dump.threads_written,
            "" if dump.complete else "  [dump incomplete]"))

    push("")
    push("Crash site")
    push("  pc    %s  %s" % (_addr(dump.pc), describe_address(table, dump.pc)))
    lr_note = " (Thumb return address)" if dump.lr & 1 else ""
    push("  lr    %s  %s%s" % (_addr(dump.lr), describe_address(table, dump.lr & ~1), lr_note))
    push("  sp    %s  (stack snapshot base; %s bytes captured upward)"
         % (_addr(dump.sp), hex(len(dump.stack))))
    push("  cpsr  %s  %s" % (_addr(dump.cpsr), cpsr_mod.format_cpsr(dump.cpsr)))
    push("  tick  %s  (raw OS_GetTickLo() value - a free-running OS timer, "
         "not wall-clock time)" % _addr(dump.tick))

    push("")
    push("Registers")
    arg_roles = ["arg0 (prog_pos pointer)", "arg1 (format string pointer)",
                 "arg2 (variadic #1)", "arg3 (variadic #2)"] if dump.is_fatal_error else []
    for i, value in enumerate(dump.regs):
        annotation = ""
        if i < 4:
            if arg_roles:
                annotation = "  = %s" % arg_roles[i]
            else:
                annotation = "  = arg%d (original r%d at hook entry)" % (i, i)
        push("  r%-2d  %s%s" % (i, _addr(value), annotation))

    push("")
    push("Mod / uplink state (captured before the dump's own card writes)")
    sys = dump.sys
    push("  uplink busy timeouts %d, lock skips %d, lock waits >=2ms %d"
         % (sys.uplink_card_busy_timeouts, sys.uplink_card_lock_skips,
            sys.uplink_card_lock_waits))
    push("  mod wake count       %d (VCount 0 hook wakeups)" % sys.mod_wake_count)
    flags = sys.frame_flags
    if flags is None:
        push("  frame flags          (unavailable)")
    else:
        push("  frame flags          [0]=%d [7]=%d [9]=%d [10]=%d"
             % (flags["flag0"], flags["flag7"], flags["flag9"], flags["flag10"]))
    if sys.queue_samples:
        for s in sys.queue_samples:
            push("  queue %s  head %s  tail %s"
                 % (_addr(s.queue), _addr(s.head), _addr(s.tail)))
    else:
        push("  queue samples        (none)")

    push("")
    push("Backtrace (best-effort; candidate return addresses from the stack snapshot)")
    if frames:
        for line in _frame_lines(table, frames):
            push(line)
        if trace_truncated:
            push("  (frame cap reached; increase --max-frames to scan further)")
    else:
        push("  (no code addresses found)")

    crashing_record = dump.crashing_record
    if crashing_record is not None:
        push("")
        push("Crashing thread wait state")
        push("  %s" % describe_wait_line(crashing_record).strip())

    others = [(rec, tf, tt) for rec, tf, tt in (thread_traces or [])
              if rec.index != dump.crashing_index]
    if others:
        push("")
        push("Other threads (%d; record %d above is the current thread)"
             % (len(others), dump.crashing_index))
        for record, thread_frames, thread_truncated in others:
            push("")
            push("  %s" % describe_thread_line(table, record))
            push("    lr %s  %s" % (_addr(record.lr),
                                    describe_address(table, record.lr & ~1)))
            push("    sp %s  stack %s..%s, snapshot %s bytes%s"
                 % (_addr(record.sp), _addr(record.stack_start), _addr(record.stack_end),
                    hex(record.snapshot_bytes),
                    "" if record.sp_valid else
                    ("  [sp outside the stack bounds; snapshot captured from sp]"
                     if record.sp_outside else "  [sp outside the stack bounds]")))
            push(describe_wait_line(record))
            if thread_frames:
                for line in _frame_lines(table, thread_frames, indent="    "):
                    push(line)
                if thread_truncated:
                    push("    (frame cap reached; increase --max-frames to scan further)")
            else:
                push("    (no code addresses found)")
            push("    registers")
            push(describe_regs_line(record))

    analysis = analyze_thread_queues(dump)
    push("")
    push("Wait-queue analysis")
    if analysis.clean:
        push("  no queue corruption detected")
    else:
        for finding in analysis.findings:
            push("  [%s] %s" % (finding.kind, finding.detail))

    if rom_info is not None:
        push("")
        push("ROM-derived info (from %s)" % (ctx.rom_path or "ROM"))
        if "assert_file" in rom_info:
            push("  assert location   : %s:%d"
                 % (rom_info["assert_file"], rom_info["assert_line"]))
        if "formatted_message" in rom_info:
            suffix = ""
            unresolved = rom_info.get("unresolved_conversions", 0)
            if unresolved:
                suffix = "  (%d conversion(s) unresolved - only arg2/arg3 are captured)" % unresolved
            push('  formatted message : "%s"%s'
                 % (_truncate(rom_info["formatted_message"]), suffix))
        for note in rom_info.get("notes", []):
            push("  note              : %s" % note)

    if raw_stack_limit is not None:
        push("")
        push("Stack snapshot (from sp=%s upward)" % _addr(dump.sp))
        for row in snapshot_rows(dump, table, limit=raw_stack_limit or None):
            parts = ["  sp+0x%04X %s" % (row["index"] * 4, _addr(row["raw"]))]
            if row["spill"]:
                parts.append("hook stub spill: %s" % row["spill"])
            elif row["is_code"] or row["candidates"]:
                description = describe_candidates(row["candidates"])
                parts.append(("code: " if row["is_code"] else "data: ") + description)
            push("  ".join(parts))

    if ctx.warnings:
        push("")
        push("Warnings")
        for warning in ctx.warnings:
            push("  - %s" % warning)

    push("")
    return "\n".join(lines)


def _candidate_json(res: Resolution) -> Dict:
    return {
        "name": res.symbol.name,
        "address": res.symbol.address,
        "offset": res.offset,
        "exact": res.exact,
        "kind": res.symbol.kind,
        "block": res.symbol.block,
        "file": res.symbol.file,
    }


def build_json(dump: CrashDump, table: Optional[SymbolTable],
               frames: List[Frame], trace_truncated: bool,
               rom_info: Optional[Dict], ctx: ReportContext,
               thread_traces: Optional[List[Tuple[ThreadRecord, List[Frame], bool]]] = None,
               ) -> Dict:
    queue_analysis: QueueAnalysis = analyze_thread_queues(dump)

    def candidates_for(addr: int) -> List[Dict]:
        if table is None:
            return []
        resolved = table.resolve(addr, kind="function")
        if not resolved:
            resolved = [r for r in table.resolve(addr) if r.exact]
        return [_candidate_json(r) for r in resolved]

    payload: Dict = {
        "source": {
            "path": ctx.source_path,
            "record_offset": ctx.source_offset,
            "record_size": RECORD_SIZE,
        },
        "record_version": dump.record_version,
        "hook_id": dump.hook_id,
        "hook": dump.hook_name,
        "game_version": ctx.version,
        "game_version_source": ctx.version_source,
        "tick": dump.tick,
        "checksum": {
            "stored": dump.checksum_stored,
            "computed": dump.checksum_computed,
            "ok": dump.checksum_ok,
        },
        "pc": {"raw": dump.pc, "candidates": candidates_for(dump.pc)},
        "lr": {"raw": dump.lr, "thumb": bool(dump.lr & 1),
               "candidates": candidates_for(dump.lr & ~1)},
        "sp": dump.sp,
        "cpsr": cpsr_mod.decode(dump.cpsr),
        "registers": {"r%d" % i: value for i, value in enumerate(dump.regs)},
        "args": {"arg%d" % i: value for i, value in enumerate(dump.args)},
        "message": {
            "raw": dump.msg,
            "length": dump.msg_len,
        },
        "thread_count": dump.thread_count,
        "threads_written": dump.threads_written,
        "crashing_index": dump.crashing_index,
        "trigger_buttons": {
            "raw": dump.trigger_buttons,
            "held": decode_buttons(dump.trigger_buttons),
        },
        "complete": dump.complete,
        "sys": {
            "uplink_card_busy_timeouts": dump.sys.uplink_card_busy_timeouts,
            "uplink_card_lock_skips": dump.sys.uplink_card_lock_skips,
            "uplink_card_lock_waits": dump.sys.uplink_card_lock_waits,
            "mod_wake_count": dump.sys.mod_wake_count,
            "frame_flags": dump.sys.frame_flags,
            "queue_samples": [
                {"queue": s.queue, "head": s.head, "tail": s.tail}
                for s in dump.sys.queue_samples
            ],
        },
        "backtrace": [
            {
                "index": frame.index,
                "origin": frame.origin,
                "raw": frame.raw,
                "address": frame.address,
                "thumb": frame.thumb,
                "stack_offset": frame.stack_offset,
                "stack_address": frame.stack_address,
                "candidates": [_candidate_json(r) for r in frame.candidates],
            }
            for frame in frames
        ],
        "backtrace_truncated": trace_truncated,
        "threads": [
            {
                "index": record.index,
                "record_offset": record.record_offset,
                "thread_id": record.thread_id if record.known else None,
                "priority": record.priority if record.known else None,
                "state": record.state,
                "state_name": record.state_name,
                "pc": {"raw": record.pc, "candidates": candidates_for(record.pc & ~1)},
                "lr": {"raw": record.lr, "thumb": bool(record.lr & 1),
                       "candidates": candidates_for(record.lr & ~1)},
                "sp": record.sp,
                "stack_start": record.stack_start,
                "stack_end": record.stack_end,
                "snapshot_bytes": record.snapshot_bytes,
                "is_current": record.is_current,
                "sp_valid": record.sp_valid,
                "sp_outside": record.sp_outside,
                "registers": {"r%d" % i: value for i, value in enumerate(record.regs)},
                "queue": record.queue,
                "mutex": record.mutex,
                "link_prev": record.link_prev,
                "link_next": record.link_next,
                "thread_ptr": record.thread_ptr,
                "backtrace": [
                    {
                        "index": frame.index,
                        "origin": frame.origin,
                        "raw": frame.raw,
                        "address": frame.address,
                        "thumb": frame.thumb,
                        "stack_offset": frame.stack_offset,
                        "stack_address": frame.stack_address,
                        "candidates": [_candidate_json(r) for r in frame.candidates],
                    }
                    for frame in thread_frames
                ],
                "backtrace_truncated": thread_truncated,
            }
            for record, thread_frames, thread_truncated in (thread_traces or [])
        ],
        "queue_analysis": {
            "clean": queue_analysis.clean,
            "findings": [
                {"kind": f.kind, "detail": f.detail, "thread_ptrs": f.thread_ptrs}
                for f in queue_analysis.findings
            ],
            "chains": {
                "0x%08X" % q: ["0x%08X" % node for node in chain]
                for q, chain in queue_analysis.chains.items()
            },
        },
        "warnings": list(ctx.warnings),
    }
    if rom_info is not None:
        payload["rom_info"] = rom_info
    return payload
