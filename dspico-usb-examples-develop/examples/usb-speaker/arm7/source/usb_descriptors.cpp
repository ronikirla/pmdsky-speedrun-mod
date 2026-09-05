#include "common.h"
#include <array>
#include "UsbStringDescriptor.h"
#include "tusb.h"
#include "usb_descriptors.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]     AUDIO | MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
    _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VENDOR, 5) )

const u8* tud_descriptor_device_cb(void)
{
    static const tusb_desc_device_t deviceDescriptor =
    {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0110,

        // Use Interface Association Descriptor (IAD) for Audio
        // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
        .bDeviceClass       = TUSB_CLASS_MISC,
        .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol    = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor           = 0xCafe,
        .idProduct          = USB_PID,
        .bcdDevice          = 0x0100,

        .iManufacturer      = STRID_MANUFACTURER,
        .iProduct           = STRID_PRODUCT,
        .iSerialNumber      = STRID_SERIAL,

        .bNumConfigurations = 0x01
    };

    return (const u8*)&deviceDescriptor;
}

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_AUDIO_SPEAKER_STEREO_FB_DESC_LEN)

#define EPNUM_AUDIO_FB      0x01
#define EPNUM_AUDIO_OUT     0x01

const u8* tud_descriptor_configuration_cb(u8 index)
{
    (void)index;

    static const u8 configurationDescriptor[] =
    {
        // Config number, interface count, string index, total length, attribute, power in mA
        TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

        /* Standard Interface Association Descriptor (IAD) */
        TUD_AUDIO_DESC_IAD(/*_firstitf*/ 0, /*_nitfs*/ 0x02, /*_stridx*/ 0x00),
        /* Standard AC Interface Descriptor(4.7.1) */\
        TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ 0, /*_nEPs*/ 0x00, /*_stridx*/ STRID_AUDIO_INTERFACE),
        /* Class-Specific AC Interface Header Descriptor(4.7.2) */
        TUD_AUDIO_DESC_CS_AC(
            /*_bcdADC*/ 0x0200,
            /*_category*/ AUDIO_FUNC_DESKTOP_SPEAKER,
            /*_totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN+TUD_AUDIO_DESC_INPUT_TERM_LEN+TUD_AUDIO_DESC_OUTPUT_TERM_LEN+TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN,
            /*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),
        /* Clock Source Descriptor(4.7.2.1) */
        TUD_AUDIO_DESC_CLK_SRC(
            /*_clkid*/ UAC2_ENTITY_CLOCK,
            /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_PRO_CLK,
            /*_ctrl*/ (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS),
            /*_assocTerm*/ UAC2_ENTITY_INPUT_TERMINAL,
            /*_stridx*/ 0x00),
        /* Input Terminal Descriptor(4.7.2.4) */
        TUD_AUDIO_DESC_INPUT_TERM(
            /*_termid*/ UAC2_ENTITY_INPUT_TERMINAL,
            /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING,
            /*_assocTerm*/ 0x00,
            /*_clkid*/ UAC2_ENTITY_CLOCK,
            /*_nchannelslogical*/ 0x02,
            /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED,
            /*_idxchannelnames*/ 0x00,
            /*_ctrl*/ 0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS),
            /*_stridx*/ 0x00),
        /* Output Terminal Descriptor(4.7.2.5) */
        TUD_AUDIO_DESC_OUTPUT_TERM(
            /*_termid*/ UAC2_ENTITY_OUTPUT_TERMINAL,
            /*_termtype*/ AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER,
            /*_assocTerm*/ UAC2_ENTITY_INPUT_TERMINAL,
            /*_srcid*/ UAC2_ENTITY_FEATURE_UNIT,
            /*_clkid*/ UAC2_ENTITY_CLOCK,
            /*_ctrl*/ 0x0000,
            /*_stridx*/ 0x00),
        /* Feature Unit Descriptor(4.7.2.8) */
        TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL(
            /*_unitid*/ UAC2_ENTITY_FEATURE_UNIT,
            /*_srcid*/ UAC2_ENTITY_INPUT_TERMINAL,
            /*_ctrlch0master*/ AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS,
            /*_ctrlch1*/ AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS,
            /*_ctrlch2*/ AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS,
            /*_stridx*/ 0x00),
        /* Standard AS Interface Descriptor(4.9.1) */
        /* Interface 1, Alternate 0 - default alternate setting with 0 bandwidth */ \
        TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ 1, /*_altset*/ 0x00, /*_nEPs*/ 0x00, /*_stridx*/ STRID_AUDIO_INTERFACE),
        /* Standard AS Interface Descriptor(4.9.1) */
        /* Interface 1, Alternate 1 - alternate interface for data streaming */ \
        TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ 1, /*_altset*/ 0x01, /*_nEPs*/ 0x02, /*_stridx*/ STRID_AUDIO_INTERFACE),
        /* Class-Specific AS Interface Descriptor(4.9.2) */
        TUD_AUDIO_DESC_CS_AS_INT(
            /*_termid*/ UAC2_ENTITY_INPUT_TERMINAL,
            /*_ctrl*/ AUDIO_CTRL_NONE,
            /*_formattype*/ AUDIO_FORMAT_TYPE_I,
            /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM,
            /*_nchannelsphysical*/ 0x02,
            /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED,
            /*_stridx*/ 0x00),
        /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */
        TUD_AUDIO_DESC_TYPE_I_FORMAT(CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX),
        /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */
        TUD_AUDIO_DESC_STD_AS_ISO_EP(
            /*_ep*/ EPNUM_AUDIO_OUT,
            /*_attr*/ (uint8_t) ((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_DATA),
            /*_maxEPsize*/ CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
            /*_interval*/ 0x01),
        /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */
        TUD_AUDIO_DESC_CS_AS_ISO_EP(
            /*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
            /*_ctrl*/ AUDIO_CTRL_NONE,
            /*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC,
            /*_lockdelay*/ 0x0001),
        /* Standard AS Isochronous Feedback Endpoint Descriptor(4.10.2.1) */
        TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(/*_ep*/ EPNUM_AUDIO_FB | 0x80, /*_epsize*/ 4, /*_interval*/ 1)
    };

    return configurationDescriptor;
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
            return USB_STRING_DESCRIPTOR(u"DSpico Speaker Example");
        }
        case STRID_SERIAL:
        {
            return USB_STRING_DESCRIPTOR(u"123456789");
        }
        case STRID_AUDIO_INTERFACE:
        {
            return USB_STRING_DESCRIPTOR(u"DSpico Speakers");
        }
        default:
        {
            return nullptr;
        }
    }
}
