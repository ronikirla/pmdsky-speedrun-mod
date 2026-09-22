// TinyUSB configuration for the PMDSky uplink.
// TinyUSB is vendored at commit 8eeddaab364e413153ebd0a8302f85dfb2e60e9f
// (0.17.0-242-g8eeddaab3), the exact gitlink used by LNH-team/dspico-usb-examples.
#pragma once

// Custom platform: the DSpico DCD (dcd_dspico.c) implements the full DCD API
// over the NDS card controller. No MCU-specific TinyUSB port is involved.
#define CFG_TUSB_MCU          OPT_MCU_NONE
#define TUP_DCD_ENDPOINT_MAX  16

// No RTOS inside TinyUSB: the mod's MainRoutine thread calls tud_task() at
// ~60 Hz and drives all DSpico event draining itself.
#define CFG_TUSB_OS           OPT_OS_NONE

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Endpoint memory placement: plain SRAM, 4-byte aligned (CPU data path, no DMA)
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

//--------------------------------------------------------------------+
// DEVICE
//--------------------------------------------------------------------+

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED

#define CFG_TUSB_RHPORT0_MODE OPT_MODE_FULL_SPEED

// EP0 + CDC: data OUT (EP1), data IN (EP2), notification IN (EP3)
#define CFG_TUD_MAX_ENDPOINTS 4

#define CFG_TUD_ENDPOINT0_SIZE 64

//------------- CLASS -------------//
#define CFG_TUD_CDC           1
#define CFG_TUD_MSC           0
#define CFG_TUD_HID           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0

// The DSpico exchanges 512-byte card data blocks. A 512-byte CDC buffer lets
// one dcd_edpt_xfer cover a whole block (the DSpico packetizes into 64-byte
// full-speed USB transfers on its side). EP_BUFSIZE sizes the endpoint
// buffers; RX/TX_BUFSIZE size the CDC class FIFOs.
#define CFG_TUD_CDC_EP_BUFSIZE 512
#define CFG_TUD_CDC_RX_BUFSIZE 512
#define CFG_TUD_CDC_TX_BUFSIZE 512
