// ---------------------------------------------------------------------------
// uplink.c
//
// ARM9-only DSpico USB autosplitter uplink for PMD:ES.
//
// Drives the DSpico's USB device controller through the game card ROM
// interface (MCCNT0/MCCNT1/MCCMD0/MCCMD1/MCD1), presenting a CDC-ACM serial
// device. A 36-byte little-endian payload of autosplitter values is streamed
// on the bulk IN endpoint at the frame rate.
//
// The uplink is opt-in and OFF by default (UplinkSetEnabled /
// HandleUplinkToggle): while disabled, UplinkTick() never touches the card
// ROM interface or the card locks at all, so the ROM behaves normally on
// emulators and on flashcarts without a DSpico. While enabled, everything is
// best-effort and is wrapped in Card_LockRom()/Card_UnlockRom(); a busy card
// simply skips the frame instead of blocking the game.
//
// Event draining is gated on the DSpico's cartridge IRQ line (REG_IF bit
// 20, asserted by the firmware while its event queue is non-empty):
// GET_EVENT is only issued when an event is actually queued, so no card
// transfer is ever left pending on an idle queue and the game's DLDI can
// never stall on it.
// A periodic idle probe (uplink_rom_probe_event) covers the case where
// the IRQ line never reaches the NDS and the flag is therefore never
// latched: a single GET_EVENT with a short inline window both detects that
// condition in the HUD status string (p0/p1/p2) and hands any captured
// event to the drain. A probe read that captured no word is aborted
// immediately (never left in flight); one that captured a word but whose
// ENABLE bit is still set is settled by the in-flight poller (waiting for
// the firmware to clear ENABLE itself before any abort).
//
// A timed-out card transfer is never reset immediately: a grace period
// (UPLINK_STUCK_GRACE_TICKS) gives a slow DSpico time to finish, and only
// a transfer still stuck after the grace triggers a cart reset -- capped
// at UPLINK_MAX_CARD_RESETS per session, after which the card is declared
// dead (the DSpico needs a power cycle; see the DSpico README). This keeps
// the cart RST line from flapping while the DSpico's USB is active, which
// is a documented DSpico firmware bug trigger.
//
// The low-level register layout and command encoding match TwlightSDK's
// libtwl/card/card.h and the DSpico USB reference (DSPicoUsb.h /
// DSPicoUsbInEndpoint.cpp) exactly.
// ---------------------------------------------------------------------------

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "uplink.h"

// ===========================================================================
// DS card ROM interface registers (single card slot = the DSpico).
// Addresses/values mirror TwlightSDK libtwl/card/card.h.
// ===========================================================================
#define REG_MCCNT0  (*(volatile uint16_t*)0x040001A0)
#define REG_MCCNT1  (*(volatile uint32_t*)0x040001A4)
#define REG_MCCMD0  (*(volatile uint32_t*)0x040001A8)
#define REG_MCCMD1  (*(volatile uint32_t*)0x040001AC)
#define REG_MCD1    (*(volatile uint32_t*)0x04100010)

// DMA3 channel (feeds MCD1 for bulk IN writes, as the reference does).
#define REG_DMA3SAD (*(volatile uint32_t*)0x040000D4)
#define REG_DMA3DAD (*(volatile uint32_t*)0x040000D8)
#define REG_DMA3CNT (*(volatile uint32_t*)0x040000DC)
// DS ARM9 interrupt flag register. Bit 20 (SLOT1_IREQ) is the cartridge
// device-interrupt flag: the DSpico drives the cartridge IRQ line high while
// its USB event queue is non-empty (see usbEventQueue.c in the DSpico
// firmware: line up on enqueue, line down when the queue is drained). We
// poll this flag instead of issuing GET_EVENT on an empty queue -- an
// empty-queue GET_EVENT leaves the card transfer pending until the next
// event, and the game's DLDI (which waits for the card to be idle without a
// timeout) would stall on it.
#define REG_IF          (*(volatile uint32_t*)0x04000214)
#define IRQ_SLOT1_IREQ  (1u << 20)
// DS ARM9 interrupt-enable register. NOTE: the real IE register is at
// 0x04000210, paired with REG_IF at 0x04000214 (the DSpico reference RTOS
// vector reads exactly those). 0x04000218 is IME2, a different register, and
// the old 0x04000218 address made uplink_card_irq_mask() a no-op: the
// firmware kept dispatching AND acking bit 20 (its dispatcher acks exactly
// IE & IF), clearing the latched flag before the UplinkTick() once-per-frame
// poll could see it -- the drain never ran and enumeration dead-locked at
// WAIT ("device not recognized" on the host).
//
// Bit 20 (SLOT1_IREQ) controls whether the firmware dispatches the cartridge
// IRQ to the game. While the uplink is on the DSpico drives that same line
// for USB events, and the firmware mis-reads it as a card pull-out ("game
// card was removed"). Clearing this bit while the uplink is active stops the
// dispatch -- and since the dispatcher only acks enabled+pending bits, the
// latched flag stays in REG_IF until uplink_card_irq_ack() clears it, which
// is what keeps uplink_card_irq_pending() from going blind. Restored on
// disable.
//
// This only stops the firmware IRQ dispatch path. The card library also
// detects pull-outs by polling (Cardi_CheckPulledOutCore compares a card-state
// token and fires the pull-out handler itself), and the engine reaches the
// card-pull functions through runtime function pointers, so the uplink
// additionally suppresses the pull-out handler entries via trampolines in
// patches/patch.asm (gated on uplink_enabled) and clears the pull-out flag in
// UplinkTick().
#define REG_IE          (*(volatile uint32_t*)0x04000210)
// Control word for the MCD1 feed DMA. It MUST be ARM9-encoded: on the ARM9
// the DMACNT timing field is 3 bits (bits 27-29) and "driven by DS gamecard"
// is mode 5 (0x28000000). The reference value 0xA6400001
// (DSPicoUsbInEndpoint.cpp, an ARM7 build) is the ARM7 encoding (2-bit
// field, mode 2); on the ARM9 it decodes to "main memory display FIFO"
// mode, which never fires -> MCD1 is never fed, every LEN_512 write stalls
// with MCCNT1_ENABLE left set, and the game's DLDI hangs on its next ROM
// read.
//   0x80000000 start | 0x28000000 timing 5 (DS card slot)
//   0x04000000 32-bit | 0x02000000 repeat | 0x00400000 dst fixed
//   0x00000001 count 1
#define UPLINK_DMA3_FEED_CNT 0xAE400001u

// --- MCCNT0 bits ---
#define MCCNT0_MODE_MASK      (1u << 13)
#define MCCNT0_MODE_ROM       (0u << 13)
#define MCCNT0_ROM_XFER_IRQ   (1u << 14)
#define MCCNT0_ENABLE         (1u << 15)

// --- MCCNT1 bits ---
#define MCCNT1_LATENCY1(x)             ((x) & 0x1FFFu)
#define MCCNT1_READ_DATA_DESCRAMBLE    (1u << 13)
#define MCCNT1_CLOCK_SCRAMBLER         (1u << 14)
#define MCCNT1_APPLY_SCRAMBLE_SEED     (1u << 15)
#define MCCNT1_LATENCY2(x)             (((x) << 16) & 0x3F0000u)
#define MCCNT1_CMD_SCRAMBLE            (1u << 22)
#define MCCNT1_DATA_READY              (1u << 23)
#define MCCNT1_LEN_0                   (0u << 24)
#define MCCNT1_LEN_512                 (1u << 24)
#define MCCNT1_LEN_4                   (7u << 24)
#define MCCNT1_CLK_6_7_MHZ             (0u << 27)
#define MCCNT1_LATENCY_CLK             (1u << 28)
#define MCCNT1_RESET_OFF               (1u << 29)
#define MCCNT1_DIR_READ                (0u << 30)
#define MCCNT1_DIR_WRITE               (1u << 30)
#define MCCNT1_ENABLE                  (1u << 31)  // read: transfer in progress

// ===========================================================================
// DSpico USB command encoding (from DSPicoUsb.h)
// ===========================================================================
#define DSPICO_CMD_USB_COMMAND(command, arguments) \
    (0xE800000000000000ull | ((uint64_t)(command) << 48) | (uint64_t)(arguments))

#define DSPICO_CMD_USB_INIT                DSPICO_CMD_USB_COMMAND(1, 0)
#define DSPICO_CMD_USB_BEGIN_SET_ADDRESS   DSPICO_CMD_USB_COMMAND(2, 0)
#define DSPICO_CMD_USB_CONNECT             DSPICO_CMD_USB_COMMAND(4, 0)
#define DSPICO_CMD_USB_DISCONNECT          DSPICO_CMD_USB_COMMAND(5, 0)
#define DSPICO_CMD_USB_SOF_DISABLE         DSPICO_CMD_USB_COMMAND(7, 0)
#define DSPICO_CMD_USB_EP_CLOSE_ALL        DSPICO_CMD_USB_COMMAND(8, 0)
#define DSPICO_CMD_USB_FINISH_SET_ADDRESS(addr) \
    DSPICO_CMD_USB_COMMAND(12, ((uint64_t)(addr) << 40))
#define DSPICO_CMD_USB_EP_OPEN(ep, maxPktSize, xfer) \
    DSPICO_CMD_USB_COMMAND(13, (((uint64_t)(ep) << 40) \
                                | ((uint64_t)((xfer) & 3u) << 32) \
                                | ((uint64_t)(maxPktSize) & 0x7FFu)))
#define DSPICO_CMD_USB_CLEAR_EVENT_QUEUE   DSPICO_CMD_USB_COMMAND(14, 0)
#define DSPICO_CMD_USB_DEINIT              DSPICO_CMD_USB_COMMAND(15, 0)
// BEGIN_TRANSFER(endpoint, offset, length): arm a transfer on the given
// endpoint. With endpoint 0x00 (EP0 OUT) and length 0 this arms the
// zero-length status OUT of a control transfer, so the host's status
// packet is ACKed instead of NAKed (the reference host does the same).
#define DSPICO_CMD_USB_BEGIN_TRANSFER(ep, offset, length) \
    DSPICO_CMD_USB_COMMAND(16, (((uint64_t)(ep) << 40) \
                                | ((uint64_t)(offset) << 32) \
                                | (uint64_t)(length)))
#define DSPICO_CMD_USB_INTERRUPT_ENABLE    DSPICO_CMD_USB_COMMAND(17, 0)
#define DSPICO_CMD_USB_INTERRUPT_DISABLE   DSPICO_CMD_USB_COMMAND(18, 0)

#define DSPICO_CMD_USB_WRITE_DATA(offset, endpoint, isLast, totalLength) \
    (0xE900000000000000ull \
        | ((uint64_t)(offset) << 48) \
        | ((uint64_t)(endpoint) << 40) \
        | ((uint64_t)(isLast) << 32) \
        | (uint64_t)(totalLength))
#define DSPICO_CMD_USB_GET_EVENT           0xEB00000000000000ull

// MCCNT1 setting word for the different operation classes.
#define UPLINK_MCCNT1_CMD   (MCCNT1_DIR_READ | MCCNT1_RESET_OFF | MCCNT1_CLK_6_7_MHZ | \
                             MCCNT1_LEN_0 | MCCNT1_CMD_SCRAMBLE | MCCNT1_LATENCY2(0) | \
                             MCCNT1_CLOCK_SCRAMBLER | MCCNT1_READ_DATA_DESCRAMBLE | MCCNT1_LATENCY1(0))
#define UPLINK_MCCNT1_EVENT (MCCNT1_DIR_READ | MCCNT1_RESET_OFF | MCCNT1_CLK_6_7_MHZ | \
                             MCCNT1_LEN_4 | MCCNT1_CMD_SCRAMBLE | MCCNT1_LATENCY2(4) | \
                             MCCNT1_CLOCK_SCRAMBLER | MCCNT1_READ_DATA_DESCRAMBLE | MCCNT1_LATENCY1(0))
#define UPLINK_MCCNT1_WRITE (MCCNT1_DIR_WRITE | MCCNT1_RESET_OFF | MCCNT1_CLK_6_7_MHZ | \
                             MCCNT1_LEN_512 | MCCNT1_CMD_SCRAMBLE | MCCNT1_LATENCY2(8) | \
                             MCCNT1_CLOCK_SCRAMBLER | MCCNT1_READ_DATA_DESCRAMBLE | MCCNT1_LATENCY1(0))

// IN endpoint address for the data stream (0x81) and control (EP0 IN = 0x80).
#define UPLINK_EP0_IN      0x80u

