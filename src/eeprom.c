#include <pmdsky.h>
#include "eeprom.h"
#include "custom_headers.h"

struct eeprom_timer eeprom_timer;
#define EEPROM_TIMER_BASE_ADDRESS 0xb65c

bool igt_loaded = false;
int eeprom_lock_id;

void SaveIGT(void) {
    if (!igt_loaded) {
        return;
    }
    int eeprom_offset = 0x0;
    int new_index = 0;
    if (eeprom_timer.index == 0) {
        new_index = 1;
        eeprom_offset = 0x6;
    } else {
        new_index = 0;
        eeprom_offset = 0x1;
    }

    eeprom_timer.index = new_index;
    eeprom_timer.redundant_timers[new_index].seconds = PLAY_TIME_SECONDS;
    eeprom_timer.redundant_timers[new_index].frames = PLAY_TIME_FRAME_COUNTER;

    Card_LockBackup(eeprom_lock_id);
    // Write IGT
    Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[new_index], 5);
    // Write index
    Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
    Card_UnlockBackup(eeprom_lock_id);
}

void LoadIGT(void) {
    eeprom_lock_id = OS_GetLockID();

    Card_LockBackup(eeprom_lock_id);
    // Read index
    Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
    // Read IGT
    int eeprom_offset = 0x1 + eeprom_timer.index * 0x5;
    Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[eeprom_timer.index], 5);
    Card_UnlockBackup(eeprom_lock_id);

    struct play_time* igt = (struct play_time*) &PLAY_TIME_SECONDS;
    igt->seconds = eeprom_timer.redundant_timers[eeprom_timer.index].seconds;
    igt->frames = eeprom_timer.redundant_timers[eeprom_timer.index].frames;

    igt_loaded = true;
}

__attribute__((used)) int HijackNoteLoadBaseAndLoadIGT(void) {
    int res = NoteLoadBase();
    LoadIGT();
    return res;
}

// TODO deal with no save file. also if you import a vanilla save its gonna have a billion igt