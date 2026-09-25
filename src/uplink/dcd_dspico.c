// TinyUSB Device Controller Driver for the DSpico flashcard.
//
// Ported from LNH-team/dspico-usb-examples (platform/dcd_dspico.cpp,
// DSPicoUsbInEndpoint.cpp, DSPicoUsbOutEndpoint.cpp) with two deliberate
// changes:
//   1. CPU data path: the example streams 512-byte blocks through DMA3 in
//      card-request mode. The game's audio streaming also uses DMA3, so the
//      data phases here feed/drain REG_MCD1 from the CPU (libtwl's
//      card_romCpu* helpers).
//   2. Polling, no IRQ: the example runs a dedicated RTOS thread woken by
//      the card IRQ. Inside the running game the OS owns the IRQ vector
//      table, so we poll GET_EVENT at ~60 Hz instead and ignore the card
//      IRQ line. dcd_int_enable() still sends the INTERRUPT_ENABLE command
//      once (the DSpico firmware needs it to arm the USB event pipeline,
//      and TinyUSB's usbd_init() calls it at boot); dcd_int_disable()
//      stays a no-op so the pipeline is never disarmed.
//
// The DSpico exchanges data in 512-byte double-buffered card transactions.
// Every card operation (command, or 512-byte data phase) runs inside the
// game's card lock so the game's own card I/O is never disturbed.
#include "tusb.h"
#include "device/dcd.h"
#include "dspico_card.h"
#include "uplink_dbg.h"

#if CFG_TUD_ENABLED

//--------------------------------------------------------------------+
// Endpoint state (16 IN + 16 OUT, mirroring the examples)
//--------------------------------------------------------------------+

typedef struct {
  uint8_t ep;           // endpoint address (IN direction bit set, e.g. 0x80)
  uint8_t* buffer;      // TinyUSB endpoint buffer (>= 512 bytes)
  uint32_t length;      // total bytes for this transfer
  uint32_t offset;      // bytes handled so far
  uint32_t remaining;   // bytes not yet reported complete
  uint8_t dspico_buf;   // current DSpico double buffer (0/1)
  bool active;          // a transfer is in flight
} dspico_in_endpoint_t;

typedef struct {
  uint8_t ep;           // endpoint address (OUT: no direction bit)
  uint8_t* buffer;
  uint32_t length;
  uint32_t offset;
  uint32_t total_received;
  uint8_t dspico_buf;
  bool active;
} dspico_out_endpoint_t;

static dspico_in_endpoint_t s_in_ep[16];
static dspico_out_endpoint_t s_out_ep[16];

// MCCNT1 settings for GET_EVENT data phases (LEN_4 == 4-byte event word)
#define DSPICO_USB_EVENT_SETTINGS \
    (MCCNT1_DIR_READ | MCCNT1_RESET_OFF | MCCNT1_CLK_6_7_MHZ | MCCNT1_LEN_4 | MCCNT1_CMD_SCRAMBLE | \
     MCCNT1_LATENCY2(4) | MCCNT1_CLOCK_SCRAMBLER | MCCNT1_READ_DATA_DESCRAMBLE | MCCNT1_LATENCY1(0))

//--------------------------------------------------------------------+
// Command helper (locked single card transaction, no data phase)
//--------------------------------------------------------------------+

static void send_command(uint64_t command) {
  uint16_t lock = dspico_card_lock_wait();
  dspico_card_set_cmd(command);
  dspico_card_start_xfer(DSPICO_USB_DEFAULT_COMMAND_SETTINGS, false);
  dspico_card_wait_busy();
  dspico_card_lock_release(lock);
}

//--------------------------------------------------------------------+
// IN endpoint (device -> host): USB data becomes card WRITE data phases
//--------------------------------------------------------------------+

