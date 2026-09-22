// PMDSky uplink: periodic ARM9 memory sampling streamed to a PC over a
// DSpico-backed USB CDC serial port.
//
// Integration:
//   - UplinkInit()  called from the mod's InitThreads (once at boot)
//   - UplinkPoll()  called from the mod's MainRoutine (~60 Hz, lowest-prio)
#pragma once

// Bring up the DSpico USB stack and start sampling. Safe to call once.
void UplinkInit(void);

// One poll iteration: drain DSpico events, run the TinyUSB task, sample.
// No-op until UplinkInit() has run.
void UplinkPoll(void);
