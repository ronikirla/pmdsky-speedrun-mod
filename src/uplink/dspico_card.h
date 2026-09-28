// NDS Slot-A card controller (ROM mode) primitives + DSpico protocol commands.
//
// Register definitions and CPU data-phase helpers are ported verbatim from
// Gericom/libtwl (develop): common/include/libtwl/card/card.h and
// common/source/card/card.c. The DSpico command encodings come from the
// LNH-team/dspico-usb-examples platform (DSPicoUsb.h).
//
// Concurrency model: the game (ARM9 + ARM7) treats the card lock word as the
// only mutual-exclusion mechanism for card transactions (OS_LockCard is the
// choke point behind Card_LockRom / Card_LockBackup). Every DSpico operation
// here runs inside an acquire/release pair so the game never sees a card
// transaction in flight.
#pragma once

// IMPORTANT: this header must stay free of <pmdsky.h>. The pmdsky-debug
// headers self-define uint8_t/uint32_t/bool/size_t (Ghidra-style, without
// <stdint.h>), which hard-conflicts with the standard fixed-width types
// that TinyUSB pulls in via <stdint.h>/<stdbool.h> (in this toolchain
// uint32_t is `unsigned long`, while pmdsky.h typedefs it `unsigned int`).
// Every uplink header therefore uses the standard types; pmdsky.h is only
// included by translation units that never touch TinyUSB.
#include <stdbool.h>
#include <stdint.h>

//--------------------------------------------------------------------+
// Card controller registers (Slot A / Slot B shared layout)
//--------------------------------------------------------------------+

#define REG_MCCNT0 (*(volatile uint16_t*)0x040001A0)
#define REG_MCD0   (*(volatile uint16_t*)0x040001A2)
#define REG_MCCNT1 (*(volatile uint32_t*)0x040001A4)
#define REG_MCCMD0 (*(volatile uint32_t*)0x040001A8)
#define REG_MCCMD1 (*(volatile uint32_t*)0x040001AC)
#define REG_MCD1   (*(volatile uint32_t*)0x04100010)

// REG_MCCNT0
#define MCCNT0_MODE_MASK      (1 << 13)
#define MCCNT0_MODE_ROM       (0 << 13)
#define MCCNT0_MODE_SPI       (1 << 13)
#define MCCNT0_ROM_XFER_IRQ   (1 << 14)
#define MCCNT0_ENABLE         (1 << 15)

// REG_MCCNT1
#define MCCNT1_LATENCY1(x)    (x)
#define MCCNT1_READ_DATA_DESCRAMBLE (1 << 13)
#define MCCNT1_CLOCK_SCRAMBLER      (1 << 14)
#define MCCNT1_LATENCY2(x)    (((x) << 16) & 0x3F0000)
#define MCCNT1_CMD_SCRAMBLE   (1 << 22)
#define MCCNT1_DATA_READY     (1 << 23)
#define MCCNT1_LEN_0          (0 << 24)
#define MCCNT1_LEN_512        (1 << 24)
#define MCCNT1_LEN_4          (7 << 24)
#define MCCNT1_CLK_6_7_MHZ    (0 << 27)
#define MCCNT1_CLK_4_2_MHZ    (1 << 27)
#define MCCNT1_RESET_OFF      (1 << 29)
#define MCCNT1_DIR_READ       (0 << 30)
#define MCCNT1_DIR_WRITE      (1 << 30)
#define MCCNT1_ENABLE         (1 << 31)  // doubles as the busy bit

#define DSPICO_LOCK_ID_SPINS   1000
#define DSPICO_BUSY_SPINS      2000000  // ~0.3 s worst case at 67 MHz, tune
#define OS_MODE_IRQ            0x12

//--------------------------------------------------------------------+
// DSpico USB protocol (DSpicoUsb.h from the LNH-team examples)
//--------------------------------------------------------------------+

#define DSPICO_CMD_USB_COMMAND(command, arguments) \
    (0xE800000000000000ull | ((uint64_t)(command) << 48) | (arguments))
