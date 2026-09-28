#include "uplink.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "dspico_card.h"
#include "uplink_sampler.h"

extern void dspico_dcd_poll(void); // dcd_dspico.c

static bool s_inited = false;
static bool s_sampling = false;

//--------------------------------------------------------------------+
// USB descriptor callbacks (TinyUSB device)
//--------------------------------------------------------------------+

uint8_t const* tud_descriptor_device_cb(void) {
  return (uint8_t const*)uplink_descriptor_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return (uint8_t const*)uplink_descriptor_configuration;
}

//--------------------------------------------------------------------+
// Local CDC mode (UPLINK_LOCAL_CDC): the TinyUSB device stack runs in the
// DSpico firmware (usb_cdc_bridge.c). The NDS never calls into TinyUSB;
// it ships sample blocks over WRITE_DATA (0xE9) and polls a status block
// plus host RX over READ_DATA (0xEA).
//--------------------------------------------------------------------+

#if UPLINK_LOCAL_CDC

// 0xEA endpoint field selects the returned 512-byte block content:
#define UPLINK_LOCAL_STATUS_EP  0
#define UPLINK_LOCAL_RX_EP      1
// 0xE9 endpoint field (ignored by the firmware, kept meaningful for logs)
#define UPLINK_LOCAL_TX_EP      0x82

// Cached firmware status (refreshed every UPLINK_LOCAL_POLL_EVERY frames)
typedef struct {
  bool valid;
  bool configured;      // SET_CONFIGURATION accepted (tud_mounted on DSpico)
  bool cdc_connected;   // CDC data interface open
  bool dtr;
  bool rts;
  uint32_t tx_bytes;    // bytes pumped to the host
  uint32_t tx_drops;    // bytes dropped (firmware TX ring overflow)
  uint32_t rx_pending;  // host bytes waiting in the firmware RX ring
} uplink_local_status_t;

static uplink_local_status_t s_local;
static uint8_t s_rx_buf[512];

// Host replies (e.g. PING) staged here and shipped on the next poll
static uint8_t s_tx_stage[512];
static uint32_t s_tx_stage_len = 0;

// Poll cadence: status/RX refresh every ~15 frames (~250 ms at 60 Hz)
#define UPLINK_LOCAL_POLL_EVERY 15

