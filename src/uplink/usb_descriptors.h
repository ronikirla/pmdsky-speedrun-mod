// USB descriptors for the PMDSky uplink CDC-ACM device.
#pragma once

#include <tusb.h>

#define UPLINK_VID          0x2020 // locally administered (DevKitPro-style)
#define UPLINK_PID          0xD801
#define UPLINK_BCD_USB      0x0110
#define UPLINK_EPNUM_NOTIF  0x83 // notification IN
#define UPLINK_EPNUM_DATA_OUT 0x01
#define UPLINK_EPNUM_DATA_IN  0x82
#define UPLINK_EPSIZE_NOTIF   16
#define UPLINK_EPSIZE_DATA    64 // full-speed max packet

// Descriptor lengths (also enforced by the compile-time checks in usb_descriptors.c)
#define UPLINK_DEV_DESC_LEN 18
#define UPLINK_CFG_DESC_LEN (9 + (9 + 5 + 5 + 4 + 5) + 7 + (9 + 7 + 7))

extern const uint8_t uplink_descriptor_device[];
extern const uint8_t uplink_descriptor_configuration[];
extern const uint16_t uplink_descriptor_string[];
