#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"

__attribute__((used)) uint8_t PlayTimerTickAndWaitTillVBlank(void) {
    // Magic number for whether we are in the top menu of the main menu.
    // If not, then advance the play timer. Otherwise pause (normal behaviour)
    int* main_menu_magic = (int*) 0x22a3670;
    if (*main_menu_magic != 0x22a3e94 && PLAY_TIME_SECONDS != 0) {
        PlayTimerTick((struct play_time*) &PLAY_TIME_SECONDS);
    }
    return WaitTillVBlank();
}