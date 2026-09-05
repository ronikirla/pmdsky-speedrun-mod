// Implements soft reset functionality via L+R+Start+Select button combo

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "eeprom.h"
#include "fixed_rng.h"
#include "uplink.h"

void HandleSoftReset(void) {
  struct held_buttons held_buttons;
  GetHeldButtons(0, (void*) &held_buttons);

  if (held_buttons.l && held_buttons.r && held_buttons.start && held_buttons.select) {
      if (IsFixedRNG()) {
        SaveRNGSeedForSoftReset();
      }
      // If the USB uplink was active, disable it completely before the
      // reset. UplinkDeinit() alone would leave uplink_enabled set; if that
      // static survives OS_ResetSystem the uplink would re-initialize during
      // the new boot -- racing the boot-time DLDI card traffic (the DLDI does
      // not take the card lock) and tripping the DSpico firmware's "USB ->
      // console reset -> USB again" crash bug. Disabling also restores the
      // intended per-session behavior: the uplink is off after every soft
      // reset and must be toggled back on from the main menu. (A full power
      // cycle of the DS remains the fallback if re-init is ever flaky.)
      UplinkSetEnabled(false);
      OS_ResetSystem();
    }
}
