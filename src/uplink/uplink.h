// PMDSky uplink: periodic ARM9 memory sampling streamed to a PC over a
// DSpico-backed USB CDC serial port.
//
// The TinyUSB CDC-ACM device stack runs in the DSpico firmware
// (dspico-firmware/src/usb_cdc_bridge.c): enumeration and CDC pumping
// happen on the RP2040. The NDS only samples memory and ships 512-byte
// blocks over WRITE_DATA (0xE9), and polls status / host RX over
// READ_DATA (0xEA).
//
// Integration:
//   - UplinkInit()  called from the mod's InitThreads (once at boot)
//   - UplinkPoll()  called from the mod's MainRoutine (~60 Hz, lowest-prio)
#pragma once

// Hand USB enumeration over to the DSpico firmware and start sampling.
// Safe to call once.
void UplinkInit(void);

// One poll iteration: ship staged sample blocks, poll firmware status /
// host RX. No-op until UplinkInit() has run.
void UplinkPoll(void);