// Push up to 512 bytes of the pending IN transfer into the current DSpico
// double buffer. `start_transfer` marks the first block of the transfer.
static void in_send_block(dspico_in_endpoint_t* s, bool start_transfer) {
  uint32_t length = s->length - s->offset;
  if (length > 0) {
    if (length > 512) length = 512;

    uint16_t lock = dspico_card_lock_wait();
    dspico_card_set_cmd(DSPICO_CMD_USB_WRITE_DATA(s->dspico_buf, s->ep, start_transfer, length));
    dspico_card_start_xfer(DSPICO_USB_WRITE_DATA_SETTINGS, false);
    // Feed the full LEN_512 phase (128 words); cpu_write reads only the
    // `length` valid bytes and zero-pads the rest of the phase (no OOB
    // reads past the TinyUSB buffer).
    dspico_card_cpu_write(s->buffer + s->offset, 512 / 4, length);
    uplink_dbg_log_raw("INBLK ep=%u %uB", (uint32_t)s->ep, length);
    dspico_card_lock_release(lock);
    s->offset += length;
    s->dspico_buf = 1 - s->dspico_buf;
  } else if (start_transfer) {
    // Zero-length transfer
    uint16_t lock = dspico_card_lock_wait();
    dspico_card_set_cmd(DSPICO_CMD_USB_WRITE_DATA(s->dspico_buf, s->ep, true, 0));
    dspico_card_start_xfer(DSPICO_USB_DEFAULT_COMMAND_SETTINGS, false);
    dspico_card_wait_busy();
    dspico_card_lock_release(lock);
  }
}

static void in_begin(uint8_t ep_addr, const uint8_t* buffer, uint32_t length) {
  dspico_in_endpoint_t* s = &s_in_ep[ep_addr & 0x0F];
  // Keep the full endpoint address (IN direction bit set): the DSpico
  // firmware (ntrCardRomGameUsb.cpp) passes the endpoint field of
  // WRITE_DATA / BEGIN_TRANSFER straight to its own dcd_edpt_xfer(),
  // where bit 7 selects direction. Stripping it would arm EPn OUT with
  // our data instead of EPn IN, the host's IN tokens get NAKed forever,
  // and no XFER_COMPLETE event is ever queued. The reference port stores
  // TUSB_DIR_IN_MASK | n in its IN endpoint state for the same reason.
  s->ep = ep_addr;
  s->buffer = (uint8_t*)buffer;
  s->length = length;
  s->offset = 0;
  s->remaining = length;
  s->dspico_buf = 0;
  s->active = true;
  // Prime both DSpico double buffers up front (the DSpico packetizes the
  // 512-byte blocks into full-speed USB transfers on its side).
  in_send_block(s, true);
  in_send_block(s, false);
}

static void in_process_complete(uint8_t ep_addr, uint32_t transferred) {
  dspico_in_endpoint_t* s = &s_in_ep[ep_addr & 0x0F];
  if (s->active) {
    s->remaining -= transferred; uplink_dbg_log_raw("INCMP e%u t%u r%u", (uint32_t)ep_addr, transferred, s->remaining);
    if (s->remaining == 0) {
      dcd_event_xfer_complete(0, ep_addr, s->length, XFER_RESULT_SUCCESS, true);
      s->active = false;
    } else {
      uint32_t length = s->remaining;
      if (length > 512) length = 512;

      uint16_t lock = dspico_card_lock_wait();
      dspico_card_set_cmd(DSPICO_CMD_USB_COMMAND_BEGIN_TRANSFER(s->ep, 1 - s->dspico_buf, length));
      dspico_card_start_xfer(DSPICO_USB_DEFAULT_COMMAND_SETTINGS, false);
      dspico_card_wait_busy();
      dspico_card_lock_release(lock);
      in_send_block(s, false);
    }
  } else {
    // The examples report this happens once for the control endpoint;
    // forward the raw byte count as-is.
    dcd_event_xfer_complete(0, ep_addr, transferred, XFER_RESULT_SUCCESS, true);
  }
}

//--------------------------------------------------------------------+
// OUT endpoint (host -> device): card READ data phases fill the DSpico
// double buffers that hold the USB OUT packets
//--------------------------------------------------------------------+

static void out_begin(uint8_t ep_addr, uint8_t* buffer, uint32_t length) {
  dspico_out_endpoint_t* s = &s_out_ep[ep_addr & 0x0F];
  s->ep = ep_addr & 0x0F;
  s->buffer = buffer;
  s->length = length;
  s->offset = 0;
  s->total_received = 0;
  s->dspico_buf = 0;
  s->active = true;

  uint32_t first = length;
  if (first > 512) first = 512;

  uint16_t lock = dspico_card_lock_wait();
  dspico_card_set_cmd(DSPICO_CMD_USB_COMMAND_BEGIN_TRANSFER(s->ep, s->dspico_buf, first));
  dspico_card_start_xfer(DSPICO_USB_DEFAULT_COMMAND_SETTINGS, false);
  dspico_card_wait_busy();
  dspico_card_lock_release(lock);
}

