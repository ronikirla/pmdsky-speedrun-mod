#include <pmdsky.h>
#include "custom_headers.h"
#include "eeprom.h"
#include "crash_dump.h"

// First trigger to fire wins; later calls (e.g. a FatalError that cascades
// into OS_Panic, or the watchdog firing after a hook) must not overwrite the
// record.
static volatile uint32_t crash_dump_active = 0;

// The record is staged in RAM one piece at a time (header, then one thread
// record at a time) and written out immediately. Staging must not yield while
// a piece is being built (the scheduler would let other threads run and
// mutate the very stacks being copied); the card writes in between yields are
// fine, each piece is already consistent when it is handed to the card
// driver.
static uint32_t crash_dump_record_buffer[CRASH_DUMP_RECORD_BUFFER_SIZE / 4];

// One collected thread from THREAD_INFO_STRUCT's list. The stack bounds are
// normalized to [lo, hi) so the dump does not depend on which of
// thread::stack_pointer / thread::stack_end_pointer is the low end. All other
// fields are re-read from the struct when the record is staged.
struct crash_thread_slot
{
  struct thread *t;
  uint32_t lo;
  uint32_t hi;
  bool bounds_ok;
};

#define CRASH_DUMP_MAX_THREADS 16
// A thread whose stack area is larger than this is not a plausible struct.
#define CRASH_DUMP_MAX_STACK_SIZE 0x10000
#define CRASH_DUMP_MAIN_RAM_START 0x02000000
#define CRASH_DUMP_MAIN_RAM_END 0x02400000

static struct crash_thread_slot crash_thread_slots[CRASH_DUMP_MAX_THREADS];

// Walk the priority-sorted thread list (thread_info::thread_list_head, head =
// highest priority) and collect one slot per thread. Pointers that cannot
// plausibly be thread structs end the walk, and a cycle in the list is broken
// by the duplicate check.
// Returns the number of slots; *current_index gets the slot whose stack area
// contains sp_base (the current/crashing thread), or -1 when it is not in the
// list.
static uint32_t CollectThreadSlots(uint32_t sp_base, int *current_index)
{
  uint32_t count = 0;
  *current_index = -1;
  struct thread *t = THREAD_INFO_STRUCT.thread_list_head;
  for (uint32_t i = 0; i < CRASH_DUMP_MAX_THREADS && t != NULL; i++)
  {
    uint32_t addr = (uint32_t)t;
    if ((addr & 3) != 0 || addr < CRASH_DUMP_MAIN_RAM_START || addr >= CRASH_DUMP_MAIN_RAM_END)
    {
      break;
    }
    bool seen = false;
    for (uint32_t j = 0; j < count; j++)
    {
      if (crash_thread_slots[j].t == t)
      {
        seen = true;
        break;
      }
    }
    if (seen)
    {
      break;
    }
    struct crash_thread_slot *slot = &crash_thread_slots[count];
    slot->t = t;
    uint32_t a = (uint32_t)t->stack_pointer;
    uint32_t b = (uint32_t)t->stack_end_pointer;
    slot->lo = (a < b) ? a : b;
    slot->hi = (a < b) ? b : a;
    slot->bounds_ok = (slot->hi - slot->lo) <= CRASH_DUMP_MAX_STACK_SIZE &&
                      slot->lo >= CRASH_DUMP_MAIN_RAM_START && slot->hi <= CRASH_DUMP_MAIN_RAM_END;
    count++;
    t = t->next_thread;
  }
  for (uint32_t j = 0; j < count; j++)
  {
    if (crash_thread_slots[j].bounds_ok &&
        sp_base >= crash_thread_slots[j].lo && sp_base < crash_thread_slots[j].hi)
    {
      *current_index = (int)j;
      break;
    }
  }
  return count;
}

