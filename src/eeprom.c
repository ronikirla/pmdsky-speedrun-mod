#include <pmdsky.h>
#include "eeprom.h"
#include "custom_headers.h"

struct eeprom_timer timers_to_be_written;
// struct play_time_no_padding play_time_to_be_written;
const int SRAM_BASE_ADDRESS = 0xb65c;

// TODO: do this for both save file blocks
void SaveIGT(void)
{
    int SRAM_OFFSET = 0x0;
    int new_index = 0;
    if (timers_to_be_written.index == 0)
    {
        new_index = 1;
        SRAM_OFFSET = 0x6;
    }
    else
    {
        new_index = 0;
        SRAM_OFFSET = 0x1;
    }

    timers_to_be_written.index = new_index;
    timers_to_be_written.redundant_timers[new_index].seconds = PLAY_TIME_SECONDS;
    timers_to_be_written.redundant_timers[new_index].frames = PLAY_TIME_FRAME_COUNTER;

    // write IGT
    Card_WriteAndVerifyEeprom(SRAM_BASE_ADDRESS + SRAM_OFFSET, &timers_to_be_written.redundant_timers[new_index], 5);
    // write index
    Card_WriteAndVerifyEeprom(SRAM_BASE_ADDRESS, &timers_to_be_written.index, 1);
}