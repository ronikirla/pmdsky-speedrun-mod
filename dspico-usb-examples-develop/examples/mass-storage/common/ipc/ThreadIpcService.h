#pragma once
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include "IpcService.h"

class ThreadIpcService : public IpcService
{
    rtos_thread_t _thread;
    rtos_event_t _event;
    u32* _stack;
    u32 _stackSize;
    u8 _priority;
    bool _messageValid = false;
    u32 _message;

    void ThreadMain();

public:
    ThreadIpcService(u32 ipcChannel, u8 priority, u32* stack, u32 stackSize)
        : IpcService(ipcChannel), _stack(stack), _stackSize(stackSize), _priority(priority) { }

    void Start() override;
    void OnMessageReceived(u32 data) override;

    virtual void HandleMessage(u32 data);
};
