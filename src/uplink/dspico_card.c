#include "dspico_card.h"
#include "uplink_sampler.h"

//--------------------------------------------------------------------+
// Card-bus watchdog
//
// Every DSpico transaction here runs while the game's card lock is held, so
// an unbounded wait on the card controller would freeze the game's own card
// I/O (overlay loads, saves, EEPROM, ...) at whatever point of the game the
// wedge happened to hit. All waits are therefore bounded by DSPICO_BUSY_SPINS
// / DSPICO_LOCK_ID_SPINS; on timeout the in-flight transaction is
// force-aborted (MCCNT1 ENABLE cleared), the bus is flagged dead and the
// uplink stops issuing card transactions for the rest of the session
// (UplinkPoll checks dspico_card_is_dead()). The game keeps running, just
// without USB streaming.
//--------------------------------------------------------------------+

static bool s_card_dead = false;

bool dspico_card_is_dead(void) {
  return s_card_dead;
}

// Force the card controller out of the in-flight transaction so the game's
// next card access starts from a clean controller state. The game's card
// driver fully reprograms MCCNT0/MCCNT1 and the command registers for every
// transaction of its own, so nothing else needs to be restored here.
static void dspico_card_abort_xfer(void) {
  REG_MCCNT1 &= ~(uint32_t)MCCNT1_ENABLE;
  s_card_dead = true;
  uplink_card_busy_timeouts++;
}

// card_romSetCmd from Gericom/libtwl: a single 64-bit bswap store over the
// two command registers. On ARM9 GCC emits two 32-bit stores (MCCMD0 first,
// then MCCMD1), matching the verified command write order.
void dspico_card_set_cmd(uint64_t cmd) {
  *(volatile uint64_t*)&REG_MCCMD0 = __builtin_bswap64(cmd);
}

// card_romStartXfer from Gericom/libtwl: preserve unrelated MCCNT0 bits,
// force ROM mode, (optionally) arm the transfer IRQ, enable; then MCCNT1.
void dspico_card_start_xfer(uint32_t settings, bool irq) {
  REG_MCCNT0 = (REG_MCCNT0 & ~(MCCNT0_MODE_MASK | MCCNT0_ROM_XFER_IRQ)) |
               MCCNT0_MODE_ROM | (irq ? MCCNT0_ROM_XFER_IRQ : 0) | MCCNT0_ENABLE;
  REG_MCCNT1 = MCCNT1_ENABLE | settings;
}

bool dspico_card_is_busy(void) {
  return REG_MCCNT1 & MCCNT1_ENABLE;
}

// Bounded: a plain command transaction finishes within microseconds, so
// DSPICO_BUSY_SPINS iterations (~0.3 s) without the busy bit clearing means
// the DSpico firmware is wedged.
void dspico_card_wait_busy(void) {
  if (s_card_dead) {
    return;
  }
  uint32_t spins = 0;
  while (dspico_card_is_busy()) {
    if (++spins > DSPICO_BUSY_SPINS) {
      dspico_card_abort_xfer();
      return;
    }
  }
}

bool dspico_card_is_data_ready(void) {
  return REG_MCCNT1 & MCCNT1_DATA_READY;
}

uint32_t dspico_card_get_data(void) {
  return REG_MCD1;
}

// card_romCpuReadUnaligned from Gericom/libtwl: drain the data phase until
// the busy bit clears, storing at most `words` 32-bit words (byte-wise, so
// no alignment requirement on dst).
// Bounded: a legitimate 512-byte phase takes ~77 ms at 6.7 MHz (a few tens
// of thousands of loop iterations), so DSPICO_BUSY_SPINS iterations without
// the phase completing means the card is wedged.
void dspico_card_cpu_read(void* dst, uint32_t words) {
  if (s_card_dead) {
    return;
  }
  uint8_t* d = (uint8_t*)dst;
  uint8_t* target = d + (words << 2);
  uint32_t spins = 0;
  do {
    if (++spins > DSPICO_BUSY_SPINS) {
      dspico_card_abort_xfer();
      return;
    }
    if (dspico_card_is_data_ready()) {
      uint32_t data = dspico_card_get_data();
      if (d < target) {
        *d++ = data & 0xFF;
        *d++ = (data >> 8) & 0xFF;
        *d++ = (data >> 16) & 0xFF;
        *d++ = (data >> 24) & 0xFF;
      }
    }
  } while (dspico_card_is_busy());
}

