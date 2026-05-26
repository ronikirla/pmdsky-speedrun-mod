#include <pmdsky.h>
#include "eeprom.h"
#include "custom_headers.h"
#include "speedrun_hud.h"
#include "optimizations.h"

struct eeprom_timer eeprom_timer;
struct eeprom_configurations eeprom_configurations;
#define EEPROM_TIMER_BASE_ADDRESS 0xb65c
#define EEPROM_CONFIGURATIONS_BASE_ADDRESS 0xb670

bool is_saving = false;
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
    if (eeprom_timer.index == 0xFF)
    {
        goto CLEANUP;
    }
    // Read Configurations
    Card_ReadEeprom(EEPROM_CONFIGURATIONS_BASE_ADDRESS, &eeprom_configurations, 8);

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

    // Need to update the HUD slot that corresponds to the currently active mode here too, or else it won't graphically show up
    AssignHUDSlots();

    // Do not load the time if there is no time in the save data (aka deleted save)
    if (PLAY_TIME_SECONDS == 0) {
        goto CLEANUP;
    }

    // Read IGT
    int eeprom_offset = 0x1 + eeprom_timer.index * 0x5;
    Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[eeprom_timer.index], 5);

    struct play_time *igt = (struct play_time *)&PLAY_TIME_SECONDS;
    igt->seconds = eeprom_timer.redundant_timers[eeprom_timer.index].seconds;
    igt->frames = eeprom_timer.redundant_timers[eeprom_timer.index].frames;

CLEANUP:
    Card_UnlockBackup(eeprom_lock_id);
    igt_loaded = true;
}

void CustomPlayTimerTick(struct play_time *param_1)

{
    char bVar1;

    bVar1 = param_1->frames + 1;
    param_1->frames = bVar1;
    if (0x3b < bVar1)
    {
        param_1->frames = '\0';
        if (param_1->seconds < MAX_PLAY_TIME)
        {
            param_1->seconds = param_1->seconds + 1;
        }
        return;
    }
    return;
}

void AddTimePenalty(struct play_time *play_time, int additional_frames)
{
    int total_frames = play_time->frames + additional_frames;
    play_time->seconds += total_frames / 60;
    play_time->frames = (uint8_t)(total_frames % 60);
    return;
}

__attribute__((used)) int HijackNoteLoadBaseAndLoadIGT(void)
{
    igt_loaded = false;
    int res = NoteLoadBase();
    LoadIGTAndConfigurations();
    return res;
}

__attribute__((used)) void *HijackNoteSaveBaseAndSetSaveVariable(void)
{
    is_saving = true;
    void *res = MemAlloc(0xB65C, 5);
    return res;
}

__attribute__((used)) void HijackNoteSaveBaseAndUnsetSaveVariableAndAlsoAddTimePenaltyByTheWay(void *whatever_this_was)
{
    is_saving = false;

    struct play_time *igt = (struct play_time *)&PLAY_TIME_SECONDS;
    AddTimePenalty(igt, 200);
    MemFree(whatever_this_was);
    return;
}

__attribute__((used)) void CheckIfShouldIncrementPlayTimer(void *param_1)
{
    if (!is_saving)
    {
        CustomPlayTimerTick(param_1);
    }
    return;
}