// ===========================================================================
// Small helpers
// ===========================================================================
static inline uint32_t uplink_bswap32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8)
         | ((x & 0x00FF0000u) >> 8)  | ((x & 0xFF000000u) >> 24);
}
static inline uint64_t uplink_bswap64(uint64_t x) {
    uint32_t lo = (uint32_t)(x & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(x >> 32);
    return ((uint64_t)uplink_bswap32(lo) << 32) | uplink_bswap32(hi);
}

// CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF). The PC reader implements the
// identical algorithm.
static uint16_t uplink_crc16(const uint8_t* data, uint32_t len) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else               crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ===========================================================================
// Card lock + low-level ROM primitives
// ===========================================================================
static int  uplink_lock_id = -1;
static bool uplink_lock_ready = false;
// Set when the card is wedged beyond the reset budget (or a reset failed):
// every card primitive becomes a no-op until the user toggles the uplink
// off/on (and power-cycles the DSpico -- the README documents a firmware
// bug where USB + cart reset leaves the DSpico's USB dead until power-off).
static bool uplink_card_dead = false;

// --- DSpico mailbox (GET_EVENT) read state -------------------------------
// GET_EVENT is a mailbox read, not a regular transfer. The DSpico firmware
// makes the event word available via MCCNT1_DATA_READY and clears
// MCCNT1_ENABLE only when it has an event to deliver -- a read issued
// against an empty queue is left pending by design until the next event
// arrives. The reference DCD (libtwl card_romCpuRead) simply loops while
// ENABLE is set, capturing MCD1 whenever DATA_READY latches: a pending
// read is a normal state, never an error, and it never resets the card.
//
// UplinkTick() runs on the game's main thread (the reference runs in a
// dedicated RTOS thread and can spin), so a pending read is handed to a
// per-frame poller: uplink_rom_get_event() waits only a few ms inline, then
// leaves the read running in the card controller hardware
// (UPLINK_ROM_INFLIGHT). UplinkTick() watches DATA_READY/ENABLE each frame;
// after UPLINK_EVENT_MAX_AGE_TICKS the read is ABORTED (ENABLE dropped --
// the same stop the DLDI itself uses on error). It is never treated as a
// stuck card: no stuck recovery, no cart reset. A cart reset while the
// DSpico's USB is active triggers a documented firmware bug (the USB link
// then dies until the DSpico is power cycled) -- exactly the flap this
// mailbox handling replaces.
#define UPLINK_EVENT_INLINE_SPIN   100000u  // ~3 ms at 33 MHz (IO reads
                                             // make it longer in practice)
#define UPLINK_EVENT_MAX_AGE_TICKS 20u      // ~0.32 s total before aborting
#define UPLINK_EVENT_STALE_MISSES  5u       // zero-event timeouts before acking
#define UPLINK_EVENT_SETTLE_MAX_TICKS 8u    // word captured but ENABLE still
                                             // set: ticks to let the firmware
                                             // clear it itself before aborting
static bool     uplink_event_inflight = false;
static uint32_t uplink_event_age      = 0;
static uint32_t uplink_event_settle_age = 0;
static bool     uplink_event_settle_only = false;
static uint32_t uplink_event_word     = 0;
static bool     uplink_event_word_got = false;
static uint32_t uplink_event_misses   = 0;

// Idle probe (see the probe block in UplinkTick): on some DSpico firmware
// states the cartridge IRQ line never reaches the NDS, so REG_IF bit 20 is
// never latched even though the firmware queues events -- the flag-gated
// drain would then never run. A periodic single GET_EVENT read (short
// inline window; a wordless pending read is aborted immediately, a read
// that captured a word is settled by the in-flight poller) both detects
// that condition and, when it captures a real event word, hands it to the
// drain via uplink_event_word_got. Verdicts for the HUD:
// 0 = queue answered empty, 1 = real event found, 2 = read left pending
// (aborted; documented empty-queue behavior), NA = not probed this session.
#define UPLINK_PROBE_INTERVAL_SLOW  30u  // ~0.5 s: waiting for the host
#define UPLINK_PROBE_INTERVAL_FAST  8u   // ~0.13 s: enumerating/streaming
#define UPLINK_PROBE_VERDICT_NA     3u
static uint32_t uplink_probe_ticks   = 0;
static uint8_t  uplink_probe_verdict = UPLINK_PROBE_VERDICT_NA;

// The last event word physically delivered to MCD1 (as consumed by the
// drain). The probe uses it to recognize a stale MCD1 readout when a
// mailbox read completes without a DATA_READY latch.
static uint32_t uplink_last_delivered = 0;

// Diagnostics (temporary): first vs last MCD1 latch observed across the
// most recent GET_EVENT spin/probe window. A difference indicates MCD1
// rollover between DATA_READY latches (the DSpico rolling MCD1 toward the
// next word while DATA_READY stays asserted). Shown on the HUD ENUM line as
// w (first, the value the single-shot read uses) and l (last); w == l means
// no rollover, w != l confirms it.
static uint32_t uplink_diag_first = 0;
static uint32_t uplink_diag_last  = 0;
static bool     uplink_diag_first_seen = false;
static bool     uplink_diag_last_seen  = false;

// True if the transfer currently in flight is one of our 4-byte mailbox
// reads. The game's DLDI uses LEN_512 and our commands LEN_0, so the LEN
// field disambiguates a stale mailbox-read ENABLE bit from a DLDI read in
// flight: only a LEN_4 transfer may be aborted by the uplink.
static inline bool uplink_xfer_is_event_read(void) {
    return (REG_MCCNT1 & 0x7000000u) == MCCNT1_LEN_4;
}

// Drops the stale ENABLE bit of our own pending mailbox read. The caller
// must hold the card lock.
static void uplink_event_abort(void) {
    if (uplink_xfer_is_event_read())
        REG_MCCNT1 &= ~MCCNT1_ENABLE;
}

static void uplink_ensure_lock(void) {
    if (!uplink_lock_ready) {
        uplink_lock_id = OS_GetLockID();
        uplink_lock_ready = true;
    }
}

// True if a card ROM transfer is currently in progress. Safe to call without
// holding the lock (it only reads the control register).
static bool uplink_card_busy(void) {
    return (REG_MCCNT1 & MCCNT1_ENABLE) != 0;
}

// True while the DSpico's cartridge IRQ flag is latched, i.e. its USB event
// queue holds (or held since the last ack) at least one event. The flag is
// rising-edge latched and cleared write-1-to-clear, so it is only acked
// after the queue has been fully drained -- a partially drained queue must
// keep it set so the drain resumes next tick.
static bool uplink_card_irq_pending(void) {
    return (REG_IF & IRQ_SLOT1_IREQ) != 0;
}

static void uplink_card_irq_ack(void) {
    REG_IF = IRQ_SLOT1_IREQ;
}

// Mask / unmask the firmware's card IRQ dispatch (REG_IE bit 20). While the
// uplink is enabled the DSpico asserts the cartridge IRQ line for every USB
// event it enqueues; left enabled, the firmware dispatches it to the game's
// card subsystem, which mis-reads it as a card pull-out and shows the "game
// card was removed" screen. Masking it stops that. The DSpico still latches the
// flag in REG_IF, so uplink_card_irq_pending() keeps working.
static void uplink_card_irq_mask(bool mask) {
    if (mask) REG_IE &= ~IRQ_SLOT1_IREQ;
    else      REG_IE |=  IRQ_SLOT1_IREQ;
}

// Start a card ROM transfer as ONE tight sequence: a final idle re-check
// followed immediately by the MCCMD0 / MCCNT0 / MCCNT1 writes, with no
// call boundaries in between. The game's DLDI does not take the ROM lock,
// so a DLDI read that slipped in between an idle check and the control
// register writes would be corrupted (and the DLDI's no-timeout completion
// wait would then hang the game). Keeping the whole sequence to a few
// instructions shrinks that window to sub-microsecond; closing it entirely
// would need a DLDI patch. The caller must hold the ROM lock and have
// already probed idle (uplink_card_busy). Returns 0 on start, -1 if the
// card became busy in the meantime (the caller just backs off and retries).
// volatile accesses are not reordered by the compiler, so the sequence
// stays contiguous in the emitted code.
static int uplink_rom_start(uint64_t cmd, uint32_t settings) {
    if (REG_MCCNT1 & MCCNT1_ENABLE) return -1;
    *(volatile uint64_t*)0x040001A8 = uplink_bswap64(cmd);
    uint16_t m = REG_MCCNT0;
    m = (uint16_t)((m & (uint16_t)~(MCCNT0_MODE_MASK | MCCNT0_ROM_XFER_IRQ))
                   | MCCNT0_MODE_ROM
                   | MCCNT0_ENABLE);
    REG_MCCNT0 = m;
    REG_MCCNT1 = MCCNT1_ENABLE | settings;
    return 0;
}

// Waits for the transfer to finish. Returns false on timeout; the caller
// then arms the two-stage stuck recovery (uplink_stuck_arm) -- the card
// is never reset right away (see uplink_stuck_poll).
static bool uplink_rom_wait_busy(void) {
    // Generous on purpose. A DSpico that is busy servicing USB (an
    // enumeration burst, a bus reset, the host re-polling) can take far
    // longer than 6 ms to answer a card command, and the old 6 ms timeout
    // fired on healthy hardware -- every false timeout then toggled the
    // cart RST line, which resets the DSpico's USB state machine and
    // triggers a documented DSpico firmware bug (the USB link dies until
    // the DSpico is power cycled). Real transfers complete in
    // microseconds, so 48 ms is only reached when the card is genuinely
    // slow or wedged.
    uint32_t timeout = 1600000u;  // ~48 ms at 33 MHz
    while (REG_MCCNT1 & MCCNT1_ENABLE) {
        if (timeout-- == 0u) return false;
    }
    return true;
}

// Result codes shared by the ROM primitives:
//   1 = operation completed, 0 = skipped because the card was busy (a normal
//       DLDI read in flight -- just retry next tick), -1 = timed out (a
//       command/write the DSpico did not complete; the two-stage stuck
//       recovery is armed and the caller backs off for a while),
//       2 = mailbox read left running in the card controller; UplinkTick()
//       polls it next frames (UPLINK_ROM_INFLIGHT).
#define UPLINK_ROM_OK     1
#define UPLINK_ROM_BUSY   0
#define UPLINK_ROM_TIMEOUT (-1)
#define UPLINK_ROM_INFLIGHT 2

// Tags identifying which operation a timed-out transfer came from; shown
// in the HUD status ("BUSY ...") while the stuck grace period runs.
#define UPLINK_OP_INIT_BASE 1u   // +0..+7: CLEAR_QUEUE / INIT / BEGIN_SET_ADDRESS / EP_OPEN x2 / INTERRUPT_ENABLE / SOF_DISABLE / CONNECT
#define UPLINK_OP_EVENT     100u // event queue command (CLEAR_EVENT_QUEUE); a mailbox
                                 // read itself never times out (see above)
#define UPLINK_OP_WRITE     200u // bulk / control IN write
#define UPLINK_OP_DEINIT    300u // teardown command

// Two-stage stuck recovery (defined with the state machine below). A
// transfer timeout "arms" a grace period; UplinkTick() polls the card each
// tick (uplink_stuck_poll) and only resets the cart if the transfer is
// STILL stuck when the grace expires, and only up to UPLINK_MAX_CARD_RESETS
// times per session. A cart reset while the DSpico's USB is active is a
// documented DSpico firmware bug trigger, so the goal is that this path
// almost never runs.
static void uplink_stuck_arm(uint32_t op_tag);
static void uplink_stuck_poll(void);

// Send a simple (no payload) command such as INIT/CONNECT/EP_OPEN/SET_ADDRESS
// and wait for it to complete.
static int uplink_rom_cmd(uint64_t cmd, uint32_t op_tag) {
    uplink_ensure_lock();
    if (uplink_card_dead) return UPLINK_ROM_BUSY;
    if (uplink_card_busy()) return UPLINK_ROM_BUSY;  // fast path, no lock needed
    Card_LockRom((uint16_t)uplink_lock_id);
    // The final idle re-check and the register writes are one tight
    // sequence inside uplink_rom_start (DLDI does not take this lock, so a
    // ROM read may have started since the probe above; writing MCCNT0/1
    // mid-transfer would corrupt the in-flight read and hang the game).
    if (uplink_rom_start(cmd, UPLINK_MCCNT1_CMD) != 0) {
        Card_UnlockRom((uint16_t)uplink_lock_id);
        return UPLINK_ROM_BUSY;
    }
    if (uplink_rom_wait_busy()) {
        Card_UnlockRom((uint16_t)uplink_lock_id);
        return UPLINK_ROM_OK;
    }
    // Timeout: arm the two-stage stuck recovery (grace period first, cart
    // reset only if still stuck -- see uplink_stuck_poll). Never reset
    // right away: the DSpico may just be busy with USB, and a cart reset
    // while its USB state machine is active triggers a documented DSpico
    // firmware bug.
    uplink_stuck_arm(op_tag);
    Card_UnlockRom((uint16_t)uplink_lock_id);
    return UPLINK_ROM_TIMEOUT;
}

// Start one GET_EVENT mailbox read and wait for the word with a short
// inline window. The DSpico delivers the word via MCCNT1_DATA_READY and
// clears MCCNT1_ENABLE when the read completes -- or never, if the queue
// was empty (the read is then left pending until the next event; see the
// mailbox note above). Returns:
//   UPLINK_ROM_OK      -- *out_event holds the event word
//   UPLINK_ROM_BUSY    -- the card was busy (a DLDI read); retry next tick
//   UPLINK_ROM_INFLIGHT-- the read is still pending; it keeps running in
//                         the card controller hardware and UplinkTick()
//                         polls it (uplink_event_inflight is set)
static int uplink_rom_get_event(uint32_t* out_event) {
    uplink_ensure_lock();
    if (uplink_card_dead) return UPLINK_ROM_BUSY;
    if (uplink_card_busy()) return UPLINK_ROM_BUSY;  // fast path, no lock needed
    uplink_event_word_got = false;  // a fresh read
    Card_LockRom((uint16_t)uplink_lock_id);
    if (uplink_rom_start(DSPICO_CMD_USB_GET_EVENT, UPLINK_MCCNT1_EVENT) != 0) {
        Card_UnlockRom((uint16_t)uplink_lock_id);
        return UPLINK_ROM_BUSY;
    }
    // Wait for the word with a short inline window. The reference
    // (card_romCpuRead) captures MCD1 on the FIRST DATA_READY latch and stops
    // using MCD1; a spin that keeps running while ENABLE stays set overwrites
    // the word on each later latch, so if the DSpico rolls MCD1 toward the
    // next queued word while DATA_READY stays asserted, the value finally used
    // is the last (rolled-over) latch, not the real word. So record the first
    // latch as the word and the last latch only for the HUD diagnostic, then
    // stop.
    uint32_t timeout = UPLINK_EVENT_INLINE_SPIN;
    bool first_latch_of_spin = false;
    while (REG_MCCNT1 & MCCNT1_ENABLE) {
        if (REG_MCCNT1 & MCCNT1_DATA_READY) {
            uint32_t w = REG_MCD1;
            if (!first_latch_of_spin) {
                // First latch of this spin: refresh the diagnostic (a no-latch
                // spin keeps the previous values, so the HUD does not show a
                // misleading 0).
                uplink_diag_first = w; uplink_diag_last = w;
                uplink_diag_first_seen = true; uplink_diag_last_seen = true;
                first_latch_of_spin = true;
            } else {
                uplink_diag_last = w;
            }
        }
        if (timeout-- == 0u) break;
    }
    if (first_latch_of_spin) {
        // Single-shot: use the FIRST latch of this spin, not the last
        // (a rolled-over MCD1).
        uplink_event_word = uplink_diag_first;
        uplink_event_word_got = true;
    }
    if (uplink_event_word_got || !(REG_MCCNT1 & MCCNT1_ENABLE)) {
        if (!uplink_event_word_got) {
            // Completed without a DATA_READY latch: no word was delivered, so
            // MCD1 still holds the previous (stale) word -- reading it here
            // assembled the SAME garbage value for both halves of a SETUP pair
            // (observed 0x69572065 on the first enumeration SETUP, HUD
            // "w6957..."). The reference (card_romCpuRead) only ever captures
            // MCD1 on a DATA_READY latch; it never reads MCD1 after the
            // transfer closes without one. Arm the all-ones "no data" sentinel
            // instead: the drain treats it like a busy answer (first word ->
            // break, second word -> bounded re-read) and the latched IRQ
            // re-drives a fresh read, so the DSpico gets another chance to
            // deliver the real word.
            uplink_event_word = 0x7FFFFFFFu;
        }
        Card_UnlockRom((uint16_t)uplink_lock_id);
        uplink_last_delivered = uplink_event_word;
        *out_event = uplink_event_word;
        if (uplink_event_word_got && (REG_MCCNT1 & MCCNT1_ENABLE)) {
            // The word was captured and delivered, but the DSpico has not
            // cleared ENABLE yet (still finalizing the read). Hand the read
            // to the in-flight poller in settle-only mode (it waits for
            // the firmware to clear ENABLE itself before any abort) instead
            // of aborting mid-finalization here -- the follow-up read of a
            // split SETUP pair is the one that hangs when a finalization
            // is aborted.
            uplink_event_word_got = false;  // the word is in *out_event
            uplink_event_inflight = true;
            uplink_event_settle_only = true;
            uplink_event_age = 0;
            uplink_event_settle_age = 0;
        } else {
            uplink_event_word_got = false;
            uplink_event_inflight = false;
        }
        uplink_event_misses = 0;
        return UPLINK_ROM_OK;
    }
    // Still pending after the inline window: the DSpico either had an empty
    // queue (pending by design until the next event) or is busy servicing
    // USB. Leave the read running in hardware; UplinkTick() polls it each
    // frame without blocking the game.
    uplink_event_inflight = true;
    uplink_event_age = 0;
    uplink_event_settle_age = 0;
    uplink_event_settle_only = false;
    Card_UnlockRom((uint16_t)uplink_lock_id);
    return UPLINK_ROM_INFLIGHT;
}

// Next event word for the drain loop: a word already captured by the
// in-flight poller (if any), else a fresh mailbox read. Same result codes
// as uplink_rom_get_event().
static int uplink_event_next(uint32_t* out_event) {
    if (uplink_event_word_got) {
        uplink_last_delivered = uplink_event_word;
        *out_event = uplink_event_word;
        uplink_event_word_got = false;
        uplink_event_inflight = false;
        uplink_event_misses = 0;
        return UPLINK_ROM_OK;
    }
    return uplink_rom_get_event(out_event);
}

// A LEN_4 mailbox readout with every data bit set -- 0xFFFFFFFF raw, or
// 0x7FFFFFFF after the "last" bit is stripped -- means the card bus drove
// no data: the DSpico completed the GET_EVENT read without answering (it
// was busy servicing USB, which is exactly what enumeration looks like).
// Such a readout is never a valid event word or SETUP second word. It is
// specifically dangerous because 0x7FFFFFFF passes the SETUP_RECEIVED
// marker test ((raw >> 30) == 1), so an unguarded all-ones second word
// assembles into a bogus bRequest=0xFF / bmRequestType=0xFF request that
// the handler acks with a zero-length status; the host then times out and
// enumeration stalls (HUD "ENUM ffffffff x0").
static bool uplink_word_is_allones(uint32_t word) {
    return word == 0xFFFFFFFFu || word == 0x7FFFFFFFu;
}

// A single GET_EVENT read for the idle probe: the same short inline window
// as uplink_rom_get_event(), but a read still pending after the window is
// ABORTED immediately (the stale ENABLE bit is dropped) -- a probe read
// that captured no word is never left in flight. A read that DID capture
// a word but whose ENABLE bit is still set (the firmware is still
// finalizing the read) is handed to the in-flight poller to settle,
// because dropping ENABLE mid-finalization can leave the firmware's card
// state machine confused (which is what hangs the follow-up read of a
// split SETUP pair). So a game ROM read can only be delayed by the inline
// window plus a settle cycle (a few ms to ~150 ms), and the disable path
// still settles any in-flight LEN_4 read before deinit. Returns:
//    1 -- a real event word was captured (armed in uplink_event_word /
//         uplink_event_word_got for the drain's uplink_event_next())
//    0 -- the queue answered empty (USB_EVENT_NONE)
//    2 -- the read was left pending by the firmware (documented behavior
//         for an empty queue) and had to be aborted: no events queued
//   -1 -- the card is dead or busy; no probe was attempted
static int uplink_rom_probe_event(void) {
    uplink_ensure_lock();
    if (uplink_card_dead) return -1;
    if (uplink_card_busy()) return -1;  // fast path, no lock needed
    uplink_event_word_got = false;  // a fresh read
    Card_LockRom((uint16_t)uplink_lock_id);
    if (uplink_rom_start(DSPICO_CMD_USB_GET_EVENT, UPLINK_MCCNT1_EVENT) != 0) {
        Card_UnlockRom((uint16_t)uplink_lock_id);
        return -1;
    }
    uint32_t timeout = UPLINK_EVENT_INLINE_SPIN;
    bool first_latch_of_spin = false;
    while (REG_MCCNT1 & MCCNT1_ENABLE) {
        if (REG_MCCNT1 & MCCNT1_DATA_READY) {
            // An all-ones latch is no data (the DSpico was busy answering);
            // ignore it so the read settles as "no word" below.
            uint32_t w = REG_MCD1;
            if (!uplink_word_is_allones(w)) {
                if (!first_latch_of_spin) {
                    uplink_diag_first = w; uplink_diag_last = w;
                    uplink_diag_first_seen = true; uplink_diag_last_seen = true;
                    first_latch_of_spin = true;
                } else {
                    uplink_diag_last = w;
                }
            }
        }
        if (timeout-- == 0u) break;
    }
    if (first_latch_of_spin) {
        // Single-shot: use the FIRST latch of this spin, not the last
        // (a rolled-over MCD1).
        uplink_event_word = uplink_diag_first;
        uplink_event_word_got = true;
    }
    if (uplink_event_word_got) {
        // A fresh word was delivered. If the DSpico has not cleared ENABLE
        // yet it is still finalizing the read: hand the read to the
        // in-flight poller (it settles it on the next tick(s), waiting for
        // the firmware to clear ENABLE itself before any abort) instead of
        // aborting mid-finalization here. The armed word (word_got) is
        // consumed by the drain on the tick the settle completes.
        if (REG_MCCNT1 & MCCNT1_ENABLE) {
            uplink_event_inflight = true;
            uplink_event_age = 0;
            uplink_event_settle_age = 0;
        }
        Card_UnlockRom((uint16_t)uplink_lock_id);
        uplink_last_delivered = uplink_event_word;
        return 1;
    }
    if (!(REG_MCCNT1 & MCCNT1_ENABLE)) {
        // Completed without a DATA_READY latch (defensive): MCD1 may hold
        // USB_EVENT_NONE, the last delivered word (stale), all-ones (no
        // data -- the DSpico was busy), or a word we missed the latch for.
        // 0 / the already-consumed word / all-ones = empty; anything else
        // is armed for the drain (which validates it).
        uint32_t word = REG_MCD1;
        Card_UnlockRom((uint16_t)uplink_lock_id);
        if (word == 0 || word == uplink_last_delivered || uplink_word_is_allones(word)) return 0;
        uplink_event_word = word;
        uplink_event_word_got = true;
        uplink_last_delivered = word;
        return 1;
    }
    // Still pending after the inline window: the queue is empty (a read on
    // an empty queue stays pending until the next event) or the firmware is
    // busy. Abort immediately -- a probe is never left in flight.
    uplink_event_abort();
    Card_UnlockRom((uint16_t)uplink_lock_id);
    return 2;
}

// Submit a bulk IN block (<= 512 bytes) to an endpoint using the DMA3 feed,
// exactly like the DSpico reference SendUsbBlock(). A length of 0 issues a
// zero-length transfer (used for the EP0 control status/data).
static int uplink_rom_write_in(const uint8_t* buffer, uint8_t endpoint, uint32_t length) {
    uplink_ensure_lock();
    if (uplink_card_dead) return UPLINK_ROM_BUSY;
    if (uplink_card_busy()) return UPLINK_ROM_BUSY;  // fast path, no lock needed
    Card_LockRom((uint16_t)uplink_lock_id);
    if (uplink_card_busy()) {
        Card_UnlockRom((uint16_t)uplink_lock_id);
        return UPLINK_ROM_BUSY;
    }
    int result;
    if (length > 0) {
        if (length > 512) length = 512;
        REG_DMA3SAD = (uint32_t)buffer;
        REG_DMA3DAD = (uint32_t)&REG_MCD1;
        REG_DMA3CNT = UPLINK_DMA3_FEED_CNT;  // start the MCD1 feed (ARM9)
        if (uplink_rom_start(DSPICO_CMD_USB_WRITE_DATA(0, endpoint, 1, length), UPLINK_MCCNT1_WRITE) != 0) {
            REG_DMA3CNT = 0;
            Card_UnlockRom((uint16_t)uplink_lock_id);
            return UPLINK_ROM_BUSY;
        }
        result = uplink_rom_wait_busy() ? UPLINK_ROM_OK : UPLINK_ROM_TIMEOUT;
        REG_DMA3CNT = 0;  // stop the feed (always, even on timeout)
    } else {
        if (uplink_rom_start(DSPICO_CMD_USB_WRITE_DATA(0, endpoint, 1, 0), UPLINK_MCCNT1_CMD) != 0) {
            Card_UnlockRom((uint16_t)uplink_lock_id);
            return UPLINK_ROM_BUSY;
        }
        result = uplink_rom_wait_busy() ? UPLINK_ROM_OK : UPLINK_ROM_TIMEOUT;
    }
    if (result == UPLINK_ROM_TIMEOUT) uplink_stuck_arm(UPLINK_OP_WRITE);
    Card_UnlockRom((uint16_t)uplink_lock_id);
    return result;
}

// ===========================================================================
// USB descriptors (minimal CDC-ACM: control iface + data iface + endpoints)
// ===========================================================================
static const uint8_t uplink_desc_device[18] = {
    0x12,               // bLength
    0x01,               // bDescriptorType = DEVICE
    0x00, 0x02,         // bcdUSB = 2.00
    0x02,               // bDeviceClass = Communications (CDC)
    0x02,               // bDeviceSubClass = CDC
    0x01,               // bDeviceProtocol = ACM
    0x40,               // bMaxPacketSize0 = 64
    0xFE, 0xCA,         // idVendor = 0xCAFE
    0x01, 0x40,         // idProduct = 0x4001 (CDC)
    0x00, 0x01,         // bcdDevice = 1.00
    0x01,               // iManufacturer
    0x02,               // iProduct
    0x00,               // iSerialNumber
    0x01,               // bNumConfigurations
};

// Total = 9 + (9+5+4+5+7) + (9+7+7) = 62 bytes.
static const uint8_t uplink_desc_config[62] = {
    // Configuration
    0x09, 0x02, 0x3E, 0x00,  // bLength, bDescriptorType=CONFIG, wTotalLength=62
    0x02,                     // bNumInterfaces
    0x01,                     // bConfigurationValue
    0x00,                     // iConfiguration
    0x00,                     // bmAttributes (bus powered)
    0x32,                     // bMaxPower = 50 mA
    // Interface 0: CDC Communications (control)
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    //   bLength, bDescriptorType=INTERFACE, ifNum=0, alt=0, endpoints=1,
    //   class=0x02 (Comm), subclass=0x02 (ACM), protocol=0x01 (ACM), iInterface=0
    // CDC Header functional descriptor
    0x05, 0x24, 0x00, 0x10, 0x01,  // bcdCDC 1.10
    // CDC ACM functional descriptor
    0x04, 0x24, 0x02, 0x02,        // bmCapabilities = line coding only
    // CDC Union functional descriptor
    0x05, 0x24, 0x06, 0x00, 0x01,  // master=0, slave0=1
    // Notification endpoint (interrupt IN)
    0x07, 0x05, 0x82, 0x03, 0x10, 0x00, 0x0A,
    //   endpoint 0x82, interrupt, maxPacket 16, interval 10 ms
    // Interface 1: CDC Communications Data
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    //   ifNum=1, alt=0, endpoints=2, class=0x0A (CDC Data), subclass/protocol=0
    // Bulk IN endpoint (the data stream)
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,  // 0x81 bulk, 64
    // Bulk OUT endpoint (required by CDC-ACM; unused)
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,  // 0x01 bulk, 64
};

static const uint8_t uplink_str_langid[4] = { 0x04, 0x03, 0x09, 0x04 };

static uint8_t uplink_make_string(const char* s, uint8_t* out) {
    uint8_t len = (uint8_t)strlen(s);
    out[0] = (uint8_t)(2 + (int)len * 2);
    out[1] = 0x03;
    for (uint8_t i = 0; i < len; i++) {
        out[2 + i * 2] = (uint8_t)s[i];
        out[3 + i * 2] = 0;
    }
    return out[0];
}

// ===========================================================================
// Control-transfer handling
// ===========================================================================
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} uplink_setup_t;

