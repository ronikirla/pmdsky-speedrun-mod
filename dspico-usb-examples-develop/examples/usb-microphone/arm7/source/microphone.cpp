#include "common.h"
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/spi/spiCodec.h>
#include <libtwl/sound/twlMicrophone.h>
#include <libtwl/sound/twlI2s.h>
#include <libtwl/sys/swi.h>
#include "tusb.h"
#include "usb_descriptors.h"
#include "microphone.h"

#define AUDIO_BLOCK_SIZE_IN_BYTES   32
#define NUMBER_OF_AUDIO_BUFFERS     128

static s16 sAudioBuffer[NUMBER_OF_AUDIO_BUFFERS][16];
static volatile int sReadBlock;
static volatile int sWriteBlock;

static bool sCaptureStarted = false;
static int sOffset = 0;

static bool sChannelMute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1]; // +1 for master channel 0
static uint16_t sChannelVolume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1]; // +1 for master channel 0

void mic_initialize()
{
    twlmic_stop();
    REG_I2SCNT = I2SCNT_MIX_RATIO_DSP_0_NITRO_8 | I2SCNT_FREQUENCY_32728_HZ;
    codec_setPage(CODEC_PAGE_0);
    {
        codec_writeRegister(CODEC_REG_PAGE0_DAC_NDAC_VAL, 0x87);
        codec_writeRegister(CODEC_REG_PAGE0_ADC_NADC_VAL, 0x87);
        codec_writeRegister(CODEC_REG_PAGE0_PLL_J, 21);
    }
    REG_I2SCNT |= I2SCNT_ENABLE;
    codec_setPage(CODEC_PAGE_1);
    {
        codec_writeRegister(CODEC_REG_PAGE1_MICBIAS, 3);
    }
    bool adcOn, dacOn;
    codec_setPage(CODEC_PAGE_0);
    {
        adcOn = codec_readRegister(CODEC_REG_PAGE0_ADC_DIGITAL_MIC) & 0x80;
        dacOn = codec_readRegister(CODEC_REG_PAGE0_DAC_DATA_PATH_SETUP) & 0xC0;
        codec_writeRegister(CODEC_REG_PAGE0_ADC_DIGITAL_MIC, 0x80);
        if (!adcOn || !dacOn)
        {
            swi_waitByLoop(0x28E91F); // 20ms
        }
        codec_writeRegister(CODEC_REG_PAGE0_ADC_DIGITAL_VOLUME_CONTROL_FINE_ADJUST, 0);
        codec_writeRegister(CODEC_REG_PAGE0_AGC_CONTROL_1, 0);
    }
    codec_setPage(CODEC_PAGE_1);
    {
        sChannelVolume[0] = 40; // dB
        codec_writeRegister(CODEC_REG_PAGE1_MIC_PGA, sChannelVolume[0] * 2); // gain
    }
}

static void micIrq(u32 irq2Mask)
{
    u32 data[8];
    data[0] = REG_MIC_FIFO;
    data[1] = REG_MIC_FIFO;
    data[2] = REG_MIC_FIFO;
    data[3] = REG_MIC_FIFO;
    data[4] = REG_MIC_FIFO;
    data[5] = REG_MIC_FIFO;
    data[6] = REG_MIC_FIFO;
    data[7] = REG_MIC_FIFO;
    int writeBlock = sWriteBlock;
    int nextWriteBlock = (writeBlock + 1) % NUMBER_OF_AUDIO_BUFFERS;
    if (nextWriteBlock != sReadBlock)
    {
        memcpy(&sAudioBuffer[writeBlock][0], data, sizeof(data));
        sWriteBlock = nextWriteBlock;
    }
}

static void startMicrophoneCapture()
{
    sReadBlock = 0;
    sWriteBlock = 0;
    sOffset = 0;
    twlmic_stop();
    twlmic_configure(MICCNT_FORMAT_NORMAL, MICCNT_RATE_DIV_1, MICCNT_IRQ_HALF_OVERFLOW);
    twlmic_clearFifo();
    rtos_setIrq2Func(RTOS_IRQ2_MIC, micIrq);
    rtos_ackIrq2Mask(RTOS_IRQ2_MIC);
    rtos_enableIrq2Mask(RTOS_IRQ2_MIC);
    twlmic_start();
    sCaptureStarted = true;
}

static void stopMicrophoneCapture()
{
    rtos_disableIrq2Mask(RTOS_IRQ2_MIC);
    twlmic_stop();
    rtos_ackIrq2Mask(RTOS_IRQ2_MIC);
    sCaptureStarted = false;
}

static bool handleMicInputTerminalGetRequest(u8 rhport, const tusb_control_request_t* p_request)
{
    u8 ctrlSel = TU_U16_HIGH(p_request->wValue);
    switch (ctrlSel)
    {
        case AUDIO_TE_CTRL_CONNECTOR: // Get terminal connector
        {
            audio_desc_channel_cluster_t ret;
            ret.bNrChannels = 1;
            ret.bmChannelConfig = (audio_channel_config_t)0;
            ret.iChannelNames = 0;
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*) &ret, sizeof(ret));
        }
    }

    return false;
}