// Stage the 0x100 header (including the checksum) into the staging buffer.
static void StageHeader(uint32_t hook_id, const uint32_t *regs,
                        uint32_t trigger_buttons, uint32_t total_threads)
{
  uint32_t *header = crash_dump_record_buffer;
  for (uint32_t i = 0; i < CRASH_DUMP_HEADER_SIZE / 4; i++)
  {
    header[i] = 0;
  }
  header[CRASH_DUMP_OFF_MAGIC / 4] = CRASH_DUMP_MAGIC;
  header[CRASH_DUMP_OFF_VERSION / 4] = CRASH_DUMP_VERSION;
  header[CRASH_DUMP_OFF_HOOK_ID / 4] = hook_id;
  header[CRASH_DUMP_OFF_TICK / 4] = OS_GetTickLo();
  header[CRASH_DUMP_OFF_PC / 4] = regs[CRASH_DUMP_REG_PC];
  header[CRASH_DUMP_OFF_LR / 4] = regs[CRASH_DUMP_REG_LR];
  header[CRASH_DUMP_OFF_SP / 4] = regs[CRASH_DUMP_REG_SP];
  header[CRASH_DUMP_OFF_CPSR / 4] = regs[CRASH_DUMP_REG_CPSR];
  for (uint32_t i = 0; i < 13; i++)
  {
    header[(CRASH_DUMP_OFF_R0 / 4) + i] = regs[CRASH_DUMP_REG_R0 + i];
  }
  header[CRASH_DUMP_OFF_ARG0 / 4] = regs[CRASH_DUMP_REG_R0];
  header[CRASH_DUMP_OFF_ARG1 / 4] = regs[CRASH_DUMP_REG_R0 + 1];
  header[CRASH_DUMP_OFF_ARG2 / 4] = regs[CRASH_DUMP_REG_R0 + 2];
  header[CRASH_DUMP_OFF_ARG3 / 4] = regs[CRASH_DUMP_REG_R0 + 3];
  header[CRASH_DUMP_OFF_TRIGGER_BUTTONS / 4] = trigger_buttons;
  // All records always fit (see StageThreadRecord), so the counts agree; the
  // parser still cross-checks what it finds against these.
  header[CRASH_DUMP_OFF_THREAD_COUNT / 4] = total_threads;
  header[CRASH_DUMP_OFF_THREADS_WRITTEN / 4] = total_threads;
  header[CRASH_DUMP_OFF_CRASHING_INDEX / 4] = 0;

  // The FatalError format string (the "official" crash reason) is the original
  // r1 (ABI: r0 = &{file,line} prog_pos, r1 = fmt, r2.. = variadic), which the
  // hook stub already saved into regs[CRASH_DUMP_REG_R1]. OS_Panic takes no
  // arguments, so a message only exists on the FatalError path. Copy it only
  // when the pointer falls in the cartridge region, where all the game's
  // string literals live - reads there are always safe, and a corrupted
  // pointer outside the range is rejected.
  if (hook_id == CRASH_DUMP_HOOK_FATAL_ERROR)
  {
    uint32_t fmt = regs[CRASH_DUMP_REG_R0 + 1];
    if (fmt >= 0x02000000 && fmt < 0x08000000)
    {
      const uint8_t *src = (const uint8_t *)fmt;
      uint8_t *dst = (uint8_t *)header;
      uint32_t len = 0;
      while (len < CRASH_DUMP_MSG_MAX && src[len] != 0)
      {
        dst[CRASH_DUMP_OFF_MSG + len] = src[len];
        len++;
      }
      header[CRASH_DUMP_OFF_MSG_LEN / 4] = len;
    }
  }

  // Checksum covers the whole header except the checksum word itself and the
  // complete flag (which is only written after everything else).
  uint32_t checksum = 0;
  for (uint32_t i = 0; i < CRASH_DUMP_HEADER_SIZE / 4; i++)
  {
    if (i == CRASH_DUMP_OFF_CHECKSUM / 4 || i == CRASH_DUMP_OFF_COMPLETE / 4)
    {
      continue;
    }
    checksum += header[i];
  }
  header[CRASH_DUMP_OFF_CHECKSUM / 4] = checksum;
}

