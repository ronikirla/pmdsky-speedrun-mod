// PMDSky uplink: periodic ARM9 memory sampling streamed to a PC over a
// DSpico-backed USB CDC serial port.
//
// Integration:
//   - UplinkInit()  called from the mod's InitThreads (once at boot)
//   - UplinkPoll()  called from the mod's MainRoutine (~60 Hz, lowest-prio)
#pragma once

// Set to 1 to also tell the DSpico firmware to drop the USB link on reset.
#ifndef UPLINK_DISCONNECT_ON_RESET
#define UPLINK_DISCONNECT_ON_RESET 0
#endif

// UPLINK_LOCAL_CDC: 1 (default) -> the DSpico firmware runs the TinyUSB
// CDC-ACM device stack locally (dspico-firmware/src/usb_cdc_bridge.c):
// enumeration and CDC pumping happen on the RP2040, the NDS only samples
// memory and ships 512-byte blocks over WRITE_DATA (0xE9), and polls
// status / host RX over READ_DATA (0xEA).
// 0 -> legacy split-brain path: the NDS runs the TinyUSB device stack and
// forwards every DSpico event over the card bus.
#ifndef UPLINK_LOCAL_CDC
#define UPLINK_LOCAL_CDC 1
#endif

// Bring up the DSpico USB stack and start sampling. Safe to call once.
void UplinkInit(void);

// One poll iteration: drain DSpico events, run the TinyUSB task, sample.
// No-op until UplinkInit() has run.
void UplinkPoll(void);

// Stop all card activity. Call before OS_ResetSystem. Irreversible until reboot.
void UplinkShutdown(void);