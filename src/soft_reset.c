// Implements soft reset functionality via L+R+Start+Select button combo

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"

static bool prev_held_soft_reset = false;

void HandleSoftReset(void) {
    struct held_buttons held_buttons;
    GetHeldButtons(0, (void*) &held_buttons);

    if (held_buttons.l && held_buttons.r && held_buttons.start && held_buttons.select) {
        if (!prev_held_soft_reset) {
            prev_held_soft_reset = true;
            OS_ResetSystem();
        }
    } else {
        prev_held_soft_reset = false;
    }
}