// Pull one DSpico double buffer (up to 512 bytes) out of the card data port
static void out_recv_block(dspico_out_endpoint_t* s) {
  uint32_t length = s->total_received - s->offset;
  if (length > 0) {
    if (length > 512) length = 512;

    uint16_t lock = dspico_card_lock_wait();
    dspico_card_set_cmd(DSPICO_CMD_USB_READ_DATA(s->dspico_buf, s->ep));
    dspico_card_start_xfer(DSPICO_USB_READ_DATA_SETTINGS, false);
    // Store the valid USB bytes; cpu_read keeps draining the card phase
    // until the busy bit clears, discarding any padding.
    dspico_card_cpu_read(s->buffer + s->offset, (length + 3) / 4);
    dspico_card_lock_release(lock);
    s->offset += length;
    s->dspico_buf = 1 - s->dspico_buf;
  }
}

static void out_process_complete(uint8_t ep_addr, uint32_t transferred) {
  dspico_out_endpoint_t* s = &s_out_ep[ep_addr & 0x0F];
  if (s->active) {
    s->total_received += transferred;

    // Bytes still to come after the 512-byte block just completed
    // (signed on purpose: negative means the transfer is done)
    int length = (int)s->length - (int)s->offset - 512;
    if (length > 512) length = 512;

    if (transferred == 512) {
      if (length > 0) {
        uint16_t lock = dspico_card_lock_wait();
        dspico_card_set_cmd(DSPICO_CMD_USB_COMMAND_BEGIN_TRANSFER(s->ep, 1 - s->dspico_buf, length));
        dspico_card_start_xfer(DSPICO_USB_DEFAULT_COMMAND_SETTINGS, false);
        dspico_card_wait_busy();
        dspico_card_lock_release(lock);
      }
    }

    out_recv_block(s);

    if (transferred < 512 || length == 0) {
      dcd_event_xfer_complete(0, ep_addr, s->offset, XFER_RESULT_SUCCESS, true);
      s->active = false;
    }
  } else {
    // idk (same comment as in the examples)
    dcd_event_xfer_complete(0, ep_addr, transferred, XFER_RESULT_SUCCESS, true);
  }
}

//--------------------------------------------------------------------+
// Event polling (replaces the examples' IRQ-woken USB thread)
//--------------------------------------------------------------------+

// Drain the DSpico event queue. Each event is one GET_EVENT transaction.
//--------------------------------------------------------------------+
// TinyUSB platform hooks
//--------------------------------------------------------------------+

// Coarse millisecond clock for TinyUSB's delay handling (the OPT_OS_NONE
// weak tusb_time_delay_ms_api() busy-waits on it). There is no free hardware
// timer inside a running game, so the counter advances once per poll
// iteration (~16.7 ms at 60 Hz). TinyUSB only uses this for optional
// device-side delays.
static uint32_t s_millis = 0;

uint32_t tusb_time_millis_api(void) {
  return s_millis;
}

// TinyUSB's tusb_int_handler() references this DCD API. The port is
// poll-based (dspico_dcd_poll() drains the event queue from the uplink
// thread), so there is no interrupt to handle.
void dcd_int_handler(uint8_t rhport) {
  (void) rhport;
}

// Suspend safety net.
//
// The DSpico firmware emits hardware SUSPEND/RESUME pairs (verified in
// dspico-firmware/src/tinyusb/dcd_rp2040.c: DEV_SUSPEND and
// DEV_RESUME_FROM_HOST are both enabled), so a lost RESUME should be
// rare. If one is ever missed, TinyUSB stays suspended and every
// transfer NAKs until the host retries with fresh bus activity. As a
// safety net: whenever we observe host activity (a SETUP or a
// XFER_COMPLETE) while tud_suspended(), inject a RESUME event first so
// the stack can recover without waiting for the next host reset.
// Counters also make the real SUSPEND/RESUME traffic visible on the
// debug console.
static uint32_t s_n_suspend = 0;
static uint32_t s_n_resume = 0;
static uint32_t s_n_synth_resume = 0;