static bool uplink_addressed = false;
static bool uplink_configured = false;
static bool uplink_pending_finish_address = false;
static bool uplink_pending_config = false;
static uint8_t uplink_pending_address = 0;
static uint8_t uplink_config_value = 0;
// ENUM diagnostics (HUD): the last SETUP request assembled, in wire order
// (bRequest/bmRequestType/wValue little-endian; 0 = none yet), and the
// number of EP0 XFER_COMPLETE events seen since the last (re)connect. If
// enumeration stalls, the hex shows exactly which request the MOD saw
// (06800001 = GET_DESCRIPTOR(device), 06800002 = GET_DESCRIPTOR(config),
// 06810100 = GET_DESCRIPTOR(string 1), 59000100 = SET_ADDRESS(1),
// 29000100 = SET_CONFIGURATION(1), 80000000 = GET_STATUS(device)) and how
// far the transfer got; a garbage value means a misread (split) SETUP pair.
static uint32_t uplink_last_setup_req = 0;
// Raw first word of the last SETUP pair (bit 31 "last" stripped, bit 30 SETUP
// marker set): carries wLength/direction/wIndex. Shown on the HUD to tell a
// real, stable first word (60080000 = GET_DESCRIPTOR wLength=8 IN wIndex=0)
// from a garbage/misread one.
static uint32_t uplink_last_setup_word1 = 0;
static uint8_t uplink_ep0_xfers = 0;