#define DSPICO_CMD_USB_COMMAND_INIT                     DSPICO_CMD_USB_COMMAND(1, 0)
#define DSPICO_CMD_USB_COMMAND_BEGIN_SET_ADDRESS        DSPICO_CMD_USB_COMMAND(2, 0)
#define DSPICO_CMD_USB_COMMAND_REMOTE_WAKEUP            DSPICO_CMD_USB_COMMAND(3, 0)
#define DSPICO_CMD_USB_COMMAND_CONNECT                  DSPICO_CMD_USB_COMMAND(4, 0)
#define DSPICO_CMD_USB_COMMAND_DISCONNECT               DSPICO_CMD_USB_COMMAND(5, 0)
#define DSPICO_CMD_USB_COMMAND_LOCAL_STACK              DSPICO_CMD_USB_COMMAND(19, 0)
#define DSPICO_CMD_USB_COMMAND_SOF_ENABLE               DSPICO_CMD_USB_COMMAND(6, 0)
#define DSPICO_CMD_USB_COMMAND_SOF_DISABLE              DSPICO_CMD_USB_COMMAND(7, 0)
#define DSPICO_CMD_USB_COMMAND_EP_CLOSE_ALL             DSPICO_CMD_USB_COMMAND(8, 0)
#define DSPICO_CMD_USB_COMMAND_EP_STALL(ep)             DSPICO_CMD_USB_COMMAND(9, ((uint64_t)(ep) << 40))
#define DSPICO_CMD_USB_COMMAND_EP_CLEAR_STALL(ep)       DSPICO_CMD_USB_COMMAND(10, ((uint64_t)(ep) << 40))
#define DSPICO_CMD_USB_COMMAND_EP_CLOSE(ep)             DSPICO_CMD_USB_COMMAND(11, ((uint64_t)(ep) << 40))
#define DSPICO_CMD_USB_COMMAND_FINISH_SET_ADDRESS(addr) DSPICO_CMD_USB_COMMAND(12, ((uint64_t)(addr) << 40))
#define DSPICO_CMD_USB_COMMAND_EP_OPEN(ep, maxPktSize, xfer) \
    DSPICO_CMD_USB_COMMAND(13, (((uint64_t)(ep) << 40) | ((uint64_t)((xfer) & 3) << 32) | ((uint64_t)((maxPktSize) & 0x7FF))))
#define DSPICO_CMD_USB_COMMAND_CLEAR_EVENT_QUEUE        DSPICO_CMD_USB_COMMAND(14, 0)
#define DSPICO_CMD_USB_COMMAND_DEINIT                   DSPICO_CMD_USB_COMMAND(15, 0)
#define DSPICO_CMD_USB_COMMAND_BEGIN_TRANSFER(ep, offset, length) \
    DSPICO_CMD_USB_COMMAND(16, (((uint64_t)(ep) << 40) | ((uint64_t)(offset) << 32) | (length)))
#define DSPICO_CMD_USB_COMMAND_INTERRUPT_ENABLE         DSPICO_CMD_USB_COMMAND(17, 0)
#define DSPICO_CMD_USB_COMMAND_INTERRUPT_DISABLE        DSPICO_CMD_USB_COMMAND(18, 0)

// Reads the next word from the DSpico event queue (bit 31 = last event)
#define DSPICO_CMD_USB_GET_EVENT                        (0xEB00000000000000ull)

#define DSPICO_CMD_USB_WRITE_DATA(offset, endpoint, isLast, totalLength) \
    (0xE900000000000000ull | ((uint64_t)(offset) << 48) | ((uint64_t)(endpoint) << 40) | ((uint64_t)(isLast) << 32) | (totalLength))
#define DSPICO_CMD_USB_READ_DATA(offset, endpoint) \
    (0xEA00000000000000ull | ((uint64_t)(offset) << 32) | ((uint64_t)(endpoint) << 40))
