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
//      the card IRQ (INTERRUPT_ENABLE). Inside the running game the OS owns
//      the IRQ vector table, so we poll GET_EVENT at ~60 Hz instead and
//      dcd_int_enable/disable are no-ops (TinyUSB's os_none queue lock uses
//      them).
//
// The DSpico exchanges data in 512-byte double-buffered card transactions.
// Every card operation (command, or 512-byte data phase) runs inside the
// game's card lock so the game's own card I/O is never disturbed.
#include "tusb.h"
#include "device/dcd.h"
#include "dspico_card.h"

#if CFG_TUD_ENABLED

//--------------------------------------------------------------------+
// Endpoint state (16 IN + 16 OUT, mirroring the examples)
//--------------------------------------------------------------------+

typedef struct {
  uint8_t ep;           // endpoint number (no direction bit)
  uint8_t* buffer;      // TinyUSB endpoint buffer (>= 512 bytes)
  uint32_t length;      // total bytes for this transfer
  uint32_t offset;      // bytes handled so far
  uint32_t remaining;   // bytes not yet reported complete
  uint8_t dspico_buf;   // current DSpico double buffer (0/1)
  bool active;          // a transfer is in flight
} dspico_in_endpoint_t;

typedef struct {
  uint8_t ep;
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
    // Feed the full LEN_512 phase (128 words); cpu_write pads with zeros
    // once the buffer is exhausted and returns when the phase completes.
    dspico_card_cpu_write(s->buffer + s->offset, 512 / 4);
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
  s->ep = ep_addr & 0x0F;
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
    s->remaining -= transferred;
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

// Returns when the queue is empty (event 0) or the DSpico reports the last
// queued event (bit 31), mirroring the examples' do-while(!lastEvent) loop.
void dspico_dcd_poll(void) {
  s_millis += 17;
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

    if (event == 0) {
      // USB_EVENT_NONE
      return;
    } else if ((event >> 30) == 1) {
      // USB_EVENT_SETUP_RECEIVED: second word carries wValue/bRequest/type
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
      dcd_event_setup_received(0, (const uint8_t*)&setup, true);
    } else if ((event >> 28) == 2) {
      // USB_EVENT_SOF
      dcd_event_sof(0, event & 0x7FF, true);
    } else if ((event >> 28) == 3) {
      // USB_EVENT_XFER_COMPLETE
      uint32_t endpoint = event & 0xFF;
      uint32_t bytes = (event >> 8) & 0x1FFF;
      if (tu_edpt_dir(endpoint) == TUSB_DIR_IN) {
        in_process_complete(endpoint, bytes);
      } else {
        out_process_complete(endpoint, bytes);
      }
    } else if (event == 1) {
      dcd_event_bus_reset(0, TUSB_SPEED_FULL, true); // USB_EVENT_BUS_RESET
    } else if (event == 2) {
      dcd_event_bus_signal(0, DCD_EVENT_UNPLUGGED, true); // USB_EVENT_UNPLUGGED
    } else if (event == 3) {
      dcd_event_bus_signal(0, DCD_EVENT_SUSPEND, true); // USB_EVENT_SUSPEND
    } else if (event == 4) {
      dcd_event_bus_signal(0, DCD_EVENT_RESUME, true); // USB_EVENT_RESUME
    }
    // else: unknown event word; ignore (the examples' "invalid" case)

    if (last_event) {
      return;
    }
  }
}
//--------------------------------------------------------------------+
// TinyUSB Device Controller API
//--------------------------------------------------------------------+

// Initialize controller to device mode
bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init) {
  (void) rhport;
  (void) rh_init;
  send_command(DSPICO_CMD_USB_COMMAND_INIT);
  return true;
}

bool dcd_deinit(uint8_t rhport) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_DEINIT);
  return true;
}

// No-op in the polling port: there is no card IRQ to gate, and TinyUSB's
// os_none queue lock routes through these two functions.
void dcd_int_enable(uint8_t rhport) {
  (void) rhport;
}

void dcd_int_disable(uint8_t rhport) {
  (void) rhport;
}

// Receive Set Address request; the status stage completion sends the rest
void dcd_set_address(uint8_t rhport, uint8_t dev_addr) {
  (void) rhport;
  (void) dev_addr;
  send_command(DSPICO_CMD_USB_COMMAND_BEGIN_SET_ADDRESS);
}

void dcd_remote_wakeup(uint8_t rhport) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_REMOTE_WAKEUP);
}

// Connect by enabling the DSpico's internal pull-up on D+
void dcd_connect(uint8_t rhport) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_CONNECT);
}

void dcd_disconnect(uint8_t rhport) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_DISCONNECT);
}

// SOF events stay disabled: the 60 Hz poll is the pacing mechanism and an
// SOF stream would just add ~60 extra card transactions per second.
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
    send_command(DSPICO_CMD_USB_COMMAND_FINISH_SET_ADDRESS((uint8_t)request->wValue));
  }
}

// Configure endpoint according to its descriptor
bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const* ep_desc) {
  (void) rhport;
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
  if (tu_edpt_dir(ep_addr) == TUSB_DIR_IN) {
    in_begin(ep_addr, buffer, total_bytes);
  } else {
    out_begin(ep_addr, buffer, total_bytes);
  }
  return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_EP_STALL(ep_addr));
}

// Clear stall; data toggle is also reset to DATA0 by the DSpico
void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  send_command(DSPICO_CMD_USB_COMMAND_EP_CLEAR_STALL(ep_addr));
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
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
