// Implements soft reset functionality via L+R+Start+Select button combo

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "eeprom.h"
#include "fixed_rng.h"

void HandleSoftReset(void) {
  struct held_buttons held_buttons;
  GetHeldButtons(0, (void*) &held_buttons);

  if (held_buttons.l && held_buttons.r && held_buttons.start && held_buttons.select) {
      if (IsFixedRNG()) {
        SaveRNGSeedForSoftReset();
      }
      OS_ResetSystem();
    }
}