static void uplink_rom_ctrl_in(const uint8_t* buffer, uint32_t len) {
    (void)uplink_rom_write_in(buffer, UPLINK_EP0_IN, len);
}

static void uplink_handle_setup(const uplink_setup_t* req) {
    uint8_t type = (req->bmRequestType >> 5) & 0x03;
    bool to_host = (req->bmRequestType & 0x80) != 0;
    (void)to_host;
    // Record the request in wire order for the HUD (see the
    // uplink_last_setup_req comment): bRequest, bmRequestType, then wValue
    // little-endian (06800001 = GET_DESCRIPTOR(device) etc.). A value that
    // does not match any known request means a misread (split) pair.
    uplink_last_setup_req = (uint32_t)(((uint32_t)req->bRequest << 24) |
                                       ((uint32_t)req->bmRequestType << 16) |
                                       ((uint32_t)(req->wValue >> 8) << 8) |
                                       (uint32_t)(req->wValue & 0xFF));

    // --- Standard requests ---
    if (type == 0) {
        switch (req->bRequest) {
        case UPLINK_REQ_GET_DESCRIPTOR: {
            uint8_t dtype = (uint8_t)((req->wValue >> 8) & 0xFF);
            uint8_t dindex = (uint8_t)(req->wValue & 0xFF);
            uint8_t strbuf[32];
            // The host may request a partial read (the first GET_DESCRIPTOR
            // asks for 8 bytes, the first configuration read for 9, the
            // first string read for 1); arm at most wLength bytes, like
            // the reference (TinyUSB arms min(wLength, total_len)).
            uint16_t wlen = req->wLength;
            if (dtype == UPLINK_DESC_DEVICE) {
                uint16_t len = (uint16_t)sizeof(uplink_desc_device);
                if (len > wlen) len = wlen;
                uplink_rom_ctrl_in(uplink_desc_device, len);
            } else if (dtype == UPLINK_DESC_CONFIGURATION) {
                uint16_t len = (uint16_t)sizeof(uplink_desc_config);
                if (len > wlen) len = wlen;
                uplink_rom_ctrl_in(uplink_desc_config, len);
            } else if (dtype == UPLINK_DESC_STRING) {
                if (dindex == 0) {
                    uint16_t len = (uint16_t)sizeof(uplink_str_langid);
                    if (len > wlen) len = wlen;
                    uplink_rom_ctrl_in(uplink_str_langid, len);
                } else if (dindex == 1) {
                    uint16_t n = uplink_make_string("DSpico", strbuf);
                    if (n > wlen) n = wlen;
                    uplink_rom_ctrl_in(strbuf, n);
                } else if (dindex == 2) {
                    uint16_t n = uplink_make_string("PMD:ES Uplink", strbuf);
                    if (n > wlen) n = wlen;
                    uplink_rom_ctrl_in(strbuf, n);
                } else {
                    uplink_rom_ctrl_in(NULL, 0);
                }
            } else {
                uplink_rom_ctrl_in(NULL, 0);
            }
            return;
        }
        case UPLINK_REQ_GET_STATUS: {
            uint8_t status[2] = { 0x00, 0x00 };
            uint16_t len = (req->wLength >= 2) ? 2 : req->wLength;
            uplink_rom_ctrl_in(status, len);
            return;
        }
        case UPLINK_REQ_GET_CONFIGURATION: {
            uint8_t cfg = uplink_configured ? uplink_config_value : 0;
            uint16_t len = (req->wLength >= 1) ? 1 : 0;
            uplink_rom_ctrl_in(len ? &cfg : NULL, len);
            return;
        }
        case UPLINK_REQ_GET_INTERFACE: {
            uint8_t alt = 0;
            uint16_t len = (req->wLength >= 1) ? 1 : 0;
            uplink_rom_ctrl_in(len ? &alt : NULL, len);
            return;
        }
        case UPLINK_REQ_SET_ADDRESS: {
            // Mirror the reference DCD: dcd_set_address() arms the MCU's
            // address-change state machine BEFORE the zero-length status
            // phase, and the switch is completed by FINISH_SET_ADDRESS
            // (sent when the resulting XFER_COMPLETE arrives). Without the
            // BEGIN command the MCU can stay on the old address, so every
            // SETUP the host sends to the new address is silently dropped
            // and enumeration stalls until the host gives up.
            uplink_rom_cmd(DSPICO_CMD_USB_BEGIN_SET_ADDRESS, UPLINK_OP_EVENT);
            uplink_pending_finish_address = true;
            uplink_pending_address = (uint8_t)(req->wValue & 0x7F);
            uplink_rom_ctrl_in(NULL, 0);
            return;
        }
        case UPLINK_REQ_SET_CONFIGURATION: {
            uplink_config_value = (uint8_t)(req->wValue & 0xFF);
            // Open the endpoints so the host can start using them. Only arm
            // the "configured" flag if every open succeeded: a BUSY open
            // (a DLDI read in flight) means the endpoint is not usable yet,
            // and a timeout armed the two-stage stuck recovery (grace
            // period first, cart reset only if still stuck). The host
            // re-sends SET_CONFIGURATION after a bus reset either way.
            int e1 = uplink_rom_cmd(DSPICO_CMD_USB_EP_OPEN(UPLINK_EP_DATA_IN,   64, UPLINK_XFER_BULK), UPLINK_OP_EVENT);
            int e2 = uplink_rom_cmd(DSPICO_CMD_USB_EP_OPEN(UPLINK_EP_DATA_OUT,  64, UPLINK_XFER_BULK), UPLINK_OP_EVENT);
            int e3 = uplink_rom_cmd(DSPICO_CMD_USB_EP_OPEN(UPLINK_EP_NOTIFY_IN, 16, UPLINK_XFER_INT), UPLINK_OP_EVENT);
            if (e1 == UPLINK_ROM_OK && e2 == UPLINK_ROM_OK && e3 == UPLINK_ROM_OK)
                uplink_pending_config = true;
            // Always acknowledge the request (zero-length status phase).
            uplink_rom_ctrl_in(NULL, 0);
            return;
        }
        case UPLINK_REQ_SET_INTERFACE:
            uplink_rom_ctrl_in(NULL, 0);
            return;
        default:
            // SET_IDLE and anything else: acknowledge with a zero-length status.
            uplink_rom_ctrl_in(NULL, 0);
            return;
        }
    }

    // --- CDC class / vendor requests: acknowledge (the reader is IN-only) ---
    uplink_rom_ctrl_in(NULL, 0);
}

// ===========================================================================
// Payload construction
// ===========================================================================
// DUNGEON_PTR (EU 0x2354138) points at the active struct dungeon. The field
// offsets below were confirmed against struct dungeon: +0x6 end_floor_flag,
// +0x748 id, +0x749 floor.
#define UPLINK_DUNGEON_PTR_ADDR 0x2354138u

// Payload lives in a static buffer (ARM9 RAM). It must be a full 512 bytes:
// the LEN_512 card transfer (and thus the MCD1 feed DMA) always pulls 512
// bytes from it, even though the DSpico firmware only uses the first
// UPLINK_PAYLOAD_SIZE of them.
static uint8_t uplink_payload_buf[512];

