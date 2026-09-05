#pragma once
#include "ThreadIpcService.h"
#include "CameraIpcCommand.h"
#include "IpcChannels.h"

class CameraIpcService : public ThreadIpcService
{
    u32 _threadStack[128];

public:
    CameraIpcService()
        : ThreadIpcService(IPC_CHANNEL_CAMERA, 6, _threadStack, sizeof(_threadStack)) { }

    void Start() override;
    void HandleMessage(u32 data) override;
};
