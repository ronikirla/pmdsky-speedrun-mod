#include <pmdsky.h>
#include "eeprom.h"
#include "custom_headers.h"

struct eeprom_timer eeprom_timer;
// struct play_time_no_padding play_time_to_be_written;
#define EEPROM_TIMER_BASE_ADDRESS 0xb65c
#define IGT_SAVE_COOLDOWN 60

int igt_save_cooldown = IGT_SAVE_COOLDOWN;
bool igt_loaded = false;

// TODO: do this for both save file blocks
void SaveIGT(void) {
    if (!igt_loaded) {
        return;
    }
    if (igt_save_cooldown > 0) {
        igt_save_cooldown--;
        //return;
    }
    igt_save_cooldown = IGT_SAVE_COOLDOWN;
    int eeprom_offset = 0x0;
    int new_index = 0;
    if (eeprom_timer.index == 0) {
        new_index = 1;
        eeprom_offset = 0x6;
    }
    else {
        new_index = 0;
        eeprom_offset = 0x1;
    }

    eeprom_timer.index = new_index;
    eeprom_timer.redundant_timers[new_index].seconds = PLAY_TIME_SECONDS;
    eeprom_timer.redundant_timers[new_index].frames = PLAY_TIME_FRAME_COUNTER;
    Debug_Print0("SAVE index: %d\nseconds 0: %d, frames 0: %d\nseconds 1: %d, frames 1: %d", eeprom_timer.index, eeprom_timer.redundant_timers[0].seconds, eeprom_timer.redundant_timers[0].frames, eeprom_timer.redundant_timers[1].seconds, eeprom_timer.redundant_timers[1].frames);

    // Write IGT
    Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[new_index], 5);
    // Write index
    Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
}

void LoadIGT(void) {
    // Read index
    Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
    // Read IGT
    int eeprom_offset = eeprom_timer.index * 0x6;
    Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[eeprom_timer.index], 5);
    Debug_Print0("LOAD index: %d\nseconds 0: %d, frames 0: %d\nseconds 1: %d, frames 1: %d", eeprom_timer.index, eeprom_timer.redundant_timers[0].seconds, eeprom_timer.redundant_timers[0].frames, eeprom_timer.redundant_timers[1].seconds, eeprom_timer.redundant_timers[1].frames);

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