static bool uplink_build_payload(uint8_t* out) {
    uplink_payload_t* p = (uplink_payload_t*)out;
    p->magic = UPLINK_MAGIC;
    p->version = UPLINK_VERSION;

    // Confirmed values.
    p->play_time_seconds = *(volatile uint32_t*)0x22ABFD4;  // PLAY_TIME_SECONDS
    p->play_time_frames  = *(volatile uint8_t*)0x22ABFD8;   // PLAY_TIME_FRAME_COUNTER
    p->main_menu_magic   = *(volatile uint32_t*)0x22A3670;  // 0x22A3E94 => main menu

    // Autosplitter trigger values (identified during reverse engineering; the
    // exact size/meaning of each should be confirmed against the split config).
    p->v_22ABAA8 = *(volatile uint8_t*)0x22ABAA8;
    p->v_22ABAA9 = *(volatile uint8_t*)0x22ABAA9;
    p->v_22ABADB = *(volatile uint8_t*)0x22ABADB;
    p->v_2325ACA = *(volatile uint8_t*)0x2325ACA;
    p->v_22A40E4 = *(volatile uint32_t*)0x22A40E4;
    p->v_2329D40 = *(volatile uint32_t*)0x2329D40;

    // Dungeon fields (only valid while a dungeon is active).
    p->flags = 0;
    p->dungeon_end_floor_flag = 0;
    p->dungeon_id = 0;
    p->dungeon_floor = 0;
    uint32_t dg = *(volatile uint32_t*)UPLINK_DUNGEON_PTR_ADDR;
    // Only trust the pointer if it points into readable main ARM9 RAM. During
    // scene transitions the pointer can hold garbage and dereferencing it
    // would raise a data bus error and crash the game.
    if (dg >= 0x02000000u && dg + 0x800u <= 0x02400000u) {
        const volatile uint8_t* dgb = (const volatile uint8_t*)dg;
        p->dungeon_end_floor_flag = dgb[0x6];
        p->dungeon_id             = dgb[0x748];
        p->dungeon_floor          = dgb[0x749];
        p->flags |= 0x1;  // in dungeon mode
    }

    p->seq++;

    p->crc16 = 0;
    p->crc16 = uplink_crc16(out, UPLINK_PAYLOAD_SIZE);
    // The LEN_512 card transfer always moves a full 512 bytes from this
    // buffer; zero the tail so the bytes the firmware ignores are stable.
    memset(out + UPLINK_PAYLOAD_SIZE, 0, sizeof(uplink_payload_buf) - UPLINK_PAYLOAD_SIZE);
    return true;
}

// ===========================================================================
// State machine + public API
// ===========================================================================
static bool     uplink_initialized = false;
static bool     uplink_stream_ready = false;
static uplink_state_t uplink_state = UP_STATE_UNINIT;
static uint16_t uplink_seq = 0;
static uint32_t uplink_frame_counter = 0;
// Ticks to wait after a card transfer times out before touching the card
// again (a wedged card would otherwise cost ~48 ms of spin per frame).
#define UPLINK_CARD_COOLDOWN_TICKS 120u
static uint32_t uplink_card_cooldown = 0;
// Consecutive ticks the card is busy with a transfer that is not one of
// ours (no in-flight mailbox read, no stuck grace pending). A DLDI read
// lasts a few ms; beyond UPLINK_FOREIGN_BUSY_LIMIT something is holding
// the card (typically the DSpico finalizing a read we aborted, or
// servicing USB a long time). Shown as "BUSY f" in the HUD.
#define UPLINK_FOREIGN_BUSY_LIMIT 4u
static uint32_t uplink_foreign_busy_ticks = 0;
// Two-stage stuck recovery state: a timeout arms a grace period during
// which UplinkTick() polls for the (late) completion; only a transfer that
// is STILL stuck when the grace expires triggers a cart reset, and only up
// to UPLINK_MAX_CARD_RESETS resets per session. A cart reset while the
// DSpico's USB is active is the documented DSpico firmware bug trigger, so
// the goal is that this path almost never runs.
#define UPLINK_STUCK_GRACE_TICKS 30u  // ~0.5 s: slow DSpicos get this long
#define UPLINK_MAX_CARD_RESETS  2u    // after this many, declare the card dead
static uint32_t uplink_stuck_grace = 0;  // ticks left in the grace period
static uint32_t uplink_stuck_op = 0;     // op tag of the stuck transfer
static uint32_t uplink_reset_count = 0;  // cart resets done this session

// A SETUP pair split across ticks: the first word (wLength/direction/
// wIndex) was already dequeued, the second (wValue/bRequest/bmRequestType)
// is still queued -- or its read still in flight -- because the drain hit
// BUSY/INFLIGHT between the two reads. The next drain iteration consumes
// that word as the second half of the pair instead of as a standalone
// event (it carries no 0x40000000 marker and would otherwise be
// misread). Cleared whenever the DSpico's queue is cleared or the uplink
// is toggled.
static bool     uplink_setup_awaiting_second = false;
static uint16_t uplink_setup_wLength = 0;
static uint8_t  uplink_setup_direction = 0;
static uint16_t uplink_setup_wIndex = 0;
// A split pair whose second word cannot be read is retried this many times
// (the word is guaranteed to be queued -- the pair is enqueued atomically
// -- so only the read of it is slow or lost); after that the (now
// misaligned) queue is dropped and the host's own retransmission delivers
// a fresh pair. uplink_pair_drops counts the give-ups (HUD "dN").
#define UPLINK_PAIR_MAX_RETRIES 4u
static uint32_t uplink_pair_retry = 0;
static uint32_t uplink_pair_drops = 0;

// A SETUP pair's second word is unusable: its read never delivered data,
// or the queue is misaligned. Clear the queue, count the drop, and back
// off -- the host retransmits the SETUP after its own timeout, so the
// next pair arrives fresh and aligned. (After a no-data readout it is
// unclear whether the word is still queued, so clearing is the only safe
// way to re-align the drain.)
static void uplink_setup_pair_drop(void) {
    uplink_setup_awaiting_second = false;
    uplink_pair_retry = 0;
    uplink_pair_drops++;
    if (uplink_rom_cmd(DSPICO_CMD_USB_CLEAR_EVENT_QUEUE, UPLINK_OP_EVENT) == UPLINK_ROM_OK)
        uplink_card_irq_ack();
    uplink_card_cooldown = UPLINK_CARD_COOLDOWN_TICKS / 4;
}

// Aborts a stuck card ROM transfer: stop the MCD1 feed, toggle the
// cartridge RST line (the only way to abort a card ROM transfer), then wait
// for the interface to go idle. The caller must hold the card lock. Returns
// false if the card is still busy after the reset.
static bool uplink_rom_recover_card(void) {
    REG_DMA3CNT = 0;  // stop the MCD1 feed
    // Assert the cart reset. The read-modify-write must drop the ENABLE bit:
    // writing MCCNT1 with it set would restart the transfer.
    REG_MCCNT1 = (REG_MCCNT1 & (uint32_t)~(MCCNT1_RESET_OFF | MCCNT1_ENABLE));
    for (volatile uint32_t i = 0; i < 10000u; i++);  // hold ~0.3 ms
    REG_MCCNT1 |= MCCNT1_RESET_OFF;  // release
    uint32_t t = 600000u;  // ~18 ms at 33 MHz
    while (REG_MCCNT1 & MCCNT1_ENABLE) {
        if (t-- == 0u) return false;
    }
    return true;
}

// A transfer timed out with MCCNT1_ENABLE still set. Do NOT reset the cart
// right away: the DSpico is often merely slow (busy servicing USB), and a
// cart reset while its USB state machine is active triggers a documented
// DSpico firmware bug -- the USB link then stays dead until the DSpico is
// power cycled, and a half-wedged RP2040 can serve garbage on ROM reads
// (which is what corrupts the game's DLDI loads). Instead, record the
// stuck operation and give the DSpico UPLINK_STUCK_GRACE_TICKS to finish
// the transfer; uplink_stuck_poll() (run from UplinkTick) does the
// follow-up. Safe to call without holding the card lock (it only sets
// flags).
static void uplink_stuck_arm(uint32_t op_tag) {
    if (uplink_stuck_grace) return;  // one stuck transfer at a time
    uplink_stuck_op = op_tag;
    uplink_stuck_grace = UPLINK_STUCK_GRACE_TICKS;
}

// Runs once per tick from UplinkTick while a stuck transfer is pending.
// If the transfer finished late (a slow DSpico), just drop it and resume.
// If it is STILL stuck after the grace period, reset the cart -- the only
// way to abort a card ROM transfer, and the only way to unstick the
// game's DLDI -- but only up to UPLINK_MAX_CARD_RESETS times per session;
// after that the card is declared dead and all card traffic stops (the
// DSpico needs a power cycle).
static void uplink_stuck_poll(void) {
    if (!uplink_stuck_grace) return;

    if (!(REG_MCCNT1 & MCCNT1_ENABLE)) {
        // Finished late: the DSpico was just slow. Drop the (unread)
        // result and resume normal operation.
        uplink_stuck_grace = 0;
        uplink_stuck_op = 0;
        return;
    }

    if (uplink_stuck_grace > 1) {
        uplink_stuck_grace--;
        return;
    }

    // Still stuck after the grace period: reset the cart.
    uplink_stuck_grace = 0;
    uplink_stuck_op = 0;

    if (uplink_reset_count >= UPLINK_MAX_CARD_RESETS) {
        // The DSpico survived two resets and is still wedged: it needs a
        // power cycle (see the DSpico README). Stop touching the card;
        // every primitive now returns BUSY until the uplink is re-enabled.
        uplink_card_dead = true;
        uplink_state = UP_STATE_FAIL;
        return;
    }

    // The cart reset clears the DSpico's event queue and deasserts the IRQ
    // line; drop any stale latched flag so we do not immediately poll an
    // empty queue after re-init. The reset also re-boots the DSpico's USB
    // state machine, so the whole init sequence runs again afterwards.
    uplink_ensure_lock();
    Card_LockRom((uint16_t)uplink_lock_id);
    uplink_card_irq_ack();
    bool unstuck = uplink_rom_recover_card();
    Card_UnlockRom((uint16_t)uplink_lock_id);
    uplink_reset_count++;
    // The reset killed any in-flight mailbox read and cleared the
    // DSpico's event queue: drop the associated state.
    uplink_event_inflight = false;
    uplink_event_word_got = false;
    uplink_event_settle_age = 0;
    uplink_event_settle_only = false;
    uplink_event_misses = 0;
    uplink_setup_awaiting_second = false;
    uplink_last_setup_req = 0;
    uplink_pair_retry = 0;
    uplink_ep0_xfers = 0;

    if (!unstuck) {
        uplink_card_dead = true;
        uplink_state = UP_STATE_FAIL;
        return;
    }
    uplink_initialized = false;
    uplink_configured = false;
    uplink_addressed = false;
    uplink_stream_ready = false;
    uplink_pending_finish_address = false;
    uplink_pending_config = false;
    uplink_state = UP_STATE_UNINIT;
    // Give the DSpico time to come back after the reset before the first
    // re-init command.
    uplink_card_cooldown = UPLINK_CARD_COOLDOWN_TICKS;
}

void UplinkInit(void) {
    if (uplink_initialized) return;
    uplink_ensure_lock();
    // Bring the DSpico up: clear stale events, init the USB controller,
    // open the control endpoints, enable its interrupts, drop SOF
    // notifications (we poll, and SOF would flood the queue), then pull
    // the USB bus up.
    //
    // INTERRUPT_ENABLE is the critical step: the DSpico's firmware enables
    // the RP2040 USB controller IRQs only on that command. Without them the
    // pull-up still goes up (the host sees a device attach) but no USB
    // tokens are processed -- SETUP packets are never captured or enqueued
    // and the host's enumeration times out with an "unrecognized device".
    // The reference DCD sends it via dcd_int_enable() for this reason.
    //
    // The sequence is all-or-nothing: if the card is busy (e.g. a DLDI read
    // in flight) a command returns BUSY and we retry the whole sequence on
    // a later tick, so the firmware never ends up partially initialized
    // (e.g. CONNECTed without INIT). If any command timed out,
    // uplink_rom_cmd() armed the two-stage stuck recovery (grace period
    // first, cart reset only if still stuck); back off for a while before
    // retrying.
    int clear = uplink_rom_cmd(DSPICO_CMD_USB_CLEAR_EVENT_QUEUE, UPLINK_OP_INIT_BASE + 0);
    int init = uplink_rom_cmd(DSPICO_CMD_USB_INIT, UPLINK_OP_INIT_BASE + 1);
    // TinyUSB usbd_init() calls usbd_reset() right after dcd_init(), which
    // calls dcd_set_address(0) -> a single BEGIN_SET_ADDRESS. Mirror it so
    // the firmware pending address is 0 before the first address-0 SETUP can
    // arrive (a no-op on a fresh boot, but it keeps the command stream
    // identical to the reference one).
    int setaddr = uplink_rom_cmd(DSPICO_CMD_USB_BEGIN_SET_ADDRESS, UPLINK_OP_INIT_BASE + 2);
    // Open the control endpoints, exactly like the reference: TinyUSB
    // usbd_init() calls dcd_edpt_open() for EP0 IN and EP0 OUT, which the
    // DCD turns into EP_OPEN commands. EP0 IN works without this (a
    // WRITE_DATA has to arm the SIE transfer), but the zero-length status
    // arm for the EP0 OUT phase (BEGIN_TRANSFER, 0, 0) only takes effect
    // if the firmware EP0 OUT endpoint is open. Without it the status
    // OUT is NAKed forever, the host times out the control transfer and
    // resets the bus -- the "device not recognized" loop.
    int ep0in = uplink_rom_cmd(DSPICO_CMD_USB_EP_OPEN(UPLINK_EP0_IN, 64, UPLINK_XFER_CONTROL), UPLINK_OP_INIT_BASE + 3);
    int ep0out = uplink_rom_cmd(DSPICO_CMD_USB_EP_OPEN(UPLINK_EP0, 64, UPLINK_XFER_CONTROL), UPLINK_OP_INIT_BASE + 4);
    int interrupt = uplink_rom_cmd(DSPICO_CMD_USB_INTERRUPT_ENABLE, UPLINK_OP_INIT_BASE + 5);
    int sof = uplink_rom_cmd(DSPICO_CMD_USB_SOF_DISABLE, UPLINK_OP_INIT_BASE + 6);
    int connect = uplink_rom_cmd(DSPICO_CMD_USB_CONNECT, UPLINK_OP_INIT_BASE + 7);
    if (clear < 0 || init < 0 || setaddr < 0 || ep0in < 0 || ep0out < 0 || interrupt < 0 || sof < 0 || connect < 0) {
        uplink_card_cooldown = UPLINK_CARD_COOLDOWN_TICKS / 2;
    } else if (clear > 0 && init > 0 && setaddr > 0 && ep0in > 0 && ep0out > 0 && interrupt > 0 && sof > 0 && connect > 0) {
        uplink_initialized = true;
        uplink_state = UP_STATE_CONNECT;
        // CLEAR_EVENT_QUEUE emptied the queue and deasserted the IRQ line;
        // drop any stale latched flag.
        uplink_card_irq_ack();
    }
    // A BUSY (0) result falls through with nothing recorded as
    // initialized; UplinkTick() retries the full sequence on the next tick.
}

