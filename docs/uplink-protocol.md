# PMDSky uplink protocol v2

Single source of truth for the USB uplink wire protocol spoken between:

- the mod running on the NDS (`src/uplink/uplink.c`, `src/uplink/uplink_sampler.c`), and
- the PC reader (`PMD_EoS_Uplink_Reader/`).

Transport: USB CDC-ACM (VID 0x2020 / PID 0xD801) provided by the DSpico
firmware's local TinyUSB stack. The NDS ships bytes to the firmware over the
card bus (WRITE_DATA 0xE9) and reads host bytes back (READ_DATA 0xEA). The
firmware is a transparent byte pipe; **this protocol never requires firmware
changes** — it is designed around the baseline firmware's behavior:

- The card data phase is always 512 bytes; the mod pads short writes with
  zeros and the firmware only forwards the declared valid byte count, so the
  host receives exactly the bytes the mod wrote.
- READ_DATA (0xEA) returns 512 bytes: the host bytes waiting in the firmware
  RX ring (511 bytes max), zero-filled to 512. The mod cannot know how many
  bytes are valid, so host→device commands are **self-delimiting** and zero
  padding is skipped by the parser.
- The firmware RX ring holds at most 511 bytes, so the host paces its
  command batches (ACK flow below) and never overflows it.

## 1. Device → host

Two kinds of payloads share the CDC byte stream:

1. **Sample frames** (binary, variable length) — the telemetry stream.
2. **Control replies** (ASCII lines terminated by `\r\n`) — responses to
   host commands. They interleave with frames; the reader separates them by
   scanning for the frame magic.

### 1.1 Sample frame

Length `L = 16 + 4*N + 2` bytes, where `N = sample_count` (1..256):

| offset      | size | field        | notes                                             |
|-------------|------|--------------|---------------------------------------------------|
| 0..1        | 2    | magic        | `'P'`, `'M'`                                      |
| 2..5        | 4    | seq          | u32 LE, frame index, wraps at 2^32                |
| 6..9        | 4    | game_frame   | u32 LE, PLAY_TIME at sample time: `sec*60+frames` |
| 10..11      | 2    | sample_count | u16 LE, `N` (lets the reader self-synchronize)    |
| 12..13      | 2    | epoch        | u16 LE, bumped on every CONFIG_COMMIT             |
| 14..15      | 2    | reserved     | zero (header is 16 bytes on the wire)             |
| 16..16+4N-1 | 4N   | samples      | u32 LE each, one per configured address entry     |
| 16+4N..L-1  | 2    | checksum     | u16 LE, byte sum of bytes `0..16+4N-1` (u16 wrap) |

- Narrow reads (width 1 or 2) are zero-extended into the u32 sample.
- An entry whose pointer path fails a validation check (null pointer,
  address outside RAM) samples as `0` for that frame — it never crashes.
- Frames are shipped as one to three whole 512-byte card transactions per
  game frame (latency-first): the frame bytes are streamed into 512-byte
  chunks; only the last chunk of a frame may be shorter than 512 bytes.
  `N <= 123` → 1 transaction per frame; `N = 256` → 3 (~0.5% frame time).
- A frame whose shipping is interrupted (card lock unavailable at frame
  start, card bus wedged mid-frame) is aborted: partially shipped bytes
  leave a truncated frame on the wire (the reader resyncs on the next
  magic), the frame's seq is not consumed, and the mod counts it dropped.

### 1.2 Control replies

