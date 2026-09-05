#include "common.h"
#include <libtwl/i2c/i2c.h>
#include "cam_ops.h"
#include "CameraIpcService.h"

#define DEFAULT_COARSE_INTEGRATION_TIME     0x10

void CameraIpcService::Start()
{
    initParams(DEFAULT_COARSE_INTEGRATION_TIME);
    ThreadIpcService::Start();
}

void CameraIpcService::HandleMessage(u32 data)
{
    switch (data)
    {
        case CAMERA_IPC_CMD_INIT_FRONT:
        {
            aptinaInit(I2C_DEVICE_CAMERA_FRONT);
            SendResponseMessage(1);
            break;
        }
        case CAMERA_IPC_CMD_INIT_BACK:
        {
            aptinaInit(I2C_DEVICE_CAMERA_BACK);
            SendResponseMessage(1);
            break;
        }
        case CAMERA_IPC_CMD_ACTIVATE:
        {
            aptinaActivate();
            SendResponseMessage(1);
            break;
        }
        case CAMERA_IPC_CMD_DEACTIVATE:
        {
            aptinaDeactivate();
            SendResponseMessage(1);
            break;
        }
        case CAMERA_IPC_CMD_SWITCH:
        {
            aptinaSwitch();
            SendResponseMessage(1);
            break;
        }
    }
}
