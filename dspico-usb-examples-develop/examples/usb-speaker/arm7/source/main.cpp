#include "common.h"
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/sound/soundChannel.h>
#include <libtwl/timer/timer.h>
#include <libtwl/sound/sound.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/sys/sysPower.h>
#include <libtwl/sio/sioRtc.h>
#include <libtwl/sio/sio.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memSwap.h>
#include <libtwl/i2c/i2cMcu.h>
#include <libtwl/spi/spiPmic.h>
#include "ExitMode.h"
#include "Arm7State.h"
#include "tusb.h"
#include "usb_descriptors.h"

const uint32_t sample_rates[] = { 44100 };

uint32_t current_sample_rate  = 44100;

#define N_SAMPLE_RATES  TU_ARRAY_SIZE(sample_rates)

enum
{
    VOLUME_CTRL_0_DB = 0,
    VOLUME_CTRL_10_DB = 2560,
    VOLUME_CTRL_20_DB = 5120,
    VOLUME_CTRL_30_DB = 7680,
    VOLUME_CTRL_40_DB = 10240,
    VOLUME_CTRL_50_DB = 12800,
    VOLUME_CTRL_60_DB = 15360,
    VOLUME_CTRL_70_DB = 17920,
    VOLUME_CTRL_80_DB = 20480,
    VOLUME_CTRL_90_DB = 23040,
    VOLUME_CTRL_100_DB = 25600,
    VOLUME_CTRL_SILENCE = 0x8000,
};

// Audio controls
// Current states
int8_t mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];       // +1 for master channel 0
int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];    // +1 for master channel 0

// Buffer for speaker data
int32_t spk_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 4];
// Speaker data size received in the last frame
volatile int spk_data_size;
// Resolution per format
const uint8_t resolutions_per_format[CFG_TUD_AUDIO_FUNC_1_N_FORMATS] = { CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX };
// Current resolution, update on format change
volatile uint8_t current_resolution;

#define AUDIO_BUFFER_LENGTH     2048

static bool sAudioStarted;

#define AUDIO_STREAM_PLAYER_TIMER           3
#define AUDIO_STREAM_PLAYER_SAFETY_BLOCKS   4
#define AUDIO_STREAM_PLAYER_THREAD_PRIORITY 15
#define AUDIO_STREAM_PLAYER_RING_BLOCKS     32
#define AUDIO_STREAM_PLAYER_BLOCK_SAMPLES   64

static rtos_event_t sAudioBlockEvent;
static rtos_thread_t sAudioThread;
static u32 sAudioThreadStack[512];

static s16 sAudioRingL[AUDIO_STREAM_PLAYER_RING_BLOCKS][AUDIO_STREAM_PLAYER_BLOCK_SAMPLES] alignas(4);
static s16 sAudioRingR[AUDIO_STREAM_PLAYER_RING_BLOCKS][AUDIO_STREAM_PLAYER_BLOCK_SAMPLES] alignas(4);

static volatile u8 sReadBlock;
static volatile u8 sWriteBlock;

rtos_mutex_t gCardMutex;

static rtos_thread_t sUsbThread;
static u32 sUsbThreadStack[512];

static rtos_event_t sVBlankEvent;
static ExitMode sExitMode;
static Arm7State sState;
static volatile u8 sMcuIrqFlag = false;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVBlankEvent);
}

static void mcuIrq(u32 irq2Mask)
{
    sMcuIrqFlag = true;
}

static void checkMcuIrq(void)
{
    // mcu only exists in DSi mode
    if (isDSiMode())
    {
        // check and ack the flag atomically
        if (mem_swapByte(false, &sMcuIrqFlag))
        {
            // check the irq mask
            u32 irqMask = mcu_getIrqMask();
            if (irqMask & MCU_IRQ_RESET)
            {
                // power button was released
                sExitMode = ExitMode::Reset;
                sState = Arm7State::ExitRequested;
            }
            else if (irqMask & MCU_IRQ_POWER_OFF)
            {
                // power button was held long to trigger a power off
                sExitMode = ExitMode::PowerOff;
                sState = Arm7State::ExitRequested;
            }
        }
    }
}