// Stage one thread record into the staging buffer. The snapshot gets a fair
// share of what is left of the record region (so all threads always fit),
// clamped to the thread's used stack and the staging buffer size.
// Returns the staged byte count (0 when the region is full); *rec_offset gets
// the offset inside the record where the caller must write it.
static uint32_t StageThreadRecord(uint32_t offset, uint32_t *records_left,
                                  struct crash_thread_slot *slot,
                                  uint32_t sp, uint32_t pc, uint32_t lr,
                                  bool is_current, uint32_t *rec_offset)
{
  uint32_t remaining = CRASH_DUMP_SIZE - offset;
  if (*records_left == 0 || remaining < CRASH_DUMP_THREAD_RECORD_HEADER_SIZE)
  {
    return 0;
  }
  uint32_t share = remaining / *records_left;
  uint32_t max_snap = (share > CRASH_DUMP_THREAD_RECORD_HEADER_SIZE)
                          ? share - CRASH_DUMP_THREAD_RECORD_HEADER_SIZE
                          : 0;
  if (max_snap > CRASH_DUMP_MAX_SNAPSHOT_BYTES)
  {
    max_snap = CRASH_DUMP_MAX_SNAPSHOT_BYTES;
  }
  bool sp_valid = slot != NULL && slot->bounds_ok && sp >= slot->lo && sp < slot->hi;
  uint32_t snap = 0;
  if (sp_valid)
  {
    snap = slot->hi - sp;
    if (snap > max_snap)
    {
      snap = max_snap;
    }
  }
  else if (is_current)
  {
    // The live stack of the triggering thread: the sp is known good even when
    // no struct thread matched it (e.g. crash before the thread was inserted
    // into the list).
    if (sp < CRASH_DUMP_MAIN_RAM_END)
    {
      snap = max_snap;
      if (sp + snap > CRASH_DUMP_MAIN_RAM_END)
      {
        snap = CRASH_DUMP_MAIN_RAM_END - sp;
      }
    }
  }
  snap &= ~3u;

  uint32_t *rec = crash_dump_record_buffer;
  rec[CRASH_DUMP_THREAD_OFF_MAGIC / 4] = CRASH_DUMP_THREAD_RECORD_MAGIC;
  rec[CRASH_DUMP_THREAD_OFF_THREAD_ID / 4] = (slot && slot->t) ? (uint32_t)slot->t->thread_id : 0xFFFFFFFF;
  rec[CRASH_DUMP_THREAD_OFF_PRIORITY / 4] = (slot && slot->t) ? (uint32_t)slot->t->sorting_order : 0xFFFFFFFF;
  rec[CRASH_DUMP_THREAD_OFF_PC / 4] = pc;
  rec[CRASH_DUMP_THREAD_OFF_LR / 4] = lr;
  rec[CRASH_DUMP_THREAD_OFF_SP / 4] = sp;
  rec[CRASH_DUMP_THREAD_OFF_STACK_START / 4] = slot ? slot->lo : 0;
  rec[CRASH_DUMP_THREAD_OFF_STACK_END / 4] = slot ? slot->hi : 0;
  rec[CRASH_DUMP_THREAD_OFF_STATE / 4] = (slot && slot->t) ? (uint32_t)slot->t->state : 0xFFFFFFFF;
  rec[CRASH_DUMP_THREAD_OFF_SNAPSHOT_BYTES / 4] = snap;
  rec[CRASH_DUMP_THREAD_OFF_FLAGS / 4] = (sp_valid ? CRASH_DUMP_THREAD_FLAG_SP_VALID : 0) |
                                         (is_current ? CRASH_DUMP_THREAD_FLAG_CURRENT : 0);
  const uint32_t *src = (const uint32_t *)sp;
  uint32_t *dst = rec + CRASH_DUMP_THREAD_OFF_SNAPSHOT / 4;
  for (uint32_t i = 0; i < snap / 4; i++)
  {
    dst[i] = src[i];
  }
  (*records_left)--;
  *rec_offset = offset;
  return CRASH_DUMP_THREAD_RECORD_HEADER_SIZE + snap;
}

// Write staged bytes into the record at `offset`, in
// CRASH_DUMP_WRITE_CHUNK_SIZE pieces (same granularity as the v2 writer).
static void WriteStaged(uint32_t offset, uint32_t len)
{
  uint32_t done = 0;
  while (done < len)
  {
    uint32_t n = len - done;
    if (n > CRASH_DUMP_WRITE_CHUNK_SIZE)
    {
      n = CRASH_DUMP_WRITE_CHUNK_SIZE;
    }
    Card_WriteAndVerifyEeprom(CRASH_DUMP_EEPROM_BASE + offset + done,
                              (uint8_t *)crash_dump_record_buffer + done, n);
    done += n;
  }
}

// Capture all thread states and write the record to the backup EEPROM.
// Note: the scheduler is deliberately left enabled. The synchronous card
// write (Cardi_RequestStreamCommand -> Cardi_RequestStreamCommandCore)
// spin-waits with OSi_RescheduleThread and the actual EEPROM programming
// runs on the card thread, so disabling the scheduler here would hang the
// write before a single byte is written. The lock is reentrant for the
// owning thread, so a crash while the mod's own save code holds it is safe.
// The original hook path halts the game regardless, so the state left here
// does not matter afterwards.
static void CrashDumpEmit(uint32_t hook_id, const uint32_t *regs,
                          uint32_t sp_base, uint32_t trigger_buttons)
{
  int current_index;
  uint32_t n_slots = CollectThreadSlots(sp_base, &current_index);
  uint32_t total = n_slots + ((current_index < 0) ? 1 : 0);

  int lock_id = GetEepromLockId();
  Card_LockBackup(lock_id);
  // Clear the complete flag first: an interrupted write of this record must
  // not be mistaken for a complete one (a previous dump may have left a 1).
  uint32_t flag = 0;
  Card_WriteAndVerifyEeprom(CRASH_DUMP_EEPROM_BASE + CRASH_DUMP_OFF_COMPLETE, &flag, 4);

  StageHeader(hook_id, regs, trigger_buttons, total);
  WriteStaged(0, CRASH_DUMP_HEADER_SIZE);

  uint32_t records_left = total;
  uint32_t offset = CRASH_DUMP_HEADER_SIZE;
  uint32_t rec_offset;
  // Record 0 is always the current thread; the hook regs hold its live state.
  struct crash_thread_slot *current_slot =
      (current_index >= 0) ? &crash_thread_slots[current_index] : NULL;
  uint32_t size = StageThreadRecord(offset, &records_left, current_slot, sp_base,
                                    regs[CRASH_DUMP_REG_PC], regs[CRASH_DUMP_REG_LR],
                                    true, &rec_offset);
  if (size != 0)
  {
    WriteStaged(rec_offset, size);
    offset = rec_offset + size;
  }
  for (uint32_t i = 0; i < n_slots; i++)
  {
    if ((int)i == current_index)
    {
      continue;
    }
    struct crash_thread_slot *slot = &crash_thread_slots[i];
    // os_context::function_address_plus_4 holds the resume address + 4
    // (verified: OS_LoadContext resumes with `subs pc, lr, #4`, and
    // OS_InitContext stores entry + 4), so subtract 4 to get the real pc.
    // Re-read the context here, just before the snapshot is copied: the
    // thread may have run during the earlier card writes.
    uint32_t pc = ((uint32_t)slot->t->context.function_address_plus_4) - 4;
    uint32_t lr = (uint32_t)slot->t->context.exit_function;
    uint32_t sp = (uint32_t)slot->t->context.usable_stack_pointer;
    size = StageThreadRecord(offset, &records_left, slot, sp, pc, lr, false, &rec_offset);
    if (size == 0)
    {
      break;
    }
    WriteStaged(rec_offset, size);
    offset = rec_offset + size;
  }

  // The record is only complete once this lands.
  flag = 1;
  Card_WriteAndVerifyEeprom(CRASH_DUMP_EEPROM_BASE + CRASH_DUMP_OFF_COMPLETE, &flag, 4);
  Card_UnlockBackup(lock_id);
}

