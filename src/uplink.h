#pragma once

#include <pmdsky.h>

// ---------------------------------------------------------------------------
// PMD:ES speedrun "uplink": streams autosplitter values from game memory to a
// PC Livesplit autosplitter over a DSpico USB CDC-ACM (serial) device.
//
// The whole thing runs on ARM9 only. The DSpico card is driven through the
// game card ROM interface (MCCNT0/MCCNT1/MCCMD0/MCCMD1/MCD1 + a DMA3 feed for
// bulk IN writes). Every card touch is wrapped in Card_LockRom()/Card_UnlockRom()
// and is best-effort: if the card is busy (e.g. a DLDI read) the frame is
// skipped instead of blocking the game.
// ---------------------------------------------------------------------------

// --- Wire protocol ---------------------------------------------------------
#define UPLINK_MAGIC   0x5350u  // "PS" little-endian
#define UPLINK_VERSION 0x01u
#define UPLINK_PAYLOAD_SIZE 36u

// Endpoint numbers / addresses for the CDC-ACM device we present.
#define UPLINK_EP0            0x00  // control
#define UPLINK_EP_DATA_IN     0x81  // bulk IN (the data stream)
#define UPLINK_EP_DATA_OUT    0x01  // bulk OUT (unused, required by CDC-ACM)
#define UPLINK_EP_NOTIFY_IN   0x82  // interrupt IN (serial state, unused)

// --- Control transfer request codes (USB 2.0 standard) --------------------
#define UPLINK_REQ_GET_STATUS        0x00
#define UPLINK_REQ_SET_ADDRESS       0x05
#define UPLINK_REQ_GET_DESCRIPTOR    0x06
#define UPLINK_REQ_GET_CONFIGURATION 0x08
#define UPLINK_REQ_SET_CONFIGURATION 0x09
#define UPLINK_REQ_GET_INTERFACE     0x0A // also SET_IDLE (direction disambiguates)
#define UPLINK_REQ_SET_INTERFACE     0x0B
#define UPLINK_DESC_DEVICE           0x00
#define UPLINK_DESC_CONFIGURATION    0x02
#define UPLINK_DESC_STRING           0x03

// Endpoint transfer types (bmAttributes.xfer)
#define UPLINK_XFER_CONTROL 0
#define UPLINK_XFER_BULK  2
#define UPLINK_XFER_INT   3

// --- High-level state (for debugging / the PC side) -----------------------
typedef enum {
    UP_STATE_UNINIT = 0,
    UP_STATE_INIT,
    UP_STATE_CONNECT,
    UP_STATE_ENUM,
    UP_STATE_STREAM,
    UP_STATE_DISABLED,
    UP_STATE_FAIL,
} uplink_state_t;

// ---------------------------------------------------------------------------
// The 36-byte little-endian packed payload.
//
// NOTE ON FIELDS: the play-time / main-menu / dungeon fields are confirmed
// against the PMD:ES symbol table. The anonymous 0x22xxxx / 0x23xxxx fields
// are the autosplitter trigger values identified during reverse-engineering;
// their exact size/meaning should be confirmed before relying on them for
// splits (see uplink.c for the exact addresses read).
// ---------------------------------------------------------------------------
typedef struct uplink_payload {
    // header (8 bytes)
    uint16_t magic;        // 0x00  UPLINK_MAGIC
    uint8_t  version;      // 0x02  UPLINK_VERSION
    uint8_t  flags;        // 0x03  bit0 = in dungeon mode, bit1 = card was busy (skipped)
    uint16_t seq;          // 0x04  sequence counter (wraps at 0x10000)
    uint16_t crc16;        // 0x06  CRC16-CCITT over the payload with crc16==0

    // data (28 bytes)
    uint32_t play_time_seconds;  // 0x08  PLAY_TIME_SECONDS (0x22ABFD4)
    uint8_t  play_time_frames;   // 0x0C  PLAY_TIME_FRAME_COUNTER (0x22ABFD8)
    uint8_t  v_22ABAA8;          // 0x0D
    uint8_t  v_22ABAA9;          // 0x0E
    uint8_t  v_22ABADB;          // 0x0F
    uint8_t  v_2325ACA;          // 0x10
    uint32_t v_22A40E4;          // 0x11
    uint32_t main_menu_magic;    // 0x15  0x22A3670 (0x22A3E94 => main menu)
    uint32_t v_2329D40;          // 0x19
    uint8_t  dungeon_end_floor_flag; // 0x1D  *DUNGEON_PTR + 0x6
    uint8_t  dungeon_id;               // 0x1E  *DUNGEON_PTR + 0x748
    uint8_t  dungeon_floor;            // 0x1F  *DUNGEON_PTR + 0x749
    uint8_t  pad[4];                   // 0x20..0x23 reserved
} __attribute__((packed)) uplink_payload_t;