void UplinkDeinit(void) {
    if (!uplink_initialized) return;
    // Mirror the reference DCD teardown: close all endpoints, drop the
    // pull-up, disable the USB controller IRQs, then deinit. (The
    // firmware's own ntrc_resetUsb() does the same on cart reset.)
    // A timed-out teardown command arms the same two-stage stuck recovery
    // (grace first, cart reset only if still stuck, capped at
    // UPLINK_MAX_CARD_RESETS): that reset is safer than the alternative
    // (a stuck ENABLE bit, which would hang the game's DLDI on the next
    // ROM read as soon as the uplink is off).
    uplink_rom_cmd(DSPICO_CMD_USB_EP_CLOSE_ALL, UPLINK_OP_DEINIT);
    uplink_rom_cmd(DSPICO_CMD_USB_DISCONNECT, UPLINK_OP_DEINIT);
    uplink_rom_cmd(DSPICO_CMD_USB_INTERRUPT_DISABLE, UPLINK_OP_DEINIT);
    uplink_rom_cmd(DSPICO_CMD_USB_DEINIT, UPLINK_OP_DEINIT);
    uplink_initialized = false;
    uplink_configured = false;
    uplink_addressed = false;
    uplink_stream_ready = false;
    uplink_pending_finish_address = false;
    uplink_pending_config = false;
    uplink_state = UP_STATE_UNINIT;
}

// ---------------------------------------------------------------------------
// Enable / disable + main-menu hotkey
// ---------------------------------------------------------------------------
// The uplink is opt-in and OFF by default. While disabled, UplinkTick() does
// not touch the card ROM interface or the card locks at all, so the ROM is
// safe on emulators (no card) and on flashcarts without DSpico firmware.
// Enabling is only possible on the main menu, by which point the game's card
// subsystem is fully operational and the boot-time DLDI traffic is over. The
// setting is per-session: a soft reset or reboot turns the uplink off again.
static bool uplink_enabled = false;
static bool uplink_toggle_held = false;

void UplinkSetEnabled(bool enabled) {
    if (uplink_enabled == enabled) return;
    bool was_dead = uplink_card_dead;
    uplink_enabled = enabled;
    uplink_card_dead = false;  // a manual toggle is an explicit retry
    uplink_reset_count = 0;    // fresh reset budget for the new session
    uplink_stuck_grace = 0;    // no pending stuck transfer
    uplink_stuck_op = 0;
    // Drop all mailbox/pair state: the queues are cleared on disable and
    // rebuilt from scratch on enable.
    uplink_event_inflight = false;
    uplink_event_word_got = false;
    uplink_event_settle_age = 0;
    uplink_event_settle_only = false;
    uplink_event_misses = 0;
    uplink_setup_awaiting_second = false;
    uplink_last_setup_req = 0;
    uplink_ep0_xfers = 0;
    uplink_pair_retry = 0;
    uplink_pair_drops = 0;
    uplink_foreign_busy_ticks = 0;
    // Reset the idle probe for the new session: the old verdict would
    // describe the previous DSpico state, not this one.
    uplink_probe_ticks = 0;
    uplink_probe_verdict = UPLINK_PROBE_VERDICT_NA;
    uplink_last_delivered = 0;
    // Drop any stale IRQ flag from the previous session state: disabling
    // clears the DSpico's queue (DEINIT), and enabling starts from scratch.
    uplink_card_irq_ack();
    if (!enabled) {
        // Settle any transfer left in flight FIRST, so the deinit commands
        // below actually run (uplink_rom_cmd no-ops while the card is
        // busy):
        //   LEN_4  -- our pending mailbox read. NOT a stuck card (an
        //              empty-queue read is left pending by design): just
        //              drop the stale ENABLE bit; a cart reset here would
        //              hit the documented DSpico USB bug for no reason.
        //   LEN_0  -- a stuck command transfer (the DLDI never uses
        //              LEN_0): a stuck ENABLE would hang the DLDI on the
        //              next ROM read, so reset the cart.
        //   LEN_512 -- a DLDI read (or a write that the stuck recovery
        //              has already handled): it completes on its own; a
        //              cart reset mid-read would corrupt it.
        if (uplink_card_busy()) {
            uplink_ensure_lock();
            Card_LockRom((uint16_t)uplink_lock_id);
            uint32_t lenfield = REG_MCCNT1 & 0x7000000u;
            if (lenfield == MCCNT1_LEN_4)
                uplink_event_abort();
            else if (was_dead || lenfield == MCCNT1_LEN_0)
                uplink_rom_recover_card();
            Card_UnlockRom((uint16_t)uplink_lock_id);
        }
        // Settle the firmware's mailbox state: an aborted GET_EVENT read
        // leaves the firmware's card state machine waiting to deliver an
        // event; a CLEAR_EVENT_QUEUE resets the queue (and the pending
        // state) so the deinit commands are answered promptly and the
        // game's next ROM read gets clean data -- a deinit left unanswered
        // while the mailbox was unsettled is what corrupted the game's
        // DLDI loads on toggle-off.
        uplink_rom_cmd(DSPICO_CMD_USB_CLEAR_EVENT_QUEUE, UPLINK_OP_DEINIT);
        // Cleanly drop the USB connection (EP close + disconnect + deinit).
        UplinkDeinit();
        // The DSpico no longer drives the card IRQ line, so hand the
        // firmware's card IRQ dispatch back to the game.
        uplink_card_irq_mask(false);
    } else {
        // Mask the firmware's card IRQ before the DSpico starts asserting the
        // line for USB events; otherwise the firmware mis-reads it as a card
        // pull-out and shows the "game card was removed" screen.
        uplink_card_irq_mask(true);
    }
    // When enabling, UplinkTick() runs UplinkInit() on the next tick; the
    // state is already clean (UplinkDeinit reset it, or init never ran).
}

bool UplinkIsEnabled(void) {
    return uplink_enabled;
}

// True while the uplink is in the ENUM state (the PC is enumerating the
// device). Used to temporarily suppress the HUD timer so the long
// "USB:ENUM ..." diagnostic line is readable (see UPLINK_HIDE_TIMER_WHILE_ENUM
// in timer.c). Only safe to call from the main routine thread.
bool UplinkIsEnumerating(void) {
    return uplink_enabled && uplink_initialized && uplink_state == UP_STATE_ENUM;
}

// In the main menu, Start + Up toggles the uplink. Same pattern as
// HandleSpeedToggle() (optimizations.c): main-menu only, edge-triggered.
void HandleUplinkToggle(void) {
    if (!OverlayIsLoaded(OGROUP_OVERLAY_1)) {
        return;
    }

    struct held_buttons held_buttons;
    GetHeldButtons(0, (void*)&held_buttons);

    if (held_buttons.start && held_buttons.up) {
        if (uplink_toggle_held) return;
        uplink_toggle_held = true;
        UplinkSetEnabled(!uplink_enabled);
        return;
    }

    uplink_toggle_held = false;
}

// DSpico event word shapes (usbEventQueue.h in the firmware):
// 0 = NONE, 1 = BUS_RESET, 2 = UNPLUGGED, 3 = SUSPEND, 4 = RESUME,
// 0x20000000|frame = SOF, 0x30000000|... = XFER_COMPLETE,
// 0x40000000|... = SETUP_RECEIVED (bit 31, already stripped, is the
// "last event in the queue" flag). Anything else is not an event -- e.g. an
// orphaned SETUP second word left behind after a lost drain.
static bool uplink_event_valid(uint32_t raw) {
    if (uplink_word_is_allones(raw)) return false;  // no data was delivered
    if (raw >= 1 && raw <= 4) return true;  // BUS_RESET / UNPLUGGED / SUSPEND / RESUME
    if ((raw >> 28) == 2) return true;      // SOF
    if ((raw >> 28) == 3) return true;      // XFER_COMPLETE
    if ((raw >> 30) == 1) return true;      // SETUP_RECEIVED
    return false;
}