// card_romCpuWriteUnaligned from Gericom/libtwl: feed the data phase until
// the busy bit clears. Keeps the card controller's data FIFO supplied by
// checking DATA_READY; once src is exhausted it clocks zero words so the
// full LEN_512 phase always completes.
// Bounded: see dspico_card_cpu_read.
void dspico_card_cpu_write(const void* src, uint32_t words, uint32_t valid_bytes) {
  if (s_card_dead) {
    return;
  }
  uint32_t data = 0;
  const uint8_t* s = (const uint8_t*)src;
  const uint8_t* data_end = s + valid_bytes;
  (void) words; // the phase length is fixed by start_xfer (LEN_512);
  // we simply keep clocking zero words until busy clears
  uint32_t spins = 0;
  do {
    if (++spins > DSPICO_BUSY_SPINS) {
      dspico_card_abort_xfer();
      return;
    }
    if (dspico_card_is_data_ready()) {
      if (s < data_end) {
        if (data_end - s >= 4) {
          // full 4-byte chunk of valid payload
          data = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);
          s += 4;
        } else {
          // tail: fewer than 4 valid bytes left; zero-pad the word
          data = 0;
          for (uint32_t i = 0; i < (uint32_t)(data_end - s); i++) {
            data |= (uint32_t)s[i] << (i * 8);
            s++;
          }
        }
      } else {
        // zero-pad the remainder of the phase (never read past data_end)
        data = 0;
      }
      REG_MCD1 = data;
    }
  } while (dspico_card_is_busy());
}

uint16_t dspico_card_lock_acquire(void) {
  int id = OS_GetLockID();
  if (id < 0) {
    return 0;
  }
  Card_LockRom((uint16_t)id); // waits until the card is free
  return (uint16_t)id;
}

void dspico_card_lock_release(uint16_t lock_id) {
  Card_UnlockRom(lock_id);
  OS_ReleaseLockId(lock_id);
}

// Bounded: if the lock id pool is exhausted (one id is permanently reserved
// for the uplink and the mod's own save code caches another), give up after
// DSPICO_LOCK_ID_SPINS attempts instead of spinning forever. Returns
// DSPICO_LOCK_ID_INVALID; the caller must skip the transaction.
uint16_t dspico_card_lock_wait(void) {
  for (uint32_t i = 0; i < DSPICO_LOCK_ID_SPINS; i++) {
    int id = OS_GetLockID();
    if (id >= 0) {
      Card_LockRom((uint16_t)id); // waits until the card is free
      return (uint16_t)id;
    }
  }
  return DSPICO_LOCK_ID_INVALID;
}

// Persistent lock id for the uplink (see dspico_card.h): reserved once
// at UplinkInit and never returned to the game's free list, so the id
// can never be handed out to the game's own card transactions.
static uint16_t s_reserved_lock_id = 0;
static bool s_reserved_valid = false;

bool dspico_card_lock_reserve(void) {
  if (s_reserved_valid) {
    return true;
  }
  int id = OS_GetLockID();
  if (id < 0) {
    return false;
  }
  s_reserved_lock_id = (uint16_t)id;
  s_reserved_valid = true;
  return true;
}

uint16_t dspico_card_lock_wait_persistent(void) {
  if (s_card_dead) {
    return DSPICO_LOCK_ID_INVALID;
  }
  if (s_reserved_valid) {
    Card_LockRom(s_reserved_lock_id); // waits until the card is free
    return s_reserved_lock_id;
  }
  return dspico_card_lock_wait();
}

void dspico_card_lock_release_persistent(uint16_t lock_id) {
  if (lock_id == DSPICO_LOCK_ID_INVALID) {
    return; // nothing was acquired
  }
  if (s_reserved_valid && lock_id == s_reserved_lock_id) {
    Card_UnlockRom(lock_id); // id stays reserved (not released to the game)
    return;
  }
  dspico_card_lock_release(lock_id);
}