void CrashDumpWrite(uint32_t hook_id, const uint32_t *regs, uint32_t sp_base)
{
  if (crash_dump_active)
  {
    return;
  }
  crash_dump_active = 1;

  // The card driver's synchronous write spin-waits while yielding and the
  // EEPROM programming is performed by the card thread, so the dump relies on
  // normal thread semantics. Skip it when the crash happened in IRQ/FIQ mode;
  // the stub still continues into the original halt path.
  uint32_t mode = regs[CRASH_DUMP_REG_CPSR] & 0x1F;
  if (mode == 0x12 || mode == 0x11)
  {
    return;
  }

  CrashDumpEmit(hook_id, regs, sp_base, 0);
}

void CrashDumpTrigger(void)
{
  if (crash_dump_active)
  {
    return;
  }

  // The manual trigger records the live pc/lr/sp/cpsr of the watchdog thread;
  // r0..r12 are not meaningful here (the interesting registers are the ones
  // saved in each thread's os_context) and are left as 0.
  uint32_t regs[CRASH_DUMP_REG_COUNT];
  for (uint32_t i = 0; i < CRASH_DUMP_REG_COUNT; i++)
  {
    regs[i] = 0;
  }
  uint32_t sp_val, lr_val, cpsr_val;
  asm volatile(
      "mov %0, sp\n\t"
      "mov %1, lr\n\t"
      "mrs %2, cpsr"
      : "=&r"(sp_val), "=&r"(lr_val), "=&r"(cpsr_val));
  regs[CRASH_DUMP_REG_PC] = (uint32_t)&CrashDumpTrigger;
  regs[CRASH_DUMP_REG_LR] = lr_val;
  regs[CRASH_DUMP_REG_SP] = sp_val;
  regs[CRASH_DUMP_REG_CPSR] = cpsr_val;

  // Keep the raw button bitfield in the record (struct held_buttons layout:
  // a,b,select,start,right,left,up/down,r,l,x,y = bits 0..11).
  struct held_buttons held_buttons;
  GetHeldButtons(0, (void *)&held_buttons);
  uint8_t raw_buttons[2];
  memcpy(raw_buttons, &held_buttons, 2);

  crash_dump_active = 1;
  CrashDumpEmit(CRASH_DUMP_HOOK_MANUAL, regs, sp_val,
                (uint32_t)raw_buttons[0] | ((uint32_t)raw_buttons[1] << 8));
}

// Watchdog: polls every frame (it sleeps on NULL and is woken from the VCount
// 0 routine like the other mod threads) and dumps all thread stacks when the
// combo is held. This turns hangs (infinite loops, deadlocks) that never
// reach FatalError/OS_Panic into a diagnosable record. The game is left
// running afterwards; crash_dump_active makes the combo fire only once.
void CrashDumpWatchdogRoutine(void *arg)
{
  while (true)
  {
    struct held_buttons held_buttons;
    GetHeldButtons(0, (void *)&held_buttons);
    if (held_buttons.l && held_buttons.r && held_buttons.x && held_buttons.y)
    {
      CrashDumpTrigger();
    }
    OS_SleepThread(NULL);
  }
}