void UplinkTick(void) {
    if (!uplink_enabled) return;

    // While the uplink is enabled, the card-pull-out handler and the
    // polling/token pull-out check are suppressed by the trampolines in
    // patches/patch.asm; clear the flag the handler would set so the engine's
    // "game card was removed" state machine can never observe it. Only the
    // flag word is cleared -- the following word holds the registered
    // pull-out callback pointer and must be left untouched. (Kept running
    // even while the card is dead: the trampoline suppression stays active
    // until the uplink is toggled off.)
    *(volatile uint32_t*)0x022BC980 = 0;

    // Two-stage stuck recovery: follow up on any armed timeout (finished
    // late -> resume; still stuck after the grace -> bounded cart reset, or
    // declare the card dead once the reset budget is spent).
    uplink_stuck_poll();

    if (uplink_card_dead) {
        uplink_state = UP_STATE_FAIL;
        return;
    }

    // Foreign busy (see uplink_foreign_busy_ticks): the card is held by a
    // transfer that is not one of ours (no in-flight mailbox read, no stuck
    // grace pending). Sampled once per tick; UplinkStatusString() shows
    // "BUSY f" when it stays high.
    if (uplink_card_busy() && !uplink_event_inflight && !uplink_stuck_grace) {
        if (uplink_foreign_busy_ticks < 0xFFFFu) uplink_foreign_busy_ticks++;
    } else {
        uplink_foreign_busy_ticks = 0;
    }

    // A mailbox (GET_EVENT) read left in flight by uplink_rom_get_event():
    // poll it without blocking. The card controller keeps the transfer
    // running in hardware between ticks; we only observe DATA_READY /
    // ENABLE here. (It cannot overlap with a stuck grace: commands and
    // writes cannot start while the card is busy.)
    if (uplink_event_inflight) {
        if (uplink_event_settle_only) {
            // A word was already delivered to the caller; only the hardware
            // settle is pending (the DSpico clearing ENABLE after it
            // finishes the read). Wait for it to happen on its own; abort
            // only if it does not clear the bit within
            // UPLINK_EVENT_SETTLE_MAX_TICKS. No word is re-read: MCD1
            // already holds the (consumed) word.
            if (!(REG_MCCNT1 & MCCNT1_ENABLE)) {
                uplink_event_inflight = false;
                uplink_event_settle_only = false;
                uplink_event_settle_age = 0;
            } else if (++uplink_event_settle_age >= UPLINK_EVENT_SETTLE_MAX_TICKS) {
                uplink_event_settle_age = 0;
                Card_LockRom((uint16_t)uplink_lock_id);
                uplink_event_abort();
                Card_UnlockRom((uint16_t)uplink_lock_id);
                uplink_event_inflight = false;
                uplink_event_settle_only = false;
            }
            return;
        }
        if (REG_MCCNT1 & MCCNT1_DATA_READY) {
            uplink_event_word = REG_MCD1;
            uplink_event_word_got = true;
        }
        if (uplink_event_word_got || !(REG_MCCNT1 & MCCNT1_ENABLE)) {
            // The word landed (or the read completed): settle the transfer.
            if (!uplink_event_word_got) {
                if (uplink_xfer_is_event_read()) {
                    // Our LEN_4 read completed WITHOUT a DATA_READY latch: no
                    // word was delivered, so MCD1 holds the previous (stale)
                    // word, not an event (the reference only captures MCD1 on
                    // a DATA_READY latch). Arm the all-ones "no data" sentinel
                    // so the drain treats it like a busy answer and re-reads on
                    // the next tick, giving the DSpico another chance to
                    // deliver the real word.
                    uplink_event_word = 0x7FFFFFFFu;
                } else {
                    // A DLDI read overwrote (and finished) our mailbox read
                    // in between ticks: MCD1 holds DLDI data, not an event.
                    // Drop the state; the latched flag drives a fresh drain,
                    // and the next GET_EVENT picks up the next event.
                    uplink_event_inflight = false;
                    return;
                }
            }
            if (REG_MCCNT1 & MCCNT1_ENABLE) {
                // The word is captured but the DSpico has not cleared
                // ENABLE yet: it is still finalizing the read. Let it close
                // out on its own -- dropping ENABLE mid-finalization can
                // leave the firmware's card state machine confused (which
                // is what hangs the follow-up read of a split SETUP pair).
                // Abort only if it does not clear the bit itself.
                if (++uplink_event_settle_age >= UPLINK_EVENT_SETTLE_MAX_TICKS) {
                    uplink_event_settle_age = 0;
                    Card_LockRom((uint16_t)uplink_lock_id);
                    uplink_event_abort();
                    Card_UnlockRom((uint16_t)uplink_lock_id);
                } else {
                    return;  // still settling; the drain resumes next tick
                }
            }
            uplink_event_word_got = true;
            uplink_event_settle_age = 0;
            uplink_event_inflight = false;
            uplink_event_misses = 0;
            // The drain below resumes on the next tick (the IRQ flag is
            // still latched) and consumes the captured word first.
        } else if (++uplink_event_age >= UPLINK_EVENT_MAX_AGE_TICKS) {
            if (uplink_setup_awaiting_second) {
                // A SETUP pair is split: the first word was dequeued, the
                // second is GUARANTEED to be queued (the pair is enqueued
                // atomically) -- only its read is slow or lost. Never drop
                // the pair state here: the next drain would then consume
                // whatever event comes next as the missing second half and
                // assemble a garbage request (the host's retry loop feeds
                // that forever). Re-issue the read; after a bounded number
                // of tries, drop the (now misaligned) queue -- the host
                // retransmits the SETUP after its own timeout and the next
                // pair arrives fresh and aligned.
                Card_LockRom((uint16_t)uplink_lock_id);
                uplink_event_abort();
                Card_UnlockRom((uint16_t)uplink_lock_id);
                uplink_event_inflight = false;
                uplink_event_word_got = false;
                uplink_event_settle_age = 0;
                if (++uplink_pair_retry < UPLINK_PAIR_MAX_RETRIES) {
                    // Re-issue the second-word read right away (the word is
                    // queued, so this normally completes inside the inline
                    // window).
                    uint32_t word = 0;
                    int rr = uplink_rom_get_event(&word);
                    if (rr == UPLINK_ROM_OK) {
                        // The word completed inside the inline window: arm
                        // it for the drain, which consumes it as the second
                        // half of the split pair (uplink_event_next returns
                        // the armed word before any fresh read).
                        uplink_event_word = word;
                        uplink_event_word_got = true;
                    }
                    // INFLIGHT: polled again from here (and re-retried on
                    // the next timeout). BUSY: a DLDI read won the race;
                    // the probe/drain retries next tick.
                } else {
                    uplink_setup_pair_drop();
                }
                return;  // one card operation per frame
            }
            // The mailbox read never completed: the DSpico had an empty
            // queue (a read on an empty queue is left pending until the
            // next event) or is too busy servicing USB to answer it. A
            // normal DSpico state, NOT a wedged card -- abort the transfer
            // (the DLDI's own error stop) and back off. No stuck recovery,
            // no cart reset: the latched IRQ flag keeps the drain going,
            // and a host that was mid-enumeration simply retries.
            Card_LockRom((uint16_t)uplink_lock_id);
            uplink_event_abort();
            Card_UnlockRom((uint16_t)uplink_lock_id);
            uplink_event_inflight = false;
            uplink_event_word_got = false;
            uplink_event_settle_age = 0;
            uplink_card_cooldown = UPLINK_CARD_COOLDOWN_TICKS;
            if (++uplink_event_misses >= UPLINK_EVENT_STALE_MISSES) {
                // Nothing has arrived for several attempts: the DSpico has
                // deasserted its IRQ line and the latched flag is stale.
                // Ack it; a real event re-latches it on the next edge.
                uplink_event_misses = 0;
                uplink_card_irq_ack();
            }
        }
        return;  // one card operation per frame
    }

    // While a stuck transfer is pending the card is busy by definition;
    // nothing else may touch it until the grace resolves.
    if (uplink_stuck_grace) return;

    if (!uplink_initialized) {
        if (uplink_card_cooldown > 0) uplink_card_cooldown--;
        else UplinkInit();
        return;
    }

    // Back off after a card timeout before touching the card again.
    if (uplink_card_cooldown > 0) {
        uplink_card_cooldown--;
        return;
    }

    // Drain the DSpico event queue -- but only when it is not empty. The
    // DSpico asserts the cartridge IRQ line (REG_IF bit 20) while events are
    // queued, and a GET_EVENT issued on an empty queue leaves the card
    // transfer pending until the next event; the game's DLDI waits for the
    // card to be idle without a timeout, so a lingering GET_EVENT would stall
    // ROM reads (e.g. while loading a new game). Gating the drain on the IRQ
    // line means we never touch the card while idle, mirroring the reference
    // IRQ-driven DCD.
    // Idle probe: on some DSpico firmware / card states the IRQ line never
    // reaches the NDS, so the latched flag above is never set even though
    // the firmware queues events (e.g. the line was already high when the
    // NDS started watching -- no rising edge, no latch). Periodically issue
    // one GET_EVENT read (short inline window; a wordless pending read is
    // aborted immediately -- a probe read is never left in flight wordless,
    // and a read that captured a word but whose ENABLE bit is still set is
    // handed to the poller above to settle): a captured event word is
    // handed to the drain below via uplink_event_word_got, and the verdict
    // is shown in the HUD status string (p0/p1/p2). A probe can only delay
    // a game ROM read by the inline window plus a settle cycle (a few ms
    // to ~150 ms), never stall it. (An in-flight read cannot be pending
    // here: the poller above returned early.)
    if (!uplink_card_irq_pending() && !uplink_event_word_got) {
        uint32_t interval =
            (uplink_state == UP_STATE_ENUM || uplink_state == UP_STATE_STREAM)
            ? UPLINK_PROBE_INTERVAL_FAST : UPLINK_PROBE_INTERVAL_SLOW;
        if (++uplink_probe_ticks >= interval) {
            uplink_probe_ticks = 0;
            int pr = uplink_rom_probe_event();
            if (pr == 1) {
                uplink_probe_verdict = 1;
            } else if (pr == 0) {
                uplink_probe_verdict = 0;
            } else if (pr == 2) {
                uplink_probe_verdict = 2;
            }
            // pr == -1: the card was busy; keep the previous verdict.
        }
    }

    // The gate also fires on uplink_event_word_got: a word captured by the
    // in-flight poller or by the probe must be drained even when the latched
    // flag is absent (the probe premise) or was already acked.
    if ((uplink_card_irq_pending() || uplink_event_word_got) && !uplink_event_inflight) {
        bool drained = false;
        int guard = 0;
        while (guard++ < 64) {
            uint32_t raw = 0;
            int r = uplink_event_next(&raw);
            if (r != UPLINK_ROM_OK) {
                // INFLIGHT: the mailbox read is pending; the in-flight
                // poller above takes over and the drain resumes next tick
                // (the flag stays latched). BUSY: a DLDI read grabbed the
                // card; the flag stays set, so the drain resumes next tick.
                break;
            }

            if (uplink_setup_awaiting_second) {
                // This word is the second half of a SETUP pair split across
                // ticks (the first word was already dequeued).
                uplink_setup_awaiting_second = false;
                bool last = (raw & (1u << 31)) != 0;
                raw &= ~(1u << 31);
                // A zero second word is only real for GET_STATUS (the only
                // standard request with an all-zero second half -- see the
                // inline path). Anything else -- no-data (all-ones) or
                // "queue empty" (USB_EVENT_NONE, the second entry not
                // enqueued yet) -- is a retry: one frame has already elapsed
                // by the time a word lands here, so consume the shared pair
                // retry budget (the poller timeout counts it too). A fresh
                // GET_EVENT on an empty queue stays pending until the second
                // entry arrives.
                bool getstatus_zero =
                    (raw == 0) && uplink_setup_direction &&
                    (uplink_setup_wLength == 2);
                if (uplink_word_is_allones(raw) || (raw == 0 && !getstatus_zero)) {
                    if (++uplink_pair_retry < UPLINK_PAIR_MAX_RETRIES) {
                        uplink_setup_awaiting_second = true;
                        break;
                    }
                    uplink_setup_pair_drop();
                    break;
                }
                // A reserved request type (bits 6:5 == 3) is never a real USB
                // request: a misread second word (not the pair's second half).
                // Drop the (misaligned) queue instead of acking it -- a
                // zero-length status for a request the host never sent would
                // abort enumeration; the host retransmits a fresh pair.
                if (((raw >> 29) & 3u) == 3u) {
                    uplink_setup_pair_drop();
                    break;
                }
                uplink_setup_t req;
                req.wValue = (uint16_t)(raw & 0xFFFF);
                req.bRequest = (uint8_t)((raw >> 16) & 0xFF);
                req.bmRequestType = (uint8_t)((raw >> 24) & 0x7F);
                if (uplink_setup_direction) req.bmRequestType |= 0x80;
                req.wIndex = uplink_setup_wIndex;
                req.wLength = uplink_setup_wLength;
                // This word was the last one in the queue: the firmware has
                // deasserted the IRQ line. Ack the latched flag before the
                // card operations below enqueue the follow-up
                // XFER_COMPLETE, so that event re-latches the flag on a
                // fresh edge and the drain resumes next tick.
                if (last) uplink_card_irq_ack();
                uplink_handle_setup(&req);
                uplink_pair_retry = 0;  // pair complete: fresh retry budget
                if (last) { drained = true; break; }
                continue;
            }

            bool last = (raw & (1u << 31)) != 0;
            raw &= ~(1u << 31);
            // HUD: capture the raw first word (bit31 clear) for ANY event, so
            // the ENUM line distinguishes no-data (0x7FFFFFFF, the all-ones
            // "no data" sentinel the no-latch paths now arm) from a real first
            // word (e.g. 0x60080000 for a SETUP) from genuine DSpico-side
            // garbage (anything else).
            uplink_last_setup_word1 = raw;

            if (raw == 0) { drained = true; break; }  // USB_EVENT_NONE: empty
            if (uplink_word_is_allones(raw)) {
                // No data was delivered on this read (the DSpico was busy).
                // Nothing trustworthy was dequeued; stop and let the probe
                // re-read the queue head on the next tick.
                break;
            }
            if (!uplink_event_valid(raw)) {
                // Not a real event (e.g. an orphaned SETUP second word): drop
                // the queue so it cannot be misread on a later drain.
                if (uplink_rom_cmd(DSPICO_CMD_USB_CLEAR_EVENT_QUEUE, UPLINK_OP_EVENT) == UPLINK_ROM_OK) {
                    drained = true;
                    // The clear emptied the queue and deasserted the IRQ
                    // line: ack now so a follow-up event re-latches the flag
                    // on a fresh edge instead of being swallowed by the
                    // end-of-drain ack.
                    uplink_card_irq_ack();
                }
                break;
            }

            // A 32-bit event (everything but the 64-bit SETUP pair, whose
            // "last" bit lives in its second word) dequeued last means the
            // firmware has emptied the queue and deasserted the IRQ line.
            // Ack the latched flag here, before the branches below issue
            // card operations (FINISH_SET_ADDRESS, BEGIN_TRANSFER,
            // EP_CLOSE_ALL) that enqueue follow-up events: those events
            // re-latch the flag on a fresh edge and the drain resumes on
            // the next tick. Acking only at the end of the drain raced with
            // exactly those follow-up events.
            if (last && (raw >> 30) != 1)
                uplink_card_irq_ack();

            if ((raw >> 30) == 1) {
                // USB_EVENT_SETUP_RECEIVED: the host is (re)enumerating. The
                // event is a 64-bit pair, enqueued atomically: first word
                // (wLength/direction/wIndex); a second GET_EVENT carries
                // (wValue/bRequest/bmRequestType).
                if (uplink_state == UP_STATE_CONNECT) uplink_state = UP_STATE_ENUM;
                uplink_setup_wLength = (uint16_t)((raw >> 16) & 0x1FFF);
                uplink_setup_direction = (uint8_t)((raw >> 29) & 1u);
                uplink_setup_wIndex = (uint16_t)(raw & 0xFFFF);

                uint32_t raw2 = 0;
                int r2 = uplink_event_next(&raw2);
                if (r2 != UPLINK_ROM_OK) {
                    // The second word is still queued -- just not readable
                    // right now (its read went in flight, or a DLDI read
                    // grabbed the card between the two words). Remember the
                    // first half and finish the pair on a later drain;
                    // nothing was lost, so there is no queue to clear.
                    uplink_setup_awaiting_second = true;
                    break;
                }
                if (uplink_word_is_allones(raw2)) {
                    // The second-word read completed with no data (all-ones):
                    // the DSpico was busy servicing USB and answered the read
                    // without driving a word. The second entry is still queued
                    // (or arrives moments later), so do not drop yet: remember
                    // the first half and re-read on the next tick. The retry
                    // is bounded by the pair budget (the poller aborts
                    // in-flight reads after ~0.32 s and counts the same
                    // budget); a true give-up drops the pair and the host
                    // retransmits a fresh, aligned pair.
                    uplink_setup_awaiting_second = true;
                    break;
                }

                // The "last" bit of the second word says whether the queue
                // was empty after this SETUP pair was dequeued (the firmware
                // sets it on the dequeue that empties the queue). Strip it
                // before the zero check below so 0x80000000 is caught too.
                bool last2 = (raw2 & (1u << 31)) != 0;
                raw2 &= ~(1u << 31);
                if (raw2 == 0) {
                    // A zero second word is either a real GET_STATUS second
                    // word (the only standard request whose
                    // (wValue, bRequest, bmRequestType) half is all zeros --
                    // and GET_STATUS always has direction=IN and wLength=2)
                    // or a USB_EVENT_NONE answer: the DSpico was still
                    // servicing USB and answered the read with "queue empty"
                    // because the pair's second entry was not enqueued yet.
                    // Word 1 disambiguates: a real zero second word implies a
                    // GET_STATUS first word. On a real GET_STATUS the
                    // assembled request is correct under either
                    // interpretation; otherwise wait and re-read -- a
                    // GET_EVENT on an empty queue stays pending until the
                    // second entry arrives, and the retry is bounded by the
                    // pair budget.
                    if (!(uplink_setup_direction && uplink_setup_wLength == 2)) {
                        uplink_setup_awaiting_second = true;
                        break;
                    }
                }
                // A reserved request type (bits 6:5 == 3) is never a real USB
                // request: the "second word" is a misread (not the pair's
                // second half). Do NOT assemble/ack it -- a zero-length status
                // for a request the host sent expecting data would abort
                // enumeration. Drop the (misaligned) queue; the host
                // retransmits a fresh, aligned pair.
                if (((raw2 >> 29) & 3u) == 3u) {
                    uplink_setup_pair_drop();
                    break;
                }
                uplink_setup_t req;
                req.wValue = (uint16_t)(raw2 & 0xFFFF);
                req.bRequest = (uint8_t)((raw2 >> 16) & 0xFF);
                req.bmRequestType = (uint8_t)((raw2 >> 24) & 0x7F);
                if (uplink_setup_direction) req.bmRequestType |= 0x80;
                req.wIndex = uplink_setup_wIndex;
                req.wLength = uplink_setup_wLength;
                if (last2) {
                    // The pair was the last queued event: the firmware has
                    // deasserted the IRQ line. Ack the latched flag before
                    // the card operations below (the EP0 IN WRITE_DATA)
                    // enqueue the follow-up XFER_COMPLETE, so that event
                    // re-latches the flag on a fresh edge and the drain
                    // resumes next tick.
                    uplink_card_irq_ack();
                }
                uplink_handle_setup(&req);
                uplink_pair_retry = 0;  // pair complete: fresh retry budget
                if (last2) {
                    drained = true;
                    break;
                }
                // More events are still queued (and the IRQ line is still
                // asserted): keep draining this tick instead of acking now --
                // the latched flag would not re-latch (the line never falls
                // and re-rises) and the drain would stall.
            }
            else if ((raw >> 28) == 3) {
                // USB_EVENT_XFER_COMPLETE
                uint32_t endpoint = raw & 0xFF;
                if (endpoint == UPLINK_EP0_IN) {
                    // EP0 IN transfer (data phase or zero-length status)
                    // done. For IN-direction requests (GET_*) the status
                    // phase is a zero-length packet from the host: arm the
                    // empty EP0 OUT buffer so the RP2040 ACKs it instead of
                    // NAKing (the reference host does the same via its EP0
                    // OUT endpoint's BEGIN_TRANSFER). For OUT-direction
                    // requests (SET_*) the zero-length IN above already was
                    // the status phase, so nothing more is sent.
                    uplink_ep0_xfers++;
                    if (uplink_setup_direction)
                        uplink_rom_cmd(DSPICO_CMD_USB_BEGIN_TRANSFER(UPLINK_EP0, 0, 0), UPLINK_OP_EVENT);
                    if (uplink_pending_finish_address) {
                        uplink_rom_cmd(DSPICO_CMD_USB_FINISH_SET_ADDRESS(uplink_pending_address), UPLINK_OP_EVENT);
                        uplink_pending_finish_address = false;
                        uplink_addressed = true;
                    }
                    if (uplink_pending_config) {
                        uplink_configured = (uplink_config_value == 1);
                        uplink_pending_config = false;
                        if (uplink_configured) {
                            uplink_state = UP_STATE_STREAM;
                            uplink_stream_ready = true;
                        }
                    }
                } else if (endpoint == UPLINK_EP_DATA_IN) {
                    uplink_stream_ready = true;  // endpoint free for the next frame
                }
            }
            else if (raw == 1) {
                // USB_EVENT_BUS_RESET: the host reset the bus; re-enumerate.
                if (uplink_state == UP_STATE_STREAM) uplink_state = UP_STATE_ENUM;
                uplink_addressed = false;
                uplink_configured = false;
                uplink_stream_ready = false;
                uplink_pending_finish_address = false;
                uplink_pending_config = false;
                uplink_last_setup_req = 0;
                uplink_pair_retry = 0;  // the host (re)starts enumeration
                uplink_ep0_xfers = 0;
                // Mirror the reference exactly: dcd_event_bus_reset() ->
                // usbd_reset() -> dcd_set_address(0), i.e. ONE
                // BEGIN_SET_ADDRESS (address 0). The firmware keeps its
                // opened endpoints (EP0 IN/OUT, opened once at init) across
                // a bus reset -- the reference never re-opens them -- and
                // the host next sends GET_DESCRIPTOR to address 0, so the
                // firmware pending address must be back to 0. Without this,
                // a device already addressed in a previous attempt keeps
                // its old address and silently drops every address-0 SETUP,
                // so re-enumeration stalls until the host gives up.
                uplink_rom_cmd(DSPICO_CMD_USB_BEGIN_SET_ADDRESS, UPLINK_OP_EVENT);
            }
            else if (raw == 2) {
                // USB_EVENT_UNPLUGGED: the host went away; wait for it to
                // (re)enumerate again.
                uplink_configured = false;
                uplink_addressed = false;
                uplink_stream_ready = false;
                uplink_pending_finish_address = false;
                uplink_pending_config = false;
                if (uplink_state == UP_STATE_STREAM || uplink_state == UP_STATE_ENUM)
                    uplink_state = UP_STATE_CONNECT;
            }
            // raw == 3 (SUSPEND) / 4 (RESUME) / SOF: nothing to do.

            if (last) { drained = true; break; }
        }
    // Fallback ack: if the drain ended without an early ack (e.g. a raw 0
    // / empty queue), clear the latched IRQ flag. On an aborted drain
    // (BUSY/TIMEOUT/orphan) the flag stays set so the drain resumes next
    // tick.
        if (drained) uplink_card_irq_ack();
    }

    // Stream one payload per frame-divider tick once configured.
    uplink_frame_counter++;
    if (uplink_configured && uplink_stream_ready
        && (uplink_frame_counter % UPLINK_FRAME_DIVIDER) == 0) {
        uplink_build_payload(uplink_payload_buf);
        int r = uplink_rom_write_in(uplink_payload_buf, UPLINK_EP_DATA_IN, UPLINK_PAYLOAD_SIZE);
        if (r == UPLINK_ROM_OK) {
            uplink_stream_ready = false;  // wait for XFER_COMPLETE before resending
            uplink_seq = ((uplink_payload_t*)uplink_payload_buf)->seq;
        } else if (r == UPLINK_ROM_TIMEOUT) {
            uplink_card_cooldown = UPLINK_CARD_COOLDOWN_TICKS;
        }
        // UPLINK_ROM_BUSY: the card is doing a DLDI read; retry next tick.
    }
}