static bool handleFeatureUnitGetRequest(u8 rhport, const tusb_control_request_t* p_request)
{
    u8 channelNum = TU_U16_LOW(p_request->wValue);
    u8 ctrlSel = TU_U16_HIGH(p_request->wValue);
    switch (ctrlSel)
    {
        case AUDIO_FU_CTRL_MUTE: // Get Mute of channel
        {
            return tud_control_xfer(rhport, p_request, &sChannelMute[channelNum], 1);
        }
        case AUDIO_FU_CTRL_VOLUME:
        {
            switch (p_request->bRequest)
            {
                case AUDIO_CS_REQ_CUR: // Get Volume of channel
                {
                    return tud_control_xfer(rhport, p_request, &sChannelVolume[channelNum], sizeof(sChannelVolume[channelNum]));
                }
                case AUDIO_CS_REQ_RANGE: // Get Volume range of channel
                {
                    audio_control_range_2_n_t(1) ret;
                    ret.wNumSubRanges = 1;
                    ret.subrange[0].bMin = 0;  // 0 dB
                    ret.subrange[0].bMax = 59; // +59 dB
                    ret.subrange[0].bRes = 1;  // 1 dB steps
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*) &ret, sizeof(ret));
                }
            }
            break;
        }
    }

    return false;
}

static bool handleFeatureUnitSetRequest(u8 rhport, const tusb_control_request_t* p_request, u8* pBuff)
{
    u8 channelNum = TU_U16_LOW(p_request->wValue);
    u8 ctrlSel = TU_U16_HIGH(p_request->wValue);
    switch (ctrlSel)
    {
        case AUDIO_FU_CTRL_MUTE: // Set Mute of channel
        {
            sChannelMute[channelNum] = ((audio_control_cur_1_t*)pBuff)->bCur;
            return true;
        }
        case AUDIO_FU_CTRL_VOLUME: // Set Volume of channel
        {
            // Request uses format layout 2
            sChannelVolume[channelNum] = (uint16_t) ((audio_control_cur_2_t*)pBuff)->bCur;
            codec_setPage(CODEC_PAGE_1);
            {
                codec_writeRegister(CODEC_REG_PAGE1_MIC_PGA, sChannelVolume[channelNum] * 2); // gain
            }
            return true;
        }
    }

    return false;
}

static bool handleClockGetRequest(u8 rhport, const tusb_control_request_t* p_request)
{
    u8 ctrlSel = TU_U16_HIGH(p_request->wValue);
    switch (ctrlSel)
    {
        case AUDIO_CS_CTRL_SAM_FREQ:
        {
            switch (p_request->bRequest)
            {
                case AUDIO_CS_REQ_CUR: // Get Sample Freq.
                {
                    uint32_t sampFreq = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
                    return tud_control_xfer(rhport, p_request, &sampFreq, sizeof(sampFreq));
                }
                case AUDIO_CS_REQ_RANGE: // Get Sample Freq. range
                {
                    audio_control_range_4_n_t(1) sampleFreqRng;
                    sampleFreqRng.wNumSubRanges = 1;
                    sampleFreqRng.subrange[0].bMin = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
                    sampleFreqRng.subrange[0].bMax = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
                    sampleFreqRng.subrange[0].bRes = 0;
                    return tud_control_xfer(rhport, p_request, &sampleFreqRng, sizeof(sampleFreqRng));
                }
            }
            break;
        }
        case AUDIO_CS_CTRL_CLK_VALID: // Get Sample Freq. valid
        {
            uint8_t clkValid = 1;
            return tud_control_xfer(rhport, p_request, &clkValid, sizeof(clkValid));
        }
    }

    return false;
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(u8 rhport, const tusb_control_request_t* p_request)
{
    u8 entityId = TU_U16_HIGH(p_request->wIndex);
    switch (entityId)
    {
        case UAC2_ENTITY_MIC_INPUT_TERMINAL:
        {
            return handleMicInputTerminalGetRequest(rhport, p_request);
        }
        case UAC2_ENTITY_FEATURE_UNIT:
        {
            return handleFeatureUnitGetRequest(rhport, p_request);
        }
        case UAC2_ENTITY_CLOCK:
        {
            return handleClockGetRequest(rhport, p_request);
        }
    }

    return false;
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(u8 rhport, const tusb_control_request_t* p_request, u8* pBuff)
{
    u8 entityId = TU_U16_HIGH(p_request->wIndex);
    switch (entityId)
    {
        case UAC2_ENTITY_FEATURE_UNIT:
        {
            return handleFeatureUnitSetRequest(rhport, p_request, pBuff);
        }
    }

    return false;
}

bool tud_audio_tx_done_pre_load_cb(u8 rhport, u8 itf, u8 ep_in, u8 cur_alt_setting)
{
    if (!sCaptureStarted)
    {
        startMicrophoneCapture();
    }

    while (sReadBlock != sWriteBlock)
    {
        int bytesWritten = tud_audio_write(((u8*)&sAudioBuffer[sReadBlock][0]) + sOffset, AUDIO_BLOCK_SIZE_IN_BYTES - sOffset);
        int offset = AUDIO_BLOCK_SIZE_IN_BYTES - bytesWritten;
        sOffset = offset % AUDIO_BLOCK_SIZE_IN_BYTES;
        if (offset > 0)
        {
            // Could not write entire block
            break;
        }
        sReadBlock = (sReadBlock + 1) % NUMBER_OF_AUDIO_BUFFERS;
    }

  return true;
}

bool tud_audio_set_itf_close_EP_cb(u8 rhport, const tusb_control_request_t* p_request)
{
    stopMicrophoneCapture();
    return true;
}