static void dspico_ensure_resumed(void) {
  if (tud_suspended()) {
    dcd_event_bus_signal(0, DCD_EVENT_RESUME, true);
    s_n_synth_resume++;
    uplink_dbg_log("SRES x%u", s_n_synth_resume);
  }
}

// Returns when the queue is empty (event 0) or the DSpico reports the last
// queued event (bit 31), mirroring the examples' do-while(!lastEvent) loop.
void dspico_dcd_poll(void) {
  s_millis += 17;
#if UPLINK_DBG_ENABLED
  // Per-poll event summary: how many words did this drain pull, and of what
  // class? A high sof count with x=0 right after an INBLK means the FIFO
  // filled with SOFs and the XFER_COMPLETE was overwritten before we drained.
  uint32_t n_total = 0, n_sof = 0, n_xfer = 0, n_setup = 0, n_other = 0;
#endif
  for (;;) {
    uint32_t event = 0;
    bool last_event = false;

    uint16_t lock = dspico_card_lock_wait();
    dspico_card_set_cmd(DSPICO_CMD_USB_GET_EVENT);
    dspico_card_start_xfer(DSPICO_USB_EVENT_SETTINGS, false);
    dspico_card_cpu_read(&event, 1);
    dspico_card_lock_release(lock);

    last_event = (event >> 31) != 0;
    event &= 0x7FFFFFFFu;

#if UPLINK_DBG_ENABLED
    // Heartbeat: how long has the DSpico event queue been silent? Silence
    // after an attach means the card is not forwarding USB events at all.
    static const uint32_t dbg_idle_thresholds_ms[] = {5000, 10000, 30000, 60000, 120000, 300000};
    static uint32_t dbg_idle_since_ms = 0;
    if (event == 0) {
      uint32_t now = uplink_dbg_ms();
      if (dbg_idle_since_ms == 0) dbg_idle_since_ms = now;
      uint32_t idle = now - dbg_idle_since_ms;
      for (int i = 0; i < 6; i++) {
        if (idle >= dbg_idle_thresholds_ms[i] && idle - dbg_idle_thresholds_ms[i] < 5000) {
          uplink_dbg_log("no events for %u s", dbg_idle_thresholds_ms[i] / 1000);
        }
      }
    } else {
      dbg_idle_since_ms = 0;
    }
    if (event != 0) {
      n_total++;
      if ((event >> 30) == 1) n_setup++;
      else if ((event >> 28) == 2) n_sof++;
      else if ((event >> 28) == 3) n_xfer++;
      else n_other++;
    }
#endif

    if (event == 0) {
      // USB_EVENT_NONE
      break;
    } else if ((event >> 30) == 1) {
      // USB_EVENT_SETUP_RECEIVED: second word carries wValue/bRequest/type
      dspico_ensure_resumed();
      tusb_control_request_t setup;
      setup.wLength = (event >> 16) & 0x1FFF;
      uint32_t direction = (event >> 29) & 1;
      setup.wIndex = event & 0xFFFF;

      uint32_t w = 0;
      lock = dspico_card_lock_wait();
      dspico_card_set_cmd(DSPICO_CMD_USB_GET_EVENT);
      dspico_card_start_xfer(DSPICO_USB_EVENT_SETTINGS, false);
      dspico_card_cpu_read(&w, 1);
      dspico_card_lock_release(lock);
      last_event = (w >> 31) != 0;
      w &= 0x7FFFFFFFu;
      setup.wValue = w & 0xFFFF;
      setup.bRequest = (w >> 16) & 0xFF;
      setup.bmRequestType = (w >> 24) & 0x7F;
      setup.bmRequestType_bit.direction = direction;
      // bmRequestType bRequest wValue wIndex wLength, e.g.
      // "SETUP 80 06 v100 i0 l12" = GET_DESCRIPTOR(DEVICE)
      uplink_dbg_log_raw("SETUP %02x %02x v%03x i%u l%u", setup.bmRequestType,
                     setup.bRequest, setup.wValue, setup.wIndex, setup.wLength);
      dcd_event_setup_received(0, (const uint8_t*)&setup, true);
    } else if ((event >> 28) == 2) {
      // USB_EVENT_SOF
      dcd_event_sof(0, event & 0x7FF, true);
    } else if ((event >> 28) == 3) {
      // USB_EVENT_XFER_COMPLETE
      uint32_t endpoint = event & 0xFF;
      uint32_t bytes = (event >> 8) & 0x1FFF;
      uplink_dbg_log_raw("XFERC ep=%02x %c %uB", endpoint, (endpoint & 0x80) ? 'I' : 'O', bytes);
      dspico_ensure_resumed();
      if (tu_edpt_dir(endpoint) == TUSB_DIR_IN) {
        in_process_complete(endpoint, bytes);
      } else {
        out_process_complete(endpoint, bytes);
      }
    } else if (event == 1) {
      uplink_dbg_log("BUS RESET");
      dcd_event_bus_reset(0, TUSB_SPEED_FULL, true); // USB_EVENT_BUS_RESET
    } else if (event == 2) {
      uplink_dbg_log("UNPLUGGED");
      dcd_event_bus_signal(0, DCD_EVENT_UNPLUGGED, true); // USB_EVENT_UNPLUGGED
    } else if (event == 3) {
      s_n_suspend++;
      uplink_dbg_log("SUSP x%u", s_n_suspend);
      dcd_event_bus_signal(0, DCD_EVENT_SUSPEND, true); // USB_EVENT_SUSPEND
    } else if (event == 4) {
      s_n_resume++;
      uplink_dbg_log("RES x%u", s_n_resume);
      dcd_event_bus_signal(0, DCD_EVENT_RESUME, true); // USB_EVENT_RESUME
    } else {
      uplink_dbg_log("UNKNOWN evt 0x%08x", event);
    }
    // (unknown event words were previously silently ignored)

    if (last_event) {
      break;
    }
  }
#if UPLINK_DBG_ENABLED
  if (n_total > 0) {
    uplink_dbg_log_raw("POLL n=%u sof=%u x=%u s=%u o=%u", n_total, n_sof, n_xfer, n_setup, n_other);
  }
#endif
}
//--------------------------------------------------------------------+
// TinyUSB Device Controller API
//--------------------------------------------------------------------+