uint32_t UplinkGetState(void) {
    return (uint32_t)uplink_state;
}

uint16_t UplinkGetSeq(void) {
    return uplink_seq;
}

// Short human-readable status for the HUD (see UpdateFPS / speedrun_hud.c).
// Returns NULL while the uplink is disabled. All callers run on the main
// routine thread, so the static formatting buffer needs no synchronization.
// The state read here is also read-only, so no locking is needed either.
const char* UplinkStatusString(void) {
    if (!uplink_enabled) return NULL;
    static char buf[40];
    if (uplink_card_dead) {
        // Wedged beyond the reset budget: the DSpico needs a power cycle,
        // then toggle the uplink off/on.
        snprintf(buf, sizeof(buf), "FAIL r%u", (unsigned)uplink_reset_count);
        return buf;
    }
    if (uplink_stuck_grace) {
        // A transfer timed out; the grace period is running. A cart reset
        // only happens if the transfer is still stuck when it expires.
        // cN = which init command (1..8), e = GET_EVENT, w = data write,
        // d = deinit command.
        if (uplink_stuck_op >= UPLINK_OP_INIT_BASE &&
            uplink_stuck_op < UPLINK_OP_INIT_BASE + 8)
            snprintf(buf, sizeof(buf), "BUSY c%u",
                     (unsigned)(uplink_stuck_op - UPLINK_OP_INIT_BASE + 1));
        else if (uplink_stuck_op == UPLINK_OP_EVENT)
            snprintf(buf, sizeof(buf), "BUSY e");
        else if (uplink_stuck_op == UPLINK_OP_WRITE)
            snprintf(buf, sizeof(buf), "BUSY w");
        else
            snprintf(buf, sizeof(buf), "BUSY");
        return buf;
    }
    if (uplink_event_inflight) {
        // A mailbox (GET_EVENT) read is pending: the DSpico is busy
        // servicing USB, or its queue is empty (a read on an empty queue is
        // left pending until the next event). A normal state -- no stuck
        // recovery or cart reset is ever triggered by it.
        return "BUSY e";
    }
    if (uplink_foreign_busy_ticks > UPLINK_FOREIGN_BUSY_LIMIT) {
        // The card is held by something that is not a tracked in-flight
        // read (typically the DSpico finalizing a read we aborted, or
        // servicing USB for a long time). The HUD shows this instead of a
        // misleading steady state.
        return "BUSY f";
    }
    char base[32];  // "ENUM 06800001 w60080000 x255" + NUL
    if (!uplink_initialized) {
        // card commands pending/retrying
        snprintf(buf, sizeof(buf), "INIT");
        return buf;
    }
    switch (uplink_state) {
        case UP_STATE_STREAM:
            snprintf(base, sizeof(base), "OK seq=%u", (unsigned)uplink_seq);
            break;
        case UP_STATE_ENUM:
            // Diagnostics (temporary): first MCD1 latch (w, the value the
            // single-shot read uses) and last MCD1 latch (l) from the most
            // recent GET_EVENT spin/probe, plus EP0 transfers completed.
            // w == l -> no MCD1 rollover between latches (the first latch
            // itself is bad: DSpico/latch-timing side); w != l -> MCD1 is
            // rolling over between latches (the single-shot first-latch
            // capture is the fix). A valid SETUP first word is 60080000
            // (GET_DESCRIPTOR wLength=8 IN wIndex=0).
            snprintf(base, sizeof(base), "ENUM w%08x l%08x x%u",
                     (unsigned)uplink_diag_first,
                     (unsigned)uplink_diag_last,
                     (unsigned)uplink_ep0_xfers);
            break;
        case UP_STATE_CONNECT:
            // "i" = the DSpico's IRQ flag is latched: it has USB events
            // queued (the host is talking to it) or a stale flag. Without
            // it, the DSpico has seen no USB activity since connect.
            snprintf(base, sizeof(base), "WAIT%s",
                     uplink_card_irq_pending() ? " i" : "");
            break;
        default:
            snprintf(base, sizeof(base), "WAIT");
            break;
    }
    // Idle probe verdict since the last probe (shown once a probe has run):
    // p0 = queue answered empty, p1 = a real event was found (the IRQ line/
    // latch is not reaching the NDS -- enumeration is probe-driven),
    // p2 = empty-queue reads are left pending (no events queued).
    // dN = SETUP pairs dropped (a split pair whose second word could not
    // be read; the queue was cleared and the host retransmits).
    if (uplink_probe_verdict < UPLINK_PROBE_VERDICT_NA) {
        if (uplink_pair_drops > 0)
            snprintf(buf, sizeof(buf), "%s p%ud%u", base,
                     (unsigned)uplink_probe_verdict, (unsigned)uplink_pair_drops);
        else
            snprintf(buf, sizeof(buf), "%s p%u", base, (unsigned)uplink_probe_verdict);
    } else {
        snprintf(buf, sizeof(buf), "%s", base);
    }
    return buf;
}
