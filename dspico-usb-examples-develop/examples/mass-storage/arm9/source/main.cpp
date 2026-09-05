#include "common.h"
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memExtern.h>
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include "dldiIpc.h"

static rtos_event_t sVblankEvent;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVblankEvent);
}

int main(int argc, char* argv[])
{
    *(vu32*)0x04000000 = 0x10000;
    *(vu16*)0x05000000 = 31 << 10;
    *(vu16*)0x0400006C = 0;

    mem_setDsCartridgeCpu(EXMEMCNT_SLOT1_CPU_ARM7);

    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    rtos_createEvent(&sVblankEvent);

    while (ipc_getArm7SyncBits() != 7);

    if (dldi_init())
    {
        *(vu16*)0x05000000 = (31 << 5);
    }
    else
    {
        *(vu16*)0x05000000 = 31;
    }

    ipc_setArm9SyncBits(6);

    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);

    while (true)
    {
        rtos_waitEvent(&sVblankEvent, true, true);
    }

    return 0;
}