// Initialize controller to device mode
bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init) {
  (void) rhport;
  (void) rh_init;
  uplink_dbg_log("DCD INIT cmd sent");
  send_command(DSPICO_CMD_USB_COMMAND_INIT);
  return true;
}

bool dcd_deinit(uint8_t rhport) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_DEINIT);
  return true;
}

// The DSpico firmware needs the INTERRUPT_ENABLE command to arm its USB
// event pipeline (the examples send it from here; TinyUSB's usbd_init()
// calls it at boot). We ignore the card IRQ line itself and poll
// GET_EVENT instead, so the command is sent exactly once.
static bool s_int_enabled = false;
void dcd_int_enable(uint8_t rhport) {
  (void) rhport;
  if (!s_int_enabled) {
    s_int_enabled = true;
    uplink_dbg_log("INTERRUPT_ENABLE cmd sent");
    send_command(DSPICO_CMD_USB_COMMAND_INTERRUPT_ENABLE);
  }
}

// Keep the event pipeline armed: the polling port never relies on the card
// IRQ line, and TinyUSB may toggle this on suspend/resume.
void dcd_int_disable(uint8_t rhport) {
  (void) rhport;
}

// The host re-addresses the device (and re-reads the descriptor) right after
// the status IN of SET_ADDRESS, so the DSpico's hardware address must be
// switched within a few ms. dcd_edpt0_status_complete() sends
// FINISH_SET_ADDRESS once that status IN completes; this flag tells the
// busy-poll below when the switch has landed.
static volatile bool s_set_addr_done = false;

// Receive Set Address request. BEGIN_SET_ADDRESS only arms the status IN; the
// address is switched by FINISH_SET_ADDRESS, sent from
// dcd_edpt0_status_complete() once the status IN completes. The normal poll
// loop runs once per frame (~16.7 ms) and the host gives up on the re-addressed
// descriptor request after ~5-10 ms of silence, so busy-poll the event queue
// here until the status IN lands, switching the address well within the host's
// patience. This blocks the MainRoutine briefly (a few ms, ~20 ms worst case)
// once per enumeration.
void dcd_set_address(uint8_t rhport, uint8_t dev_addr) {
  (void) rhport;
  (void) dev_addr;
  uplink_dbg_log_raw("SET_ADDR %u", dev_addr);
  s_set_addr_done = false;
  send_command(DSPICO_CMD_USB_COMMAND_BEGIN_SET_ADDRESS);
  for (uint32_t i = 0; i < 200 && !s_set_addr_done; i++) {
    dspico_dcd_poll();
    tud_task();
  }
  if (!s_set_addr_done) {
    uplink_dbg_log_raw("SET_ADDR TIMEOUT");
  }
}

