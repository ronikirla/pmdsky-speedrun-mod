#include <pmdsky.h>
#include "eeprom.h"
#include "custom_headers.h"
#include "speedrun_hud.h"
#include "optimizations.h"

struct eeprom_timer eeprom_timer;
struct eeprom_configurations eeprom_configurations;
#define EEPROM_TIMER_BASE_ADDRESS 0xb65c
#define EEPROM_CONFIGURATIONS_BASE_ADDRESS 0xb670

bool igt_loaded = false;
int eeprom_lock_id;

void SaveIGT(void)
{
    if (!igt_loaded)
    {
        return;
    }
    int eeprom_offset = 0x0;
    int new_index = 0;
    if (eeprom_timer.index == 0)
    {
        new_index = 1;
        eeprom_offset = 0x6;
    }
    else
    {
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

void SaveConfigurations(void)
{
    eeprom_configurations.SRAM_hud_display_mode = hud_display_mode;
    eeprom_configurations.SRAM_optimization_mode = optimization_mode;

    eeprom_lock_id = OS_GetLockID();

    Card_LockBackup(eeprom_lock_id);
    Card_WriteAndVerifyEeprom(EEPROM_CONFIGURATIONS_BASE_ADDRESS, &eeprom_configurations, 8);
    Card_UnlockBackup(eeprom_lock_id);
}

void LoadIGTAndConfigurations(void)
{
    eeprom_lock_id = OS_GetLockID();

    Card_LockBackup(eeprom_lock_id);
    // Read index
    Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
    // Read IGT
    int eeprom_offset = 0x1 + eeprom_timer.index * 0x5;
    Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[eeprom_timer.index], 5);
    // Read Configurations
    Card_ReadEeprom(EEPROM_CONFIGURATIONS_BASE_ADDRESS, &eeprom_configurations, 8);

    Card_UnlockBackup(eeprom_lock_id);

    struct play_time *igt = (struct play_time *)&PLAY_TIME_SECONDS;
    igt->seconds = eeprom_timer.redundant_timers[eeprom_timer.index].seconds;
    igt->frames = eeprom_timer.redundant_timers[eeprom_timer.index].frames;

    if (eeprom_configurations.SRAM_hud_display_mode < 0 || eeprom_configurations.SRAM_hud_display_mode >= HUD_DISPLAY_COUNT)
    {
        eeprom_configurations.SRAM_hud_display_mode = HUD_DISPLAY_NONE;
    }
    if (eeprom_configurations.SRAM_optimization_mode < 0 || eeprom_configurations.SRAM_optimization_mode >= OPTIMIZATION_MODE_COUNT)
    {
        eeprom_configurations.SRAM_optimization_mode = OPTIMIZATION_MODE_DEFAULT;
    }
    hud_display_mode = eeprom_configurations.SRAM_hud_display_mode;
    optimization_mode = eeprom_configurations.SRAM_optimization_mode;

    // Need to update the HUD slot that corresponds to the currently active mode here too, or else it won't grasphically show up
    AssignHUDSlots();

    igt_loaded = true;
}

__attribute__((used)) int HijackNoteLoadBaseAndLoadIGT(void)
{
    int res = NoteLoadBase();
    LoadIGTAndConfigurations();
    return res;
}

// TODO deal with no save file. also if you import a vanilla save its gonna have a billion igt