static void initializeVBlankIrq()
{
    rtos_createEvent(&sVBlankEvent);
    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);
}

static void usbThreadMain(void* arg)
{
    while (true)
    {
        tud_task();
    }
}

static void initializeArm7()
{
    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    // clear sound registers
    dmaFillWords(0, (void*)0x04000400, 0x100);

    pmic_setAmplifierEnable(true);
    sys_setSoundPower(true);

    readUserSettings();
    pmic_setPowerLedBlink(PMIC_CONTROL_POWER_LED_BLINK_NONE);

    sio_setGpioSiIrq(false);
    sio_setGpioMode(RCNT0_L_MODE_GPIO);

    rtc_init();

    snd_setMasterVolume(127);
    snd_setMasterEnable(true);

    initializeVBlankIrq();

    if (isDSiMode())
    {
        rtos_setIrq2Func(RTOS_IRQ2_MCU, mcuIrq);
        rtos_enableIrq2Mask(RTOS_IRQ2_MCU);
    }

    tusb_rhport_init_t dev_init =
    {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    sReadBlock = 0;
    sWriteBlock = 0;
    rtos_createThread(&sUsbThread, 3, usbThreadMain, NULL, sUsbThreadStack, sizeof(sUsbThreadStack));
    rtos_wakeupThread(&sUsbThread);

    ipc_setArm7SyncBits(7);
}

static void updateArm7IdleState()
{
    checkMcuIrq();

    if (sState == Arm7State::ExitRequested)
    {
        snd_setMasterVolume(0); // mute sound
    }
}

static bool performExit(ExitMode exitMode)
{
    switch (exitMode)
    {
        case ExitMode::Reset:
        {
            mcu_setWarmBootFlag(true);
            mcu_hardReset();
            break;
        }
        case ExitMode::PowerOff:
        {
            pmic_shutdown();
            break;
        }
    }

    while (true); // wait infinitely for exit
}

static void updateArm7ExitRequestedState()
{
    performExit(sExitMode);
}

static void updateArm7()
{
    switch (sState)
    {
        case Arm7State::Idle:
        {
            updateArm7IdleState();
            break;
        }
        case Arm7State::ExitRequested:
        {
            updateArm7ExitRequestedState();
            break;
        }
    }
}

// Helper for clock get requests
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
{
    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
    {
        if (request->bRequest == AUDIO_CS_REQ_CUR)
        {
            audio_control_cur_4_t curf = { (int32_t) tu_htole32(current_sample_rate) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
        }
        else if (request->bRequest == AUDIO_CS_REQ_RANGE)
        {
            audio_control_range_4_n_t(N_SAMPLE_RATES) rangef =
            {
                .wNumSubRanges = tu_htole16(N_SAMPLE_RATES)
            };
            for (uint8_t i = 0; i < N_SAMPLE_RATES; i++)
            {
                rangef.subrange[i].bMin = (int32_t) sample_rates[i];
                rangef.subrange[i].bMax = (int32_t) sample_rates[i];
                rangef.subrange[i].bRes = 0;
            }

            return tud_audio_buffer_and_schedule_control_xfer(rhport, (const tusb_control_request_t*)request, &rangef, sizeof(rangef));
        }
    }
    else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID
        && request->bRequest == AUDIO_CS_REQ_CUR)
    {
        audio_control_cur_1_t cur_valid = { .bCur = 1 };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (const tusb_control_request_t*)request, &cur_valid, sizeof(cur_valid));
    }
    return false;
}

// Helper for clock set requests
static bool tud_audio_clock_set_request(u8 rhport, const audio_control_request_t* request, const u8* buf)
{
    (void)rhport;

    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
    {
        current_sample_rate = (u32)((const audio_control_cur_4_t*)buf)->bCur;
        return true;
    }
    else
    {
        return false;
    }
}

