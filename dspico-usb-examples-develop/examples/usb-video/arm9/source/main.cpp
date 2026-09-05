#include "common.h"
#include <nds.h>
#include <stdio.h>
#include <libtwl/gfx/gfx.h>
#include <libtwl/dma/dmaTwl.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memVram.h>
#include <libtwl/mem/memExtern.h>
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include "Camera.h"
#include "IpcChannels.h"

#define FRAME_SIZE                  (256 * 192 * 2)
#define NUMBER_OF_FRAME_BUFFERS     3

static rtos_event_t sVblankEvent;
static rtos_event_t sCaptureEvent;

static u32 sCurrentFrame = 0;
static vu32 sRequestedFrame = 0;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVblankEvent);
}

static void captureIpcMessageHandler(u32 channel, u32 data, void* arg)
{
    sRequestedFrame++;
    rtos_signalEvent(&sCaptureEvent);
}

int main(int argc, char* argv[])
{
    *(vu32*)0x04000000 = 0x10000;
    *(vu16*)0x05000000 = 31 << 5;
    *(vu16*)0x0400006C = 0;

    mem_setDsCartridgeCpu(EXMEMCNT_SLOT1_CPU_ARM7);
    mem_setMainMemoryPriority(EXMEMCNT_MAIN_MEM_PRIO_ARM7);

    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    rtos_createEvent(&sVblankEvent);

    while (ipc_getArm7SyncBits() != 7);

    *(vu16*)0x05000000 = 31 << 10;
    cam_init(false);
    *(vu16*)0x05000000 = 31;

    rtos_createEvent(&sCaptureEvent);
    ipc_setChannelHandler(IPC_CHANNEL_CAPTURE, captureIpcMessageHandler, nullptr);

    ipc_setArm9SyncBits(6);

    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);

	mem_setVramAMapping(MEM_VRAM_AB_LCDC);
    REG_DISPCNT = 0x20000;

    u8* frameBuffer = new(std::align_val_t(32)) u8[FRAME_SIZE * NUMBER_OF_FRAME_BUFFERS];
    ipc_sendFifoMessage(IPC_CHANNEL_CAPTURE, (u32)frameBuffer >> 5);

    while (true)
    {
        rtos_waitEvent(&sCaptureEvent, false, true);
        while (sCurrentFrame != sRequestedFrame)
        {
            cam_dmaStart(1, frameBuffer + FRAME_SIZE * (sCurrentFrame % 3));
            dma_twlWait(1);
            sCurrentFrame++;
            ipc_sendFifoMessage(IPC_CHANNEL_CAPTURE, 0);
        }
    }

    return 0;
}