ASCII, `\r\n`-terminated, best-effort (a reply whose card transaction is
skipped is dropped; the host's timeouts recover):

| reply | meaning |
| ------- | --------- |
| `OK CONFIG count=<N> max=<M>` | CONFIG_BEGIN accepted; `N` = declared count, `M` = mod's max entries |
| `OK ENTRIES n=<K>` | `K` entries applied so far (cumulative); sent after any RX read that applied at least one entry |
| `OK STREAMING epoch=<E>` | CONFIG_COMMIT accepted; sampling auto-starts. Sent **before** the first frame so it does not compete with the frame flood; COMMIT is idempotently re-ackable (see below) |
| `OK ABORTED` | CONFIG_ABORT accepted (table discarded) |
| `ERR <REASON>` | command rejected; the whole config is discarded and the mod returns to idle |
| `UPLINK seq=<S> sent=<T> drop=<D> stack=<U> rx=<R> cfg=<A>/<N>` | PING reply: frame counters, main-thread stack usage in bytes, `R` = total nonzero host bytes ever parsed, `A`/`N` = applied/declared config entries. Diagnostics: after a failed upload, `rx` shows whether the mod ever received the entry bytes and `cfg` how far its table got |

`ERR <REASON>` values:

| reason | cause |
| -------- | ------- |
| `VER` | CONFIG_BEGIN version != 1 |
| `COUNT` | CONFIG_BEGIN count is 0 or > max; or COMMIT's applied count != declared count |
| `FULL` | CONFIG_ENTRY beyond the declared count (defensive) |
| `IDX` | CONFIG_ENTRY index is not the next sequential index |
| `WIDTH` | CONFIG_ENTRY width code is 3 (invalid) |
| `STEPS` | CONFIG_ENTRY step count > 4 |
| `OFFSET` | CONFIG_ENTRY step offset > 0x7FFF |
| `BASE` | CONFIG_ENTRY **direct** entry (no steps) base address is not in RAM. Pointer-path bases are accepted anywhere (see 2.1) |

`ENTRY`/`COMMIT`/`ABORT` received while no config is in progress are stale
bytes from an earlier aborted upload; they are consumed silently (no
reply), so they cannot interfere with a subsequent upload attempt.

## 2. Host → device

Commands arrive as a byte stream; complete commands are executed as their
bytes become available (commands may share a 512-byte RX block, and a
command may be split across blocks). Zero padding after the last command in
a block is skipped. Unknown nonzero bytes resynchronize the parser (dropped
one at a time).

| cmd | length | payload |
| ----- | -------- | --------- |
| `0x01` START | 1 | resume sampling |
| `0x02` STOP | 1 | pause sampling |
| `0x04` PING | 1 | → `UPLINK ...` reply |
| `0x10` CONFIG_BEGIN | 5 | `ver u8` (must be 1), `count u16 LE` (1..max), `reserved u8` (ignored) |
| `0x11` CONFIG_ENTRY | 8 + 3*S | `idx u16 LE`, `base u32 LE`, `meta u8`, then `S` steps |
| `0x12` CONFIG_COMMIT | 1 | finalize the table, start streaming |
| `0x13` CONFIG_ABORT | 1 | discard the table |

CONFIG_ENTRY `meta` bits:

- bits 0-1: width code — `0` → 1 byte, `1` → 2 bytes, `2` → 4 bytes, `3` invalid
- bits 2-4: step count `S` (0..4; 5..7 invalid)
- bits 5-7: reserved, ignored

Each step is 3 bytes: `offset u16 LE` (must be ≤ 0x7FFF) and `flags u8`
(bit 0 = dereference at `cur + offset`; remaining bits reserved, ignored).

### 2.1 Address entries

Every configured entry produces one u32 sample per frame. An entry is
either a direct address (`S = 0`) or a pointer path:

```
cur = base
for each step:
    cur += offset
    if deref:
        require in_ram(cur, 4)
        cur = *(uint32_t *)cur      (bytewise little-endian read)
require in_ram(cur, width)
sample = width bytes at cur        (bytewise little-endian read)
```

`in_ram(addr, len)` is true iff `addr >= 0x02000000 && addr + len <=
0x02400000` (ARM9 main RAM). Any failed check makes the sample `0` for that
frame. A null pointer fails automatically (0 is outside RAM). Paths are
re-evaluated every frame, so pointer-derived samples track the game live
and read 0 outside dungeons/menus etc.

Note what is and is not checked: the mod only ever **reads** at deref
targets and at the final address — the base itself is never read, it is
just the start of the arithmetic. A path's base (and any intermediate
value) may therefore be null or outside RAM: the entry is still accepted
into the table and simply samples `0` for every frame in which the chain
does not land in RAM. This is what makes dungeon variables work: connect
anywhere, and the dungeon-path entries stream 0 until the player enters a
dungeon. Only **direct** entries (`S = 0`) are RAM-checked at upload time
(early feedback for typos); their per-frame read is checked like any
other.