// Helper for feature unit get requests
static bool tud_audio_feature_unit_get_request(u8 rhport, const audio_control_request_t* request)
{
    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR)
    {
        audio_control_cur_1_t mute1 = { .bCur = mute[request->bChannelNumber] };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (const tusb_control_request_t*)request, &mute1, sizeof(mute1));
    }
    else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
    {
        if (request->bRequest == AUDIO_CS_REQ_RANGE)
        {
            audio_control_range_2_n_t(1) range_vol =
            {
                tu_htole16(1),
                { { tu_htole16(-VOLUME_CTRL_50_DB), tu_htole16(VOLUME_CTRL_0_DB), tu_htole16(256) } }
            };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (const tusb_control_request_t*)request, &range_vol, sizeof(range_vol));
        }
        else if (request->bRequest == AUDIO_CS_REQ_CUR)
        {
            audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(volume[request->bChannelNumber]) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (const tusb_control_request_t*)request, &cur_vol, sizeof(cur_vol));
        }
    }
    return false;
}

// Helper for feature unit set requests
static bool tud_audio_feature_unit_set_request(u8 rhport, const audio_control_request_t* request, const u8* buf)
{
    (void)rhport;

    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE)
    {
        mute[request->bChannelNumber] = ((const audio_control_cur_1_t*)buf)->bCur;
        return true;
    }
    else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
    {
        volume[request->bChannelNumber] = ((const audio_control_cur_2_t*)buf)->bCur;
        return true;
    }
    else
    {
        return false;
    }
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(u8 rhport, const tusb_control_request_t* p_request)
{
    auto request = (const audio_control_request_t*)p_request;
    switch (request->bEntityID)
    {
        case UAC2_ENTITY_CLOCK:
        {
            return tud_audio_clock_get_request(rhport, request);
        }
        case UAC2_ENTITY_FEATURE_UNIT:
        {
            return tud_audio_feature_unit_get_request(rhport, request);
        }
        default:
        {
            return false;
        }
    }
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(u8 rhport, const tusb_control_request_t* p_request, u8* buf)
{
    auto request = (const audio_control_request_t*)p_request;
    switch (request->bEntityID)
    {
        case UAC2_ENTITY_FEATURE_UNIT:
        {
            return tud_audio_feature_unit_set_request(rhport, request, buf);
        }
        case UAC2_ENTITY_CLOCK:
        {
            return tud_audio_clock_set_request(rhport, request, buf);
        }
        default:
        {
            return false;
        }
    }
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, const tusb_control_request_t* p_request)
{
    (void)rhport;

    const u8 itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    const u8 alt = tu_u16_low(tu_le16toh(p_request->wValue));

    (void)itf;
    (void)alt;

    if (sAudioStarted)
    {
        // Stop audio playback
        snd_stopChannel(0);
        snd_stopChannel(1);
        tmr_stop(AUDIO_STREAM_PLAYER_TIMER);
        rtos_disableIrqMask(RTOS_IRQ_TIMER(AUDIO_STREAM_PLAYER_TIMER));
        rtos_ackIrqMask(RTOS_IRQ_TIMER(AUDIO_STREAM_PLAYER_TIMER));
        rtos_sleepThread(&sAudioThread);
        sReadBlock = 0;
        sWriteBlock = 0;
        sAudioStarted = false;
    }

    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, const tusb_control_request_t* p_request)
{
    (void)rhport;
    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

    spk_data_size = 0;
    if (alt != 0)
    {
        current_resolution = resolutions_per_format[alt - 1];
    }

    return true;
}

static void fillRingBlock(u32 block)
{
    s16* blockPtrL = &sAudioRingL[block][0];
    s16* blockPtrR = &sAudioRingR[block][0];

    tud_audio_read(spk_buf, AUDIO_STREAM_PLAYER_BLOCK_SAMPLES * 4);

    s16* src = (s16*)spk_buf;
    s16* limit = (s16*)spk_buf + AUDIO_STREAM_PLAYER_BLOCK_SAMPLES * 2;
    while (src < limit)
    {
        *blockPtrL++ = *src++;
        *blockPtrR++ = *src++;
    }
}

static void audioThreadMain(void* arg)
{
    do
    {
        bool doUpdate = true;
        while (doUpdate)
        {
            u32 writeBlock = sWriteBlock;
            int freeBlocks = sReadBlock - writeBlock - 1;
            if (freeBlocks < 0)
            {
                freeBlocks += AUDIO_STREAM_PLAYER_RING_BLOCKS;
            }

            if (freeBlocks > AUDIO_STREAM_PLAYER_SAFETY_BLOCKS && tud_audio_available() >= AUDIO_STREAM_PLAYER_BLOCK_SAMPLES * 4)
            {
                fillRingBlock(writeBlock);
                if (++writeBlock == AUDIO_STREAM_PLAYER_RING_BLOCKS)
                {
                    writeBlock = 0;
                }
                sWriteBlock = writeBlock;
            }
            else
            {
                doUpdate = false;
            }
        }

        rtos_waitEvent(&sAudioBlockEvent, false, true);
    } while(true);
}

void tud_audio_feedback_params_cb(u8 func_id, u8 alt_itf, audio_feedback_params_t* feedback_param)
{
    (void)func_id;
    (void)alt_itf;
    feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    feedback_param->sample_freq = current_sample_rate;
}

bool tud_audio_rx_done_pre_read_cb(u8 rhport, u16 n_bytes_received, u8 func_id, u8 ep_out, u8 cur_alt_setting)
{
    (void)rhport;
    (void)func_id;
    (void)ep_out;
    (void)cur_alt_setting;

    while (!sAudioStarted && tud_audio_available() >= AUDIO_STREAM_PLAYER_BLOCK_SAMPLES * 4)
    {
        fillRingBlock(sWriteBlock);
        sWriteBlock++;

        if (sWriteBlock == 8)
        {
            u32 timer = -((33513982 + current_sample_rate) / (current_sample_rate * 2));
            REG_SOUNDxSAD(0) = (u32)&sAudioRingL[0][0];
            REG_SOUNDxSAD(1) = (u32)&sAudioRingR[0][0];
            REG_SOUNDxTMR(0) = timer;
            REG_SOUNDxTMR(1) = timer;
            REG_SOUNDxPNT(0) = 0;
            REG_SOUNDxPNT(1) = 0;
            REG_SOUNDxLEN(0) = sizeof(sAudioRingL) >> 2;
            REG_SOUNDxLEN(1) = sizeof(sAudioRingR) >> 2;
            REG_SOUNDxCNT(0) = SOUNDCNT_VOLUME(127) | SOUNDCNT_MODE_LOOP | SOUNDCNT_FORMAT_PCM16;
            REG_SOUNDxCNT(1) = SOUNDCNT_VOLUME(127) | SOUNDCNT_PAN(127) | SOUNDCNT_MODE_LOOP | SOUNDCNT_FORMAT_PCM16;

            rtos_createEvent(&sAudioBlockEvent);
            rtos_disableIrqMask(RTOS_IRQ_TIMER(AUDIO_STREAM_PLAYER_TIMER));
            rtos_ackIrqMask(RTOS_IRQ_TIMER(AUDIO_STREAM_PLAYER_TIMER));
            rtos_setIrqFunc(RTOS_IRQ_TIMER(AUDIO_STREAM_PLAYER_TIMER), [] (u32 irqMask)
            {
                u32 readBlock = sReadBlock;
                if (++readBlock == AUDIO_STREAM_PLAYER_RING_BLOCKS)
                {
                    readBlock = 0;
                }
                sReadBlock = readBlock;
                rtos_signalEvent(&sAudioBlockEvent);
            });
            tmr_configure(AUDIO_STREAM_PLAYER_TIMER, TMCNT_H_CLK_SYS_DIV_64, timer << 1, true);

            rtos_createThread(&sAudioThread, AUDIO_STREAM_PLAYER_THREAD_PRIORITY, audioThreadMain,
                nullptr, sAudioThreadStack, sizeof(sAudioThreadStack));

            tmr_start(AUDIO_STREAM_PLAYER_TIMER);
            rtos_enableIrqMask(RTOS_IRQ_TIMER(AUDIO_STREAM_PLAYER_TIMER));

            sAudioStarted = true;
            rtos_wakeupThread(&sAudioThread);

            snd_startChannel(0);
            snd_startChannel(1);
            break;
        }
    }

    return true;
}

int main()
{
    sState = Arm7State::Idle;
    initializeArm7();

    while (true)
    {
        rtos_waitEvent(&sVBlankEvent, true, true);
        updateArm7();
    }

    return 0;
}
