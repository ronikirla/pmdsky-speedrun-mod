#include "usb_descriptors.h"

// Standard CDC-ACM (Communication Device Class) descriptor set.
// Interface 0: CDC control (notification EP3 IN)
// Interface 1: CDC data (EP1 OUT / EP2 IN, 64-byte full-speed packets)

#define UPLINK_CFG_DESC_LEN \
    (9 /* config */ + (9 + 5 + 5 + 4 + 5) /* cdc control */ + 7 /* notif EP */ \
     + (9 + 7 + 7) /* cdc data */)

const uint8_t uplink_descriptor_device[] = {
    // Device descriptor
    18, /* bLength */
    0x01, /* bDescriptorType = DEVICE */
    UPLINK_BCD_USB, /* bcdUSB 1.10 */
    0x02, /* bDeviceClass = CDC */
    0x00, /* bDeviceSubClass */
    0x00, /* bDeviceProtocol */
    UPLINK_EPSIZE_DATA, /* bMaxPacketSize0 */
    UPLINK_VID & 0xFF, (UPLINK_VID >> 8) & 0xFF, /* idVendor */
    UPLINK_PID & 0xFF, (UPLINK_PID >> 8) & 0xFF, /* idProduct */
    0x0100, /* bcdDevice 1.00 */
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

// String descriptors (UTF-16LE, first word = (char_count << 8) | 0x03)
#define UPLINK_STR(len, ...) ((uint16_t)(((len) << 8) | 0x03)), ##__VA_ARGS__

const uint16_t uplink_descriptor_string[] = {
    0x0409, // 0: language (en-US)
    UPLINK_STR(6, 'P', 'M', 'D', 'S', 'k', 'y'), // 1: manufacturer
    UPLINK_STR(10, 'U', 'p', 'l', 'i', 'n', 'k', ' ', 'C', 'D', 'C'), // 2: product
    UPLINK_STR(4, '0', '0', '0', '1'), // 3: serial
};

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  switch (index) {
    case 0: return uplink_descriptor_string;
    case 1: return &uplink_descriptor_string[1];
    case 2: return &uplink_descriptor_string[2];
    case 3: return &uplink_descriptor_string[3];
    default: return NULL;
  }
}
