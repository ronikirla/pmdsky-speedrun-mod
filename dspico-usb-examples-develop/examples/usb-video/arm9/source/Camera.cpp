#include "common.h"
#include <libtwl/sys/swi.h>
#include <libtwl/dma/dmaTwl.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include "IpcChannels.h"
#include "CameraIpcCommand.h"
#include "Camera.h"

#define REG_SCFG_CLK    (*(vu32*)0x04004004)
#define REG_CAM_MCNT    (*(vu16*)0x04004200)
#define REG_CAM_CNT     (*(vu16*)0x04004202)
#define REG_CAM_DAT     (*(vu32*)0x04004204)

static rtos_event_t sEvent;

static void ipcMessageHandler(u32 channel, u32 data, void* arg)
{
    rtos_signalEvent(&sEvent);
}

void cam_init(bool front)
{
    rtos_createEvent(&sEvent);
    ipc_setChannelHandler(IPC_CHANNEL_CAMERA, ipcMessageHandler, nullptr);

    REG_SCFG_CLK |= 0x0004;
    REG_CAM_MCNT = 0x0000;
    swi_waitByLoop(0x1E);
    REG_SCFG_CLK |= 0x0100;
    swi_waitByLoop(0x1E);
    REG_CAM_MCNT = 0x0022;
    swi_waitByLoop(0x2008);
    REG_SCFG_CLK &= ~0x0100;
    REG_CAM_CNT &= ~0x8000;
    REG_CAM_CNT |= 0x0020;
    REG_CAM_CNT = (REG_CAM_CNT & ~0x0300) | 0x0200;
    REG_CAM_CNT |= 0x0400;
    REG_CAM_CNT |= 0x0800;
    REG_SCFG_CLK |= 0x0100;
    swi_waitByLoop(0x14);

    ipc_sendFifoMessage(IPC_CHANNEL_CAMERA, front ? CAMERA_IPC_CMD_INIT_FRONT : CAMERA_IPC_CMD_INIT_BACK);
    rtos_waitEvent(&sEvent, false, true);

    REG_SCFG_CLK &= ~0x0100;
    REG_SCFG_CLK |= 0x0100;

    ipc_sendFifoMessage(IPC_CHANNEL_CAMERA, CAMERA_IPC_CMD_ACTIVATE);
    rtos_waitEvent(&sEvent, false, true);
}

void cam_stop()
{
    ipc_sendFifoMessage(IPC_CHANNEL_CAMERA, CAMERA_IPC_CMD_DEACTIVATE);
    rtos_waitEvent(&sEvent, false, true);
}

void cam_switch()
{
    ipc_sendFifoMessage(IPC_CHANNEL_CAMERA, CAMERA_IPC_CMD_SWITCH);
    rtos_waitEvent(&sEvent, false, true);
}

void cam_dmaStart(int dma, void* dst)
{
    // REG_CAM_CNT |= 0x2000;
    REG_CAM_CNT &= ~0x2000;
    REG_CAM_CNT = (REG_CAM_CNT & ~0x000F) | 0x0003;
    REG_CAM_CNT |= 0x0020;
    REG_CAM_CNT |= 0x8000;

    dma_twl_config_t dmaConfig =
    {
        .src = (const void*)0x04004204,
        .dst = dst,
        .totalWordCount = 0x6000,
        .wordCount = 0x200,
        .blockInterval = 2,
        .fillData = 0,
        .control = 0x8B044000
    };
    dma_twlSetParams(dma, &dmaConfig);
}
