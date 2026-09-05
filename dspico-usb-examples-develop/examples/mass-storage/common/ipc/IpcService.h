#pragma once
#include <libtwl/ipc/ipcFifoSystem.h>

class IpcService
{
    const u32 _ipcChannel;
protected:
    explicit IpcService(u32 ipcChannel)
        : _ipcChannel(ipcChannel) { }

    void SendResponseMessage(u32 data) const
    {
        ipc_sendFifoMessage(_ipcChannel, data);
    }

public:
    virtual void Start();
    virtual void OnMessageReceived(u32 data) = 0;
};
