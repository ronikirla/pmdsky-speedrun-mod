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
#include "tusb_config.h"
#include "usb_descriptors.h"
#include "CameraIpcService.h"

#define FRAME_SIZE                  (FRAME_WIDTH * FRAME_HEIGHT * 2)
#define NUMBER_OF_FRAME_BUFFERS     3

rtos_mutex_t gCardMutex;
rtos_mutex_t gI2cMutex;

static CameraIpcService sCameraIpcService;

static rtos_thread_t sUsbThread;
static u32 sUsbThreadStack[512];

static rtos_event_t sVBlankEvent;
static rtos_event_t sCaptureEvent;
static ExitMode sExitMode;
static Arm7State sState;
static volatile u8 sMcuIrqFlag = false;

static u32 sCurrentFrame = 0;
static vu32 sCapturedFrame = 0xFFFFFFFF;
static u8* sFrameBuffer = nullptr;

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
            rtos_lockMutex(&gI2cMutex);
            u32 irqMask = mcu_getIrqMask();
            rtos_unlockMutex(&gI2cMutex);
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

static void captureIpcMessageHandler(u32 channel, u32 data, void* arg)
{
    if (sFrameBuffer == nullptr)
    {
        sFrameBuffer = (u8*)(data << 5);
    }
    else
    {
        sCapturedFrame++;
        rtos_signalEvent(&sCaptureEvent);
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
        sCameraIpcService.Start();
        rtos_setIrq2Func(RTOS_IRQ2_MCU, mcuIrq);
        rtos_enableIrq2Mask(RTOS_IRQ2_MCU);
    }

    rtos_createEvent(&sCaptureEvent);
    ipc_setChannelHandler(IPC_CHANNEL_CAPTURE, captureIpcMessageHandler, nullptr);

    ipc_setArm7SyncBits(7);
    while (ipc_getArm9SyncBits() != 6)
    {
        rtos_waitEvent(&sVBlankEvent, true, true);
    }

    // request two frames in advance
    ipc_sendFifoMessage(IPC_CHANNEL_CAPTURE, 0);
    ipc_sendFifoMessage(IPC_CHANNEL_CAPTURE, 0);

    tusb_rhport_init_t dev_init =
    {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    rtos_createThread(&sUsbThread, 8, usbThreadMain, NULL, sUsbThreadStack, sizeof(sUsbThreadStack));
    rtos_wakeupThread(&sUsbThread);
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
            rtos_lockMutex(&gI2cMutex);
            mcu_setWarmBootFlag(true);
            mcu_hardReset();
            rtos_unlockMutex(&gI2cMutex);
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

void tud_video_frame_xfer_complete_cb(u8 ctl_idx, u8 stm_idx)
{
    (void) ctl_idx;
    (void) stm_idx;
}

int tud_video_commit_cb(u8 ctl_idx, u8 stm_idx, const video_probe_and_commit_control_t* parameters)
{
    (void) ctl_idx;
    (void) stm_idx;
    return VIDEO_ERROR_NONE;
}

int main()
{
    sState = Arm7State::Idle;
    initializeArm7();

    while (true)
    {
        if (tud_video_n_streaming(0, 0))
        {
            rtos_waitEvent(&sCaptureEvent, false, true);
            while (sCurrentFrame != (sCapturedFrame + 1))
            {
                updateArm7();
                tud_video_n_frame_xfer(0, 0,
                    sFrameBuffer + (sCurrentFrame % NUMBER_OF_FRAME_BUFFERS) * FRAME_SIZE, FRAME_SIZE);
                sCurrentFrame++;
                ipc_sendFifoMessage(IPC_CHANNEL_CAPTURE, 0);
            }
        }
        else
        {
            rtos_waitEvent(&sVBlankEvent, true, true);
            updateArm7();
        }
    }

    return 0;
}
