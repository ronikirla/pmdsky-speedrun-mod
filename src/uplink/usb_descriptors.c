#include "usb_descriptors.h"

// Standard CDC-ACM (Communication Device Class) descriptor set.
// Interface 0: CDC control (notification EP3 IN)
// Interface 1: CDC data (EP1 OUT / EP2 IN, 64-byte full-speed packets)

const uint8_t uplink_descriptor_device[] = {
    // Device descriptor
    18, /* bLength */
    0x01, /* bDescriptorType = DEVICE */
    UPLINK_BCD_USB & 0xFF, (UPLINK_BCD_USB >> 8) & 0xFF, /* bcdUSB 1.10 */
    0x02, /* bDeviceClass = CDC */
    0x00, /* bDeviceSubClass */
    0x00, /* bDeviceProtocol */
    UPLINK_EPSIZE_DATA, /* bMaxPacketSize0 */
    UPLINK_VID & 0xFF, (UPLINK_VID >> 8) & 0xFF, /* idVendor */
    UPLINK_PID & 0xFF, (UPLINK_PID >> 8) & 0xFF, /* idProduct */
    0x00, 0x01, /* bcdDevice 1.00 */
    0x01, /* iManufacturer */
    0x02, /* iProduct */
    0x03, /* iSerialNumber */
    0x01, /* bNumConfigurations */
};

const uint8_t uplink_descriptor_configuration[] = {
    // Configuration descriptor
    9,
    0x02, /* bDescriptorType = CONFIGURATION */
    UPLINK_CFG_DESC_LEN & 0xFF, (UPLINK_CFG_DESC_LEN >> 8) & 0xFF, /* wTotalLength */
    0x02, /* bNumInterfaces */
    0x01, /* bConfigurationValue */
    0x00, /* iConfiguration */
    0x80, /* bmAttributes: bus powered, no remote wakeup */
    0x32, /* bMaxPower: 100 mA */

    // CDC control interface
    9,
    0x04, /* bDescriptorType = INTERFACE */
    0x00, /* bInterfaceNumber */
    0x00, /* bAlternateSetting */
    0x01, /* bNumEndpoints (notification) */
    0x02, /* bInterfaceClass = CDC */
    0x02, /* bInterfaceSubClass = CDC Communications */
    0x01, /* bInterfaceProtocol = AT command subset */
    0x00, /* iInterface */

    // Class-specific: CDC header (1.10)
    5, 0x24, 0x01, 0x10, 0x01,
    // Class-specific: call management (no call support, data interface 1)
    5, 0x24, 0x02, 0x00, 0x01,
    // Class-specific: abstract control management (no capabilities)
    4, 0x24, 0x03, 0x00,
    // Class-specific: union (control -> data)
    5, 0x24, 0x06, 0x00, 0x01,

    // Notification endpoint
    7,
    0x05, /* bDescriptorType = ENDPOINT */
    UPLINK_EPNUM_NOTIF, /* bEndpointAddress */
    0x03, /* bmAttributes = INTERRUPT */
    UPLINK_EPSIZE_NOTIF & 0xFF, (UPLINK_EPSIZE_NOTIF >> 8) & 0xFF, /* wMaxPacketSize */
    0x0A, /* bInterval */

    // CDC data interface
    9,
    0x04, /* bDescriptorType = INTERFACE */
    0x01, /* bInterfaceNumber */
    0x00, /* bAlternateSetting */
    0x02, /* bNumEndpoints */
    0x0A, /* bInterfaceClass = CDC Data */
    0x00, /* bInterfaceSubClass */
    0x00, /* bInterfaceProtocol */
    0x01, /* iInterface = "Uplink CDC" */

    // Data endpoints
    7,
    0x05,
    UPLINK_EPNUM_DATA_OUT,
    0x02, /* bmAttributes = BULK */
    UPLINK_EPSIZE_DATA & 0xFF, (UPLINK_EPSIZE_DATA >> 8) & 0xFF,
    0x00,
    7,
    0x05,
    UPLINK_EPNUM_DATA_IN,
    0x02,
    UPLINK_EPSIZE_DATA & 0xFF, (UPLINK_EPSIZE_DATA >> 8) & 0xFF,
    0x00,
};

// String descriptors (UTF-16LE).
//
// IMPORTANT (TinyUSB 0.17 convention): usbd.c computes the string response
// length with tu_desc_len(), i.e. the FIRST BYTE ON THE WIRE is bLength.
// On this little-endian target a header word of (0x03 << 8) | bLength
// serializes as [bLength, 0x03], a valid string descriptor header.
// (The old (len << 8) | 0x03 form serializes as [0x03, len] and made the
// host see bLength=3 / a wrong bDescriptorType on every string -- this is
// what made Windows abort enumeration before SET_CONFIGURATION.)
#define UPLINK_STR_HDR(blen) ((uint16_t)((0x03 << 8) | (blen)))
#define UPLINK_STR(blen, ...) UPLINK_STR_HDR(blen), ##__VA_ARGS__
// Language descriptor header: wire bytes 04 09 (bLength=4, bDescriptorType=9).
#define UPLINK_LANG_HDR ((uint16_t)(0x09 << 8) | 0x04)

