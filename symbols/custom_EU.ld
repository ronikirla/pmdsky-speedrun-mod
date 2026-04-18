/* !file overlay29 */
/* ApplyMoveEffect = ?????; unknown */
ApplyMoveEffectHookAddr = 0x023302E4;
ApplyMoveEffectJumpAddr = 0x0233310C;
ApplyItemEffectHookAddr = 0x0231C434;
ApplyItemEffectJumpAddr = 0x0231D574;

/* Add your own symbols here... */

/* !file arm9 */
ChangeGlobalBorderColor = 0x02027D74;

SetBrightnessMain = 0x022eb1e0;
SetBrightnessSub = 0x022eb214;
SetBrightnessNonblocking = 0x022eb14c;
SetBrightness = 0x02008f3c;
UnloadMenuState = 0x02034a04;

NamePrompt = 0x02036aa8;

WINDOW_LIST = 0x022a921c;

OS_CreateThread = 0x020798f8;
OS_WakeupThreadDirect = 0x02079cd8;
OS_SleepThread = 0x02079c20;
OS_Sleep = 0x02079eac;
OS_SetThreadPriority = 0x02079dfc;
OS_GetThreadPriority = 0x02079ea4;

GetThreadPriority = 0x02079ea4;
SetThreadPriority = 0x02079dfc;

ReceiveMessageWithHighPrio = 0x02002a44;

CallsChangeThreadPriority = 0x02002628;

DAT_02003aac = 0x02003aac;
FUN_0200265c = 0x0200265c;

Cardi_RequestStreamCommand = 0x02083db0;