### 2.2 Config upload flow (host-paced)

```
host: CONFIG_BEGIN(count)            mod: OK CONFIG count=N max=M
host: batch 1 (whole entries, <= ~440 bytes, one Write)
                                     mod: OK ENTRIES n=<cumulative>
host: batch 2 ...                    mod: OK ENTRIES n=...
host: CONFIG_COMMIT                  mod: OK STREAMING epoch=E
```

- Batches contain **one entry each**: every host Write is a single USB
  packet (8-14 bytes). This is the only delivery mode the mod has ever
  processed reliably — multi-packet writes (larger batches = 3+ USB
  packets) never reached the mod's parser, while every single-packet
  command did. (The firmware's CDC RX path has only ever been exercised
  with single-packet transfers; v1's commands were all ≤ 6 bytes.)
- The host waits for `OK STREAMING` (~700 ms deadline). The reply can be
  skipped (the card lock may be busy the moment streaming starts), so the
  host **re-sends COMMIT** on timeout instead of restarting: the mod
  re-acks idempotently whenever its table is already complete
  (applied == declared), without changing state. If the COMMIT re-sends
  are exhausted, the host does **not** restart (CONFIG_ABORT would
  discard a table that is very likely applied and streaming); instead it
  sends a **PING** and decides from the reply's `cfg=A/N` field:
  `A == N` → the upload is complete (the mod committed and is streaming;
  the OK STREAMING reply was simply lost). `A < N` → the table is
  incomplete, restart. A **valid sample frame** whose sample_count equals
  the configured count is also accepted as commit confirmation (the mod
  streams only with a committed table and streams nothing while a config
  is in progress). During the upload the mod polls
  RX every frame (see 2.4), so a full batch is almost always delivered
  between two polls and round trips are ~1-2 frames (~16-33 ms).
- On any `ERR` (or a timeout) the host restarts from CONFIG_BEGIN; the mod
  has already discarded the partial table. Before CONFIG_BEGIN the host
  sends CONFIG_ABORT and waits ~400 ms: the abort silently discards any
  config state left by the failed attempt, and the wait gives the mod one
  RX poll (~250 ms) to drain stale bytes still in its ring — stale entry
  bytes applied to the fresh table would otherwise fail every retry with
  `ERR IDX`.
- The mod aborts a stalled config silently: ~2 s (120 frames) without any
  host bytes returns it to idle.
- CONFIG_BEGIN while streaming (or mid-config) restarts the configuration.
- CONFIG_ABORT discards the table entirely (a previously streaming table
  does not survive an abort; the host re-uploads).

### 2.3 Streaming gate

Frames flow only while sampling is started **and** the table is non-empty
**and** no config is in progress. There is no built-in table: with no
reader connected the mod streams nothing. CONFIG_COMMIT auto-starts
sampling; START/STOP pause and resume.

### 2.4 Mod RX polling

The mod reads the firmware RX block every 15 frames (~250 ms at 60 Hz),
unconditionally — this is how it notices a connecting reader. The rate is
the same during config uploads: raising it (e.g. per-frame polling) makes
the firmware's RX handler do its heavy prep (ring drain + 512-byte memset)
inside the card IRQ at 15x the validated rate, which wedges the card bus
and permanently kills the uplink. ~250 ms round trips are well within the
host's 2 s ACK timeouts, so a full upload takes ~1-2 s.