// Word offsets of each descriptor's header word inside
// uplink_descriptor_string.
#define UPLINK_STR_OFF_LANG 0  // language descriptor (string 0)
#define UPLINK_STR_OFF_MFG  2  // 0 + 2 language words
#define UPLINK_STR_OFF_PROD  (UPLINK_STR_OFF_MFG + 1 + 6)      // + 1 header + 6 chars
#define UPLINK_STR_OFF_SERIAL (UPLINK_STR_OFF_PROD + 1 + 10)   // + 1 header + 10 chars

const uint16_t uplink_descriptor_string[] = {
    // String 0: language descriptor, wire bytes 04 09 09 04
    // (bLength=4, bDescriptorType=9, wLANGID=0x0409 en-US little-endian).
    UPLINK_LANG_HDR,
    0x0409,
    // String 1: "PMDSky" (6 chars -> bLength 14)
    UPLINK_STR(14, 'P', 'M', 'D', 'S', 'k', 'y'),
    // String 2: "Uplink CDC" (10 chars -> bLength 22)
    UPLINK_STR(22, 'U', 'p', 'l', 'i', 'n', 'k', ' ', 'C', 'D', 'C'),
    // String 3: "0001" (4 chars -> bLength 10)
    UPLINK_STR(10, '0', '0', '0', '1'),
};

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  switch (index) {
    case 0: return &uplink_descriptor_string[UPLINK_STR_OFF_LANG];
    case 1: return &uplink_descriptor_string[UPLINK_STR_OFF_MFG];
    case 2: return &uplink_descriptor_string[UPLINK_STR_OFF_PROD];
    case 3: return &uplink_descriptor_string[UPLINK_STR_OFF_SERIAL];
    default: return NULL;
  }
}

// Compile-time guards: the descriptor arrays must be exactly as long as they
// advertise. A 16-bit constant that silently truncates to one uint8_t
// element (e.g. UPLINK_BCD_USB in a uint8_t array) would otherwise go
// unnoticed and put a malformed descriptor on the wire.
// (bLength / wTotalLength are checked against their literal spec values, which
  // must remain constant expressions for file-scope typedefs in C.)
typedef char uplink_desc_device_size_check[(sizeof(uplink_descriptor_device) == 18) ? 1 : -1];
typedef char uplink_desc_device_blen_check[((int)sizeof(uplink_descriptor_device) == 18) ? 1 : -1];
typedef char uplink_desc_cfg_len_check[(UPLINK_CFG_DESC_LEN == 67) ? 1 : -1];
typedef char uplink_desc_cfg_size_check[(sizeof(uplink_descriptor_configuration) == (size_t)UPLINK_CFG_DESC_LEN) ? 1 : -1];
// String table: 4 (language) + 14 + 22 + 10 = 50 bytes.
typedef char uplink_str_size_check[(sizeof(uplink_descriptor_string) == 50) ? 1 : -1];
// Header word macros must serialize (little-endian) as [bLength, bDescriptorType]
// so tu_desc_len() (first wire byte) sees the real bLength.
typedef char uplink_str_hdr_check[(((int)UPLINK_STR_HDR(14) & 0xFF) == 14 &&
                                   (((int)UPLINK_STR_HDR(14) >> 8) & 0xFF) == 3 &&
                                   ((int)UPLINK_STR_HDR(22) & 0xFF) == 22 &&
                                   ((int)UPLINK_STR_HDR(10) & 0xFF) == 10 &&
                                   ((int)UPLINK_LANG_HDR & 0xFF) == 4 &&
                                   (((int)UPLINK_LANG_HDR >> 8) & 0xFF) == 9) ? 1 : -1];
// Offsets must stay inside the table (each string = 1 header word + char words).
typedef char uplink_str_off_check[(((UPLINK_STR_OFF_LANG + 2) <= (int)(sizeof(uplink_descriptor_string) / 2)) &&
                                   ((UPLINK_STR_OFF_MFG + 1 + 6) <= (int)(sizeof(uplink_descriptor_string) / 2)) &&
                                   ((UPLINK_STR_OFF_PROD + 1 + 10) <= (int)(sizeof(uplink_descriptor_string) / 2)) &&
                                   ((UPLINK_STR_OFF_SERIAL + 1 + 4) <= (int)(sizeof(uplink_descriptor_string) / 2))) ? 1 : -1];
