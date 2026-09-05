#include "common.h"
#include "UsbStringDescriptor.h"
#include "tusb.h"
#include "usb_descriptors.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]  VIDEO | AUDIO | MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
    _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VIDEO, 5) | _PID_MAP(VENDOR, 6) )

#define USB_VID   0xCafe
#define USB_BCD   0x0200

enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_UVC_CONTROL,
    STRID_UVC_STREAMING
};

const u8* tud_descriptor_device_cb(void)
{
    static const tusb_desc_device_t deviceDescriptor =
    {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = USB_BCD,

        // Use Interface Association Descriptor (IAD) for Video
        // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
        .bDeviceClass       = TUSB_CLASS_MISC,
        .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol    = MISC_PROTOCOL_IAD,

        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor           = USB_VID,
        .idProduct          = USB_PID,
        .bcdDevice          = 0x0100,

        .iManufacturer      = STRID_MANUFACTURER,
        .iProduct           = STRID_PRODUCT,
        .iSerialNumber      = STRID_SERIAL,

        .bNumConfigurations = 0x01
    };

    return (const u8*)&deviceDescriptor;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

/* Time stamp base clock. It is a deprecated parameter. */
#define UVC_CLOCK_FREQUENCY    27000000

/* video capture path */
#define UVC_ENTITY_CAP_INPUT_TERMINAL  0x01
#define UVC_ENTITY_CAP_PROCESSING      0x02
#define UVC_ENTITY_CAP_OUTPUT_TERMINAL 0x03

enum
{
    ITF_NUM_VIDEO_CONTROL,
    ITF_NUM_VIDEO_STREAMING,
    ITF_NUM_TOTAL
};

#define EPNUM_VIDEO_IN      0x81

#define USE_ISO_STREAMING (!CFG_TUD_VIDEO_STREAMING_BULK)

typedef struct TU_ATTR_PACKED
{
    tusb_desc_interface_t itf;
    tusb_desc_video_control_header_1itf_t header;
    tusb_desc_video_control_camera_terminal_t camera_terminal;
    u8 processing_unit[13];
    tusb_desc_video_control_output_terminal_t output_terminal;
} uvc_control_desc_t;

/* Windows support YUY2 and NV12
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/stream/usb-video-class-driver-overview */

typedef struct TU_ATTR_PACKED
{
    tusb_desc_interface_t itf;
    tusb_desc_video_streaming_input_header_1byte_t header;
    tusb_desc_video_format_uncompressed_t format;
    tusb_desc_video_frame_uncompressed_2int_t frame;
    tusb_desc_video_streaming_color_matching_t color;

#if USE_ISO_STREAMING
        // For ISO streaming, USB spec requires to alternate interface
        tusb_desc_interface_t itf_alt;
#endif

    tusb_desc_endpoint_t ep;
} uvc_streaming_desc_t;

typedef struct TU_ATTR_PACKED
{
    tusb_desc_configuration_t config;
    tusb_desc_interface_assoc_t iad;
    uvc_control_desc_t video_control;
    uvc_streaming_desc_t video_streaming;
} uvc_cfg_desc_t;

const u8* tud_descriptor_configuration_cb(u8 index)
{
    static const uvc_cfg_desc_t configurationDescriptor =
    {
        .config = {
            .bLength = sizeof(tusb_desc_configuration_t),
            .bDescriptorType = TUSB_DESC_CONFIGURATION,

            .wTotalLength = sizeof(uvc_cfg_desc_t),
            .bNumInterfaces = ITF_NUM_TOTAL,
            .bConfigurationValue = 1,
            .iConfiguration = 0,
            .bmAttributes =  TU_BIT(7),
            .bMaxPower = 100 / 2
        },
        .iad = {
            .bLength = sizeof(tusb_desc_interface_assoc_t),
            .bDescriptorType = TUSB_DESC_INTERFACE_ASSOCIATION,

            .bFirstInterface = ITF_NUM_VIDEO_CONTROL,
            .bInterfaceCount = 2,
            .bFunctionClass = TUSB_CLASS_VIDEO,
            .bFunctionSubClass = VIDEO_SUBCLASS_INTERFACE_COLLECTION,
            .bFunctionProtocol = VIDEO_ITF_PROTOCOL_UNDEFINED,
            .iFunction = 0
        },

        .video_control = {
            .itf = {
                .bLength = sizeof(tusb_desc_interface_t),
                .bDescriptorType = TUSB_DESC_INTERFACE,

                .bInterfaceNumber = ITF_NUM_VIDEO_CONTROL,
                .bAlternateSetting = 0,
                .bNumEndpoints = 0,
                .bInterfaceClass = TUSB_CLASS_VIDEO,
                .bInterfaceSubClass = VIDEO_SUBCLASS_CONTROL,
                .bInterfaceProtocol = VIDEO_ITF_PROTOCOL_15,
                .iInterface = STRID_UVC_CONTROL
            },
            .header = {
                .bLength = sizeof(tusb_desc_video_control_header_1itf_t),
                .bDescriptorType = TUSB_DESC_CS_INTERFACE,
                .bDescriptorSubType = VIDEO_CS_ITF_VC_HEADER,

                .bcdUVC = VIDEO_BCD_1_50,
                .wTotalLength = sizeof(uvc_control_desc_t) - sizeof(tusb_desc_interface_t)/* - sizeof(tusb_desc_endpoint_t) - 5*/, // CS VC descriptors only
                .dwClockFrequency = UVC_CLOCK_FREQUENCY,
                .bInCollection = 1,
                .baInterfaceNr = { ITF_NUM_VIDEO_STREAMING }
            },
            .camera_terminal = {
                .bLength = sizeof(tusb_desc_video_control_camera_terminal_t),
                .bDescriptorType = TUSB_DESC_CS_INTERFACE,
                .bDescriptorSubType = VIDEO_CS_ITF_VC_INPUT_TERMINAL,

                .bTerminalID = UVC_ENTITY_CAP_INPUT_TERMINAL,
                .wTerminalType = VIDEO_ITT_CAMERA,
                .bAssocTerminal = 0,
                .iTerminal = 0,
                .wObjectiveFocalLengthMin = 0,
                .wObjectiveFocalLengthMax = 0,
                .wOcularFocalLength = 0,
                .bControlSize = 3,
                .bmControls = { 0, 0, 0 }
            },
            .processing_unit = { 0x0D, 0x24, 0x05, UVC_ENTITY_CAP_PROCESSING, UVC_ENTITY_CAP_INPUT_TERMINAL, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 },
            .output_terminal = {
                .bLength = sizeof(tusb_desc_video_control_output_terminal_t),
                .bDescriptorType = TUSB_DESC_CS_INTERFACE,
                .bDescriptorSubType = VIDEO_CS_ITF_VC_OUTPUT_TERMINAL,

                .bTerminalID = UVC_ENTITY_CAP_OUTPUT_TERMINAL,
                .wTerminalType = VIDEO_TT_STREAMING,
                .bAssocTerminal = 0,
                .bSourceID = UVC_ENTITY_CAP_PROCESSING,
                .iTerminal = 0
            }
        },

        .video_streaming = {
            .itf = {
                .bLength = sizeof(tusb_desc_interface_t),
                .bDescriptorType = TUSB_DESC_INTERFACE,

                .bInterfaceNumber = ITF_NUM_VIDEO_STREAMING,
                .bAlternateSetting = 0,
                .bNumEndpoints = CFG_TUD_VIDEO_STREAMING_BULK, // bulk 1, iso 0
                .bInterfaceClass = TUSB_CLASS_VIDEO,
                .bInterfaceSubClass = VIDEO_SUBCLASS_STREAMING,
                .bInterfaceProtocol = VIDEO_ITF_PROTOCOL_15,
                .iInterface = STRID_UVC_STREAMING
            },
            .header = {
                .bLength = sizeof(tusb_desc_video_streaming_input_header_1byte_t),
                .bDescriptorType = TUSB_DESC_CS_INTERFACE,
                .bDescriptorSubType = VIDEO_CS_ITF_VS_INPUT_HEADER,

                .bNumFormats = 1,
                .wTotalLength = sizeof(uvc_streaming_desc_t) - sizeof(tusb_desc_interface_t)
                    - sizeof(tusb_desc_endpoint_t) - (USE_ISO_STREAMING ? sizeof(tusb_desc_interface_t) : 0) , // CS VS descriptors only
                .bEndpointAddress = EPNUM_VIDEO_IN,
                .bmInfo = 0,
                .bTerminalLink = UVC_ENTITY_CAP_OUTPUT_TERMINAL,
                .bStillCaptureMethod = 0,
                .bTriggerSupport = 0,
                .bTriggerUsage = 0,
                .bControlSize = 1,
                .bmaControls = { 0 }
            },
            .format = {
                .bLength = sizeof(tusb_desc_video_format_uncompressed_t),
                .bDescriptorType = TUSB_DESC_CS_INTERFACE,
                .bDescriptorSubType = VIDEO_CS_ITF_VS_FORMAT_UNCOMPRESSED,
                .bFormatIndex = 1, // 1-based index
                .bNumFrameDescriptors = 1,
                .guidFormat = { TUD_VIDEO_GUID_YUY2 },
                .bBitsPerPixel = 16,
                .bDefaultFrameIndex = 1,
                .bAspectRatioX = 0,
                .bAspectRatioY = 0,
                .bmInterlaceFlags = 0,
                .bCopyProtect = 0
            },
            .frame = {
                .bLength = sizeof(tusb_desc_video_frame_uncompressed_2int_t),
                .bDescriptorType = TUSB_DESC_CS_INTERFACE,
                .bDescriptorSubType = VIDEO_CS_ITF_VS_FRAME_UNCOMPRESSED,
                .bFrameIndex = 1, // 1-based index
                .bmCapabilities = 0,
                .wWidth = FRAME_WIDTH,
                .wHeight = FRAME_HEIGHT,
                .dwMinBitRate = FRAME_WIDTH * FRAME_HEIGHT * 16 * 1,
                .dwMaxBitRate = FRAME_WIDTH * FRAME_HEIGHT * 16 * FRAME_RATE,
                .dwMaxVideoFrameBufferSize = FRAME_WIDTH * FRAME_HEIGHT * 16 / 8,
                .dwDefaultFrameInterval = 10000000 / FRAME_RATE,
                .bFrameIntervalType = 2, // 2 discrete
                .dwFrameInterval = {
                    10000000 / FRAME_RATE,
                    10000000
                }
            },
            .color = {
                .bLength = sizeof(tusb_desc_video_streaming_color_matching_t),
                .bDescriptorType = TUSB_DESC_CS_INTERFACE,
                .bDescriptorSubType = VIDEO_CS_ITF_VS_COLORFORMAT,

                .bColorPrimaries = VIDEO_COLOR_PRIMARIES_BT709,
                .bTransferCharacteristics = VIDEO_COLOR_XFER_CH_BT709,
                .bMatrixCoefficients = VIDEO_COLOR_COEF_BT709
            },

#if USE_ISO_STREAMING
            .itf_alt = {
                .bLength = sizeof(tusb_desc_interface_t),
                .bDescriptorType = TUSB_DESC_INTERFACE,

                .bInterfaceNumber = ITF_NUM_VIDEO_STREAMING,
                .bAlternateSetting = 1,
                .bNumEndpoints = 1,
                .bInterfaceClass = TUSB_CLASS_VIDEO,
                .bInterfaceSubClass = VIDEO_SUBCLASS_STREAMING,
                .bInterfaceProtocol = VIDEO_ITF_PROTOCOL_15,
                .iInterface = STRID_UVC_STREAMING
            },
#endif

            .ep = {
                .bLength = sizeof(tusb_desc_endpoint_t),
                .bDescriptorType = TUSB_DESC_ENDPOINT,

                .bEndpointAddress = EPNUM_VIDEO_IN,
                .bmAttributes = {
                    .xfer = CFG_TUD_VIDEO_STREAMING_BULK ? TUSB_XFER_BULK : TUSB_XFER_ISOCHRONOUS,
                    .sync = CFG_TUD_VIDEO_STREAMING_BULK ? 0 : 1 // asynchronous
                },
                .wMaxPacketSize = CFG_TUD_VIDEO_STREAMING_BULK ? 64 : CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE,
                .bInterval = 1
            }
        }
    };

    return (const u8*)&configurationDescriptor;
}

const u16* tud_descriptor_string_cb(u8 index, u16 langid)
{
    switch (index)
    {
        case STRID_LANGID:
        {
            static const UsbStringDescriptor<2> descriptor(0x0409);
            return (const u16*)&descriptor;
        }
        case STRID_MANUFACTURER:
        {
            return USB_STRING_DESCRIPTOR(u"LNH");
        }
        case STRID_PRODUCT:
        {
            return USB_STRING_DESCRIPTOR(u"DSpico Camera Example");
        }
        case STRID_SERIAL:
        {
            return USB_STRING_DESCRIPTOR(u"123456789");
        }
        case STRID_UVC_CONTROL:
        {
            return USB_STRING_DESCRIPTOR(u"UVC Control");
        }
        case STRID_UVC_STREAMING:
        {
            return USB_STRING_DESCRIPTOR(u"UVC Streaming");
        }
        default:
        {
            return nullptr;
        }
    }
}