ASSERT_SIZE(uplink_payload_t, UPLINK_PAYLOAD_SIZE);

// --- Public API ------------------------------------------------------------

// Brings the DSpico USB stack up (INIT + CONNECT). Only runs from UplinkTick()
// while the uplink is enabled. Safe to call more than once; subsequent calls
// are no-ops.
void UplinkInit(void);

// Cleanly tears down the DSpico USB state machine (DEINIT + disconnect) before
// a soft reset. Safe to call from any thread; also a no-op if not initialized.
void UplinkDeinit(void);

// Called once per frame from MainRoutine. While the uplink is disabled (the
// default) this is a no-op that never touches the card. When enabled it
// advances the state machine, drains DSpico events, services enumeration, and
// (when configured) streams one payload per frame-divider tick. Best-effort:
// a DSpico event read may briefly block the frame (up to a few ms inline,
// after which the pending read is polled on later frames instead of
// blocking), but nothing here ever resets the card while a mailbox read is
// pending.
void UplinkTick(void);

// --- Enable / disable ------------------------------------------------------
// The uplink is OFF by default and must be explicitly enabled, so the ROM is
// safe on emulators and on flashcarts without a DSpico: while disabled, no
// card ROM interface access or card locking happens at all. Enabling is only
// possible from the main menu (Start + Up, see HandleUplinkToggle); disabling
// cleanly disconnects the DSpico USB device. The setting is per-session and
// is not saved to the card EEPROM.
void UplinkSetEnabled(bool enabled);
bool UplinkIsEnabled(void);

// Main-menu hotkey handler (Start + Up) toggling the uplink. Called once per
// frame from MainRoutine; same pattern as HandleSpeedToggle.
void HandleUplinkToggle(void);

// Current state + last sequence, for debugging on the HUD if ever needed.
uint32_t UplinkGetState(void);
uint16_t UplinkGetSeq(void);

// Short human-readable status for the HUD: NULL while disabled, otherwise
// "FAIL rN" (the card is wedged beyond N cart resets; power-cycle the
// DSpico, then toggle off/on to retry), "BUSY [cN|e|w|d]" (a transfer
// timed out; the grace period is running before any reset is attempted --
// cN = which init command, e = GET_EVENT, w = data write, d = deinit),
// "INIT" (card commands still pending/retrying), "WAIT" / "WAIT i"
// (initialized, waiting for the PC to enumerate; "i" = the DSpico's IRQ
// flag is latched, i.e. it has USB events queued), "ENUM r xN" (host is
// enumerating; r = compact code of the last processed SETUP request:
// g=GET_DESCRIPTOR, s=GET_STATUS, c=GET_CONFIGURATION, i=GET_INTERFACE,
// a=SET_ADDRESS, C=SET_CONFIGURATION, I=SET_INTERFACE, ?=other, -=none
// yet; N = EP0 transfers completed since the last (re)connect -- if
// enumeration stalls, these show which control transfer it broke on) or
// "OK seq=N" (configured and streaming). Only safe to call
// from the main routine thread.
// Note: "BUSY e" is also shown while a GET_EVENT mailbox read is pending
// (the DSpico is busy servicing USB, or its queue is empty and the read is
// left pending until the next event). That state never triggers the stuck
// recovery or a cart reset; only cN/w/d timeouts can lead to one.
const char* UplinkStatusString(void);

// True while the uplink is in the ENUM state (the PC is enumerating the
// device). Used to temporarily suppress the HUD timer so the long
// "USB:ENUM ..." diagnostic line is readable. Only safe to call from the
// main routine thread.
bool UplinkIsEnumerating(void);

// Frame-rate divider: 1 = every frame (~60 Hz), 3 = ~20 Hz, 10 = ~6 Hz.
#define UPLINK_FRAME_DIVIDER 1u