void dcd_remote_wakeup(uint8_t rhport) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_REMOTE_WAKEUP);
}

// Connect by enabling the DSpico's internal pull-up on D+
void dcd_connect(uint8_t rhport) {
  (void) rhport;
  uplink_dbg_log("CONNECT (D+ pull-up)");
  send_command(DSPICO_CMD_USB_COMMAND_CONNECT);
}

void dcd_disconnect(uint8_t rhport) {
  (void) rhport;
  uplink_dbg_log("DISCONNECT (pull-up off)");
  send_command(DSPICO_CMD_USB_COMMAND_DISCONNECT);
}

// SOF forwarding is left at the DSpico's default; toggling it here is ignored.
// (SOF_DISABLE was tried to stop the ~1 kHz SOF stream from flooding the
// poll-based event drain, but sending it killed the DSpico's event pipeline
// entirely, so it must not be issued.)
void dcd_sof_enable(uint8_t rhport, bool en) {
  (void) rhport;
  (void) en;
}

// Called after the status IN of a control transfer completes
void dcd_edpt0_status_complete(uint8_t rhport, tusb_control_request_t const* request) {
  (void) rhport;
  if (request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_DEVICE &&
      request->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD &&
      request->bRequest == TUSB_REQ_SET_ADDRESS) {
    uplink_dbg_log_raw("FIN_SET_ADDR %u", (uint32_t)request->wValue);
    send_command(DSPICO_CMD_USB_COMMAND_FINISH_SET_ADDRESS((uint8_t)request->wValue));
    s_set_addr_done = true;
  }
}

// Configure endpoint according to its descriptor
bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const* ep_desc) {
  (void) rhport;
  uplink_dbg_log("OPEN ep=0x%02x", ep_desc->bEndpointAddress);
  send_command(DSPICO_CMD_USB_COMMAND_EP_OPEN(
      ep_desc->bEndpointAddress, ep_desc->wMaxPacketSize, ep_desc->bmAttributes.xfer));
  return true;
}

void dcd_edpt_close_all(uint8_t rhport) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_EP_CLOSE_ALL);
}

// Submit a transfer; dcd_event_xfer_complete() arrives via dspico_dcd_poll()
bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer, uint16_t total_bytes) {
  (void) rhport;
  uplink_dbg_log_raw("XFERQ ep=%02x %c %uB", ep_addr, (ep_addr & 0x80) ? 'I' : 'O', total_bytes);
  if (tu_edpt_dir(ep_addr) == TUSB_DIR_IN) {
    in_begin(ep_addr, buffer, total_bytes);
  } else {
    out_begin(ep_addr, buffer, total_bytes);
  }
  return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  uplink_dbg_log("STALL ep=0x%02x", ep_addr);
  send_command(DSPICO_CMD_USB_COMMAND_EP_STALL(ep_addr));
}

// Clear stall; data toggle is also reset to DATA0 by the DSpico
void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  uplink_dbg_log("CLRSTL ep=0x%02x", ep_addr);
  send_command(DSPICO_CMD_USB_COMMAND_EP_CLEAR_STALL(ep_addr));
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  uplink_dbg_log("CLOSE ep=0x%02x", ep_addr);
  send_command(DSPICO_CMD_USB_COMMAND_EP_CLOSE(ep_addr));
}

// The NDS has no caches
bool dcd_dcache_clean(const void* addr, uint32_t data_size) {
  (void) addr; (void) data_size;
  return true;
}

bool dcd_dcache_invalidate(const void* addr, uint32_t data_size) {
  (void) addr; (void) data_size;
  return true;
}

bool dcd_dcache_clean_invalidate(const void* addr, uint32_t data_size) {
  (void) addr; (void) data_size;
  return true;
}

#endif // CFG_TUD_ENABLED