// MCCNT1 settings for plain command transactions (no data phase)
#define DSPICO_USB_DEFAULT_COMMAND_SETTINGS \
    (MCCNT1_DIR_READ | MCCNT1_RESET_OFF | MCCNT1_CLK_6_7_MHZ | MCCNT1_LEN_0 | MCCNT1_CMD_SCRAMBLE | \
     MCCNT1_LATENCY2(0) | MCCNT1_CLOCK_SCRAMBLER | MCCNT1_READ_DATA_DESCRAMBLE | MCCNT1_LATENCY1(0))

// MCCNT1 settings for 512-byte card data phases (USB IN = write data to card)
#define DSPICO_USB_WRITE_DATA_SETTINGS \
    (MCCNT1_DIR_WRITE | MCCNT1_RESET_OFF | MCCNT1_CLK_6_7_MHZ | MCCNT1_LEN_512 | MCCNT1_CMD_SCRAMBLE | \
     MCCNT1_LATENCY2(8) | MCCNT1_CLOCK_SCRAMBLER | MCCNT1_READ_DATA_DESCRAMBLE | MCCNT1_LATENCY1(0))

// MCCNT1 settings for 512-byte card data phases (USB OUT = read data from card)
#define DSPICO_USB_READ_DATA_SETTINGS \
    (MCCNT1_DIR_READ | MCCNT1_RESET_OFF | MCCNT1_CLK_6_7_MHZ | MCCNT1_LEN_512 | MCCNT1_CMD_SCRAMBLE | \
     MCCNT1_LATENCY2(4) | MCCNT1_CLOCK_SCRAMBLER | MCCNT1_LATENCY1(0))

//--------------------------------------------------------------------+
// Game lock API (PMDSky, verified against Ghidra in Phase 0)
//--------------------------------------------------------------------+

int    OS_GetLockID(void);        // 0x020793C4, returns -1 when no lock is free
void   OS_ReleaseLockId(int id);  // 0x0207942C
void   Card_LockRom(uint16_t id); // 0x020837CC, waits until the card is free
void   Card_UnlockRom(uint16_t id); // 0x020837E8
uint32_t OS_GetProcMode(void);    // 0x0207BBE0

//--------------------------------------------------------------------+
// Primitive card operations (no locking; caller must hold the lock)
//--------------------------------------------------------------------+

// Writes the 64-bit command (bswap64) across REG_MCCMD0/1
void   dspico_card_set_cmd(uint64_t cmd);
// Kicks off a ROM transaction with the given MCCNT1 settings
void   dspico_card_start_xfer(uint32_t settings, bool irq);
bool   dspico_card_is_busy(void);      // MCCNT1 bit 31
void   dspico_card_wait_busy(void);
bool   dspico_card_is_data_ready(void); // MCCNT1 bit 23
uint32_t dspico_card_get_data(void);   // read REG_MCD1
// Uplink calls this before a reset. Afterwards lock_wait always returns 0.
void dspico_card_set_shutdown(bool shutdown);

// CPU data-phase transfers. `words` is a 32-bit word count (LEN_512 == 512
// bytes == 128 words). Both loop until the busy bit clears, feeding or
// draining REG_MCD1 while DATA_READY is set. Unaligned-safe byte access.
//
// cpu_write reads at most `valid_bytes` bytes from `src` and zero-pads the
// rest of the phase (the DSpico only transfers `valid_bytes` of each 512-byte
// double buffer; the remainder must be well-formed, not OOB garbage).
void   dspico_card_cpu_read(void* dst, uint32_t words);
void   dspico_card_cpu_write(const void* src, uint32_t words, uint32_t valid_bytes);

//--------------------------------------------------------------------+
// Locked wrappers (safe to call from the uplink thread)
//--------------------------------------------------------------------+

// Acquire the game card lock. Returns the lock id, or 0 if no lock was
// available (caller must skip the operation).
uint16_t dspico_card_lock_acquire(void);
void     dspico_card_lock_release(uint16_t lock_id);

// Acquire the game card lock. Returns the lock id, or 0 on failure (shutdown
// requested, called from IRQ mode, no lock ID free, or the card never became
// free). Callers MUST check for 0 and abandon the transaction.
uint16_t dspico_card_lock_wait(void);

