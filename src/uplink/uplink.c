#include "uplink.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "dspico_card.h"
#include "uplink_sampler.h"
#include "uplink_dbg.h"

extern void dspico_dcd_poll(void); // dcd_dspico.c

static bool s_inited = false;
static bool s_sampling = false;

//--------------------------------------------------------------------+
// USB descriptor callbacks (TinyUSB device)
//--------------------------------------------------------------------+

uint8_t const* tud_descriptor_device_cb(void) {
  uplink_dbg_log_raw("DESC device %uB", (uint32_t)UPLINK_DEV_DESC_LEN);
  return (uint8_t const*)uplink_descriptor_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  uplink_dbg_log_raw("DESC config %uB", (uint32_t)UPLINK_CFG_DESC_LEN);
  return (uint8_t const*)uplink_descriptor_configuration;
}

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
      uplink_dbg_log("CMD start");
      break;
    case 0x02: // STOP
      s_sampling = false;
      uplink_sampler_discard_block();
      uplink_dbg_log("CMD stop");
      break;
    case 0x03: // SET_ADDR idx u32
      if (len >= 5) {
        uplink_dbg_log("CMD set %u %08x", (uint32_t)buf[1], uplink_le32(buf + 2));
        uplink_sampler_set_addr(buf[1], uplink_le32(buf + 2));
      }
      break;
    case 0x04: { // PING
      uplink_dbg_log("CMD ping");
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
      tud_cdc_write((uint8_t*)msg, n);
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

// TinyUSB application callback (weak symbol in cdc_device.h)
void tud_cdc_rx_cb(uint8_t itf) {
  uint8_t buf[64];
  for (;;) {
    uint32_t n = tud_cdc_n_read(itf, buf, sizeof(buf));
    if (n == 0) {
      return;
    }
    for (uint32_t i = 0; i < n; i++) {
      if (s_cmd_len >= sizeof(s_cmd)) {
        uplink_cmd_drop_first(); // overflow; drop the oldest byte
      }
      s_cmd[s_cmd_len++] = buf[i];
    }
    uplink_cmd_parse();
  }
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void UplinkInit(void) {
  if (s_inited) {
    return;
  }
  uplink_dbg_init();
  uplink_sampler_init();

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
  uplink_dbg_log("uplink init done");

  s_sampling = true; // auto-start; host can pause with 0x02
  s_inited = true;
}

void UplinkPoll(void) {
  if (!s_inited) {
    return;
  }

  // CDC connection edge (enumeration success shows up here)
  {
    static bool s_cdc_conn = false;
    bool conn = tud_cdc_connected();
    if (conn != s_cdc_conn) {
      s_cdc_conn = conn;
      uplink_dbg_log("CDC %s", conn ? "connected" : "disconnected");
    }
  }

  // 1. Drain DSpico events (card transactions, each inside the game lock)
  dspico_dcd_poll();

  // 2. Run the TinyUSB device task. This may issue more card transactions
  //    (edpt_open / edpt_xfer / stall) and invoke the CDC callbacks.
  tud_task();

  // 2b. Persistent status line (~500 ms):
  //     r=tud_ready d=DTR(CDC line state) m=tud_mounted s=tud_suspended.
  //     The debug console suppresses identical repeats, so this stays as
  //     one steady line whose text only changes when the state does.
  {
    static uint32_t s_last_status_ms = 0;
    uint32_t now = uplink_dbg_ms();
    if (now - s_last_status_ms >= 500) {
      s_last_status_ms = now;
      uplink_dbg_log("st r=%u d=%u m=%u s=%u",
                     (uint32_t)tud_ready(), (uint32_t)tud_cdc_connected(),
                     (uint32_t)tud_mounted(), (uint32_t)tud_suspended());
    }
  }

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
          static uint32_t s_last_drop_logged = 0;
          if (uplink_frames_dropped - s_last_drop_logged >= 9) {
            s_last_drop_logged = uplink_frames_dropped;
            if (tud_cdc_connected()) {
              uplink_dbg_log("TX full, +%u drops", uplink_frames_dropped - s_last_drop_logged);
            } else {
              uplink_dbg_log("drop: CDC not connected");
            }
          }
        }
      }
    }
  }
}

