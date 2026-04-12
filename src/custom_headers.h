#pragma once

// These are static addresses for the screen_fade structs governing the top screen
/*enum fade_status_pointer {
    TOP_SCREEN_GROUND = 0x2389B58,
    TOP_SCREEN_MAIN_MENU = 0x231EAD8,
    BOTTOM_SCREEN_GROUND = 0x2389C68,
    BOTTOM_SCREEN_GROUND_NONBLOCKING = 0x2389B9C,
    BOTTOM_SCREEN_MAIN_MENU = 0x231EB1C
};*/

struct held_buttons {
    bool a : 1;         // 0x0
    bool b : 1;         // 0x1
    bool select : 1;    // 0x2
    bool start : 1;     // 0x3
    bool right : 1;     // 0x4
    bool left : 1;      // 0x5
    bool up : 1;        // 0x6
    bool down : 1;      // 0x7
    bool r : 1;         // 0x8
    bool l : 1;         // 0x9
    bool x : 1;         // 0xA
    bool y : 1;         // 0xB
    uint8_t _padding : 4;
};
ASSERT_SIZE(struct held_buttons, 2);

void UnloadMenuState(void);

void NamePrompt(int param_1, int param_2, int param_3);

void OS_CreateThread(struct thread *thread,
                     void (*func) (void*),
                     void *arg,
                     void *stack,
                     uint32_t stackSize,
                     uint32_t prio);
void OS_WakeupThreadDirect(struct thread *thread);
void OS_SleepThread(undefined* queue);
void OS_SetThreadPriority(struct thread* thread, uint32_t prio);
uint32_t OS_GetThreadPriority(struct thread* thread);
void OS_Sleep(uint32_t msec);

void ReceiveMessageWithHighPrio(void* param_1);

void CallsChangeThreadPriority(void* param_1, int param_2);
void FUN_0200265c(void* param_1);

// From the SDK
static inline bool GX_IsVBlank(void) {
  return (*(uint16_t*) (0x04000000 + 0x004)) & 0x0001;
}