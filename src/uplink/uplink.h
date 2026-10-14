// PMDSky uplink: reader-driven memory sampling streamed to a PC over a
// DSpico-backed USB CDC serial port (protocol v2, docs/uplink-protocol.md).
//
// The TinyUSB CDC-ACM device stack runs in the DSpico firmware
// (dspico-firmware/src/usb_cdc_bridge.c): enumeration and CDC pumping
// happen on the RP2040. The NDS ships sample frames over WRITE_DATA (0xE9)
// and polls host commands over READ_DATA (0xEA) — the firmware's baseline
// RX path, no firmware changes required.
//
// Integration:
//   - UplinkInit()  called from the mod's InitThreads (once at boot)
//   - UplinkPoll()  called from the mod's MainRoutine (~60 Hz, lowest-prio)
#pragma once

// Hand USB enumeration over to the DSpico firmware and start polling.
// Safe to call once.
void UplinkInit(void);

// One poll iteration: stream one sample frame (when configured and
// started) and, every ~15 frames, read host commands from the firmware.
// No-op until UplinkInit() has run.
void UplinkPoll(void);