static uint32_t uplink_local_le32(uint8_t const* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Locked single card transaction, no data phase
static void uplink_local_send_cmd(uint64_t command) {
  uint16_t lock = dspico_card_lock_wait();
  dspico_card_set_cmd(command);
  dspico_card_start_xfer(DSPICO_USB_DEFAULT_COMMAND_SETTINGS, false);
  dspico_card_wait_busy();
  dspico_card_lock_release(lock);
}

// Ship up to 512 valid bytes to the firmware CDC TX ring (the rest of the
// 512-byte phase is zero-padded by dspico_card_cpu_write).
static void uplink_local_write_block(const uint8_t* src, uint32_t valid) {
  if (valid > 512) {
    valid = 512;
  }
  uint16_t lock = dspico_card_lock_wait();
  dspico_card_set_cmd(DSPICO_CMD_USB_WRITE_DATA(0, UPLINK_LOCAL_TX_EP, 1, valid));
  dspico_card_start_xfer(DSPICO_USB_WRITE_DATA_SETTINGS, false);
  dspico_card_cpu_write(src, 512 / 4, valid);
  dspico_card_lock_release(lock);
}

// Read a 512-byte block (status or host RX) from the firmware
static uint32_t uplink_local_read_block(uint8_t* dst, uint32_t endpoint) {
  uint16_t lock = dspico_card_lock_wait();
  dspico_card_set_cmd(DSPICO_CMD_USB_READ_DATA(0, endpoint));
  dspico_card_start_xfer(DSPICO_USB_READ_DATA_SETTINGS, false);
  dspico_card_cpu_read(dst, 512 / 4);
  dspico_card_lock_release(lock);
  return 512;
}

// Refresh s_local from the firmware status block; returns true on success
static bool uplink_local_poll_status(void) {
  uint8_t block[512];
  if (uplink_local_read_block(block, UPLINK_LOCAL_STATUS_EP) != 512) {
    return false;
  }
  uint32_t w0 = uplink_local_le32(block + 0);
  s_local.valid = true;
  s_local.configured = (w0 & 2u) != 0;
  s_local.cdc_connected = (w0 & 4u) != 0;
  s_local.dtr = (w0 & 8u) != 0;
  s_local.rts = (w0 & 16u) != 0;
  s_local.tx_bytes = uplink_local_le32(block + 4);
  s_local.tx_drops = uplink_local_le32(block + 8);
  s_local.rx_pending = uplink_local_le32(block + 12);
  return true;
}

// Send host-bound data (command replies). Staged and shipped over
// WRITE_DATA on the next UplinkPoll iteration.
static void uplink_send_host_reply(const uint8_t* data, uint32_t len) {
  if (s_tx_stage_len + len > sizeof(s_tx_stage)) {
    uplink_local_write_block(s_tx_stage, s_tx_stage_len); // flush, no drops (replies are small)
    s_tx_stage_len = 0;
  }
  memcpy(s_tx_stage + s_tx_stage_len, data, len);
  s_tx_stage_len += len;
}

#endif // UPLINK_LOCAL_CDC

//--------------------------------------------------------------------+
// Host command protocol (PC -> NDS over CDC):
//   [0x01]           start sampling
//   [0x02]           stop sampling
//   [0x03 idx u32]   retarget sample slot `idx` to address (0 disables)
//   [0x04]           ping -> "UPLINK seq=<n> sent=<n> drop=<n>\r\n"
//--------------------------------------------------------------------+

static uint32_t uplink_le32(uint8_t const* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Minimal decimal formatting (no libc on the NDS)
static uint8_t uplink_utoa(uint32_t v, char* out) {
  char tmp[12];
  uint8_t n = 0;
  if (v == 0) {
    out[0] = '0';
    return 1;
  }
  while (v > 0) {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  for (uint8_t i = 0; i < n; i++) {
    out[i] = tmp[n - 1 - i];
  }
  return n;
}

static void uplink_handle_host_command(uint8_t const* buf, uint16_t len) {
  if (len == 0) {
    return;
  }
  switch (buf[0]) {
    case 0x01: // START
      s_sampling = true;
      break;
    case 0x02: // STOP
      s_sampling = false;
      uplink_sampler_discard_block();
      break;
    case 0x03: // SET_ADDR idx u32
      if (len >= 5) {
        uplink_sampler_set_addr(buf[1], uplink_le32(buf + 2));
      }
      break;
    case 0x04: { // PING
      char msg[64];
      uint8_t* p = (uint8_t*)msg;
      uint8_t n = 0;
      const char* prefix = "UPLINK seq=";
      while (*prefix) p[n++] = (uint8_t)*prefix++;
      n += uplink_utoa(uplink_sampler_get_seq(), p + n);
      const char* mid = " sent=";
      while (*mid) p[n++] = (uint8_t)*mid++;
      n += uplink_utoa(uplink_frames_sent, p + n);
      const char* tail = " drop=";
      while (*tail) p[n++] = (uint8_t)*tail++;
      n += uplink_utoa(uplink_frames_dropped, p + n);
      p[n++] = '\r';
      p[n++] = '\n';
#if UPLINK_LOCAL_CDC
      uplink_send_host_reply((uint8_t*)msg, n);
#else
      tud_cdc_write((uint8_t*)msg, n);
#endif
      break;
    }
    default:
      break;
  }
}

// CDC receive: in this TinyUSB (0.17, LNH fork) the class driver copies OUT
// packets into a FIFO and invokes tud_cdc_rx_cb(itf); the app drains it with
// tud_cdc_n_read(). Commands are parsed from a byte stream (two commands may
// share one USB packet), so bytes are accumulated and complete commands are
// emitted as they become available.
static uint8_t s_cmd[6]; // longest command: SET_ADDR (0x03 idx u32) = 6 bytes
static uint8_t s_cmd_len = 0;

static void uplink_cmd_drop_first(void) {
  for (uint8_t i = 0; i + 1 < s_cmd_len; i++) {
    s_cmd[i] = s_cmd[i + 1];
  }
  s_cmd_len--;
}

static void uplink_cmd_parse(void) {
  for (;;) {
    if (s_cmd_len == 0) {
      return;
    }
    uint8_t need = 0;
    switch (s_cmd[0]) {
      case 0x01:
      case 0x02:
      case 0x04:
        need = 1;
        break;
      case 0x03:
        need = 6;
        break;
      default:
        uplink_cmd_drop_first(); // unknown byte; resynchronize
        continue;
    }
    if (s_cmd_len < need) {
      return; // incomplete command; wait for more bytes
    }
    uplink_handle_host_command(s_cmd, need);
    for (uint8_t i = 0; i + need < s_cmd_len; i++) {
      s_cmd[i] = s_cmd[i + need];
    }
    s_cmd_len -= need;
  }
}

// Feed host bytes into the command stream parser (commands are executed
// as their bytes complete, regardless of USB packet boundaries).
static void uplink_cmd_feed(const uint8_t* data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    if (s_cmd_len >= sizeof(s_cmd)) {
      uplink_cmd_drop_first(); // overflow; drop the oldest byte
    }
    s_cmd[s_cmd_len++] = data[i];
  }
  uplink_cmd_parse();
}

// TinyUSB application callback (weak symbol in cdc_device.h); legacy path.
void tud_cdc_rx_cb(uint8_t itf) {
  uint8_t buf[64];
  for (;;) {
    uint32_t n = tud_cdc_n_read(itf, buf, sizeof(buf));
    if (n == 0) {
      return;
    }
    uplink_cmd_feed(buf, n);
  }
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void UplinkInit(void) {
  if (s_inited) {
    return;
  }
  uplink_sampler_init();

#if UPLINK_LOCAL_CDC
  // Hand USB enumeration over to the DSpico firmware: its local TinyUSB
  // stack (usb_cdc_bridge.c) initializes the RP2040 SIE and pulls up D+.
  // The NDS keeps no TinyUSB state; UplinkPoll() ships sample blocks and
  // polls the firmware status block.
  uplink_local_send_cmd(DSPICO_CMD_USB_COMMAND_LOCAL_STACK);
#else
  tusb_rhport_init_t init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_AUTO,
  };
  // tusb_init -> usbd_init -> dcd_init (DSpico INIT command). This
  // TinyUSB fork never connects on its own (dcd_connect() is only
  // reachable via tud_connect()), so assert the DSpico's D+ pull-up
  // explicitly - without it the host can never detect the device.
  tusb_init(0, &init);
  tud_connect();
#endif

  s_sampling = true; // auto-start; host can pause with 0x02
  s_inited = true;
}

void UplinkPoll(void) {
  if (!s_inited) {
    return;
  }

#if UPLINK_LOCAL_CDC
  // 1. Flush staged host replies (PING)
  if (s_tx_stage_len > 0) {
    uplink_local_write_block(s_tx_stage, s_tx_stage_len);
    s_tx_stage_len = 0;
  }

  // 2. Sample + stream: one 512-byte card transaction per full block
  //    (9 frames); the firmware pumps the bytes to the host at USB rate,
  //    decoupled from the game frame timing.
  if (s_sampling) {
    uplink_sampler_tick();
    if (uplink_sampler_block_full()) {
      uint8_t stage[512];
      uint32_t len = uplink_sampler_flush_block(stage);
      if (len > 0) {
        uplink_local_write_block(stage, len);
        uplink_frames_sent += len / UPLINK_FRAME_LEN;
      }
    }
  }

  // 3. Firmware status + host RX, every ~15 frames (~250 ms)
  {
    static uint32_t s_poll_cnt = 0;
    if (++s_poll_cnt >= UPLINK_LOCAL_POLL_EVERY) {
      s_poll_cnt = 0;
      if (uplink_local_poll_status()) {
        if (s_local.rx_pending > 0) {
          uint32_t n = uplink_local_read_block(s_rx_buf, UPLINK_LOCAL_RX_EP);
          uplink_cmd_feed(s_rx_buf, n);
        }
      }
    }
  }
#else
  // 1. Drain DSpico events (card transactions, each inside the game lock)
  dspico_dcd_poll();

  // 2. Run the TinyUSB device task. This may issue more card transactions
  //    (edpt_open / edpt_xfer / stall) and invoke the CDC callbacks.
  tud_task();

  // 3. Sample + stream
  if (s_sampling) {
    uplink_sampler_tick();
    if (uplink_sampler_block_full()) {
      uint8_t stage[512];
      uint32_t len = uplink_sampler_flush_block(stage);
      if (len > 0) {
        if (tud_cdc_connected() && tud_cdc_write(stage, len) == len) {
          tud_cdc_write_flush();
          uplink_frames_sent += len / UPLINK_FRAME_LEN;
        } else {
          uplink_frames_dropped += len / UPLINK_FRAME_LEN;
        }
      }
    }
  }
#endif
}

void UplinkShutdown(void) {
  if (!s_inited) return;
  s_inited = false;    // UplinkPoll becomes a no-op
  s_sampling = false;
#if UPLINK_DISCONNECT_ON_RESET
  uplink_local_send_cmd(DSPICO_CMD_USB_COMMAND_DISCONNECT);
#endif
}


