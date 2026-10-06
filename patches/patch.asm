.nds
.include "symbols.asm"

.open "arm9.bin", arm9_start
    // Wait until overlay 36 is loaded before running our custom RNG function
    .org 0x020036b0
        bl @DelayRand16BitIfAdvancesNotLocked
    .org 0x020036e0
        bl @DelayRand16BitIfAdvancesNotLocked

    .org 0x020492b0
        bl HijackNoteSaveBaseAndSetSaveVariable
    .org 0x02049508
        bl HijackNoteSaveBaseAndUnsetSaveVariableAndAlsoAddTimePenaltyByTheWay
    .org 0x02051150
        b CheckIfShouldIncrementPlayTimer

    .org 0x020662b8
        bl HijackNoteLoadBaseAndLoadIGT
    // Remove the busy sleep from G3X_Reset since we handle the frame synchronization separately
    // in a way that lets us sometimes skip the wait to squeeze more processing time.
    .org 0x02077c78
        nop

    // Trampoline for waiting until overlay 36 is loaded before running our custom version of WaitTillVBlank
    .org 0x02003a40
        b @DelayWaitTillVBlankTrampoline
    @WaitTillVBlank:

    // Trampoline to inject custom code to VCount 0 alarm interrupt handler
    .org 0x02003764
        b 0x02094850
    @vcount0:
    // Wait until overlay 36 is loaded and then an extra 16 frames to make sure
    // we are safe to run our code
    .org 0x02094850
        stmdb sp!,{r0-r12,lr}
        ldr r1, [@delay]
        cmp r1, 0
        bne @delay_not_done

        bl WakeupThreads
        b @exit

    @delay_not_done:
        ldr r0, [@overlay36_loaded]
        ldr r0, [r0]
        cmp r0, 0
        subne r1, 1
        str r1, [@delay]
        cmp r1, 0
        bleq InitThreads

    @exit:
        ldmia sp!,{r0-r12,lr}
        add r2, r2, 1 // Run original instruction from the trampoline
        b @vcount0
    @overlay36_loaded:
        .word 0x020047BC
    @delay:
        .word 0x00000010
    // Wait for Overlay36
    .org 0x02094910
    @DelayWaitTillVBlankTrampoline:
        stmdb sp!,{r7}
        ldr r7, [@delay]
        cmp r7, 0
        ldmia sp!,{r7}
        beq CustomWaitTillVBlank
        stmdb sp!,{r4,lr} // Original instruction
        b @WaitTillVBlank
    .org 0x02094950
    @DelayRand16BitIfAdvancesNotLocked:
        stmdb sp!,{r7}
        ldr r7, [@delay]
        cmp r7, 0
        ldmia sp!,{r7}
        beq Rand16BitIfAdvancesNotLocked // Custom function
        b Rand16Bit // Original function

    // Crash dump: branch FatalError and OS_Panic into stubs that capture the
    // registers and a stack snapshot, then write a 0x1000 record to the backup
    // EEPROM (layout in src/crash_dump.h). The stubs live in the verified-free
    // region at 0x02094968 (zero-filled in the original ROM, no xrefs) right
    // after this code, and they emulate the original first instruction so the
    // stack layout the original code expects is preserved. The @delay guard
    // ensures the mod's overlay 36 code is initialized before calling into it.
    .org 0x0200c2e4
        b @CrashDumpFatalError
    .org 0x0207bfb8
        b @CrashDumpOS_Panic
    .org 0x02094968
    @CrashDumpFatalError:
        // Original first instruction. r0 and r3 stay live in registers in the
        // original code (mov r1, r0 / the next push {r3, lr}), so they are
        // preserved across the dump call.
        push {r0, r1, r2, r3}
        push {r4, r5, r6, r7, r8, r9, r10, r11, r12}
        // Wait until overlay 36 is loaded (r12 is saved on the stack by now).
        ldr r12, [@delay]
        cmp r12, 0
        bne @crashFeGuardFail
        sub sp, sp, #0x44 // 17-word register frame
        str r0, [sp, #0x10] // Preserve original r0 (regs[4])
        str r3, [sp, #0x1C] // Preserve original r3 (regs[7])
        ldr r0, [@crashFePc]
        mrs r1, cpsr
        add r2, sp, #0x68 // sp_base
        str r0, [sp, #0x00] // regs[0] = pc
        str lr, [sp, #0x04] // regs[1] = lr
        str r2, [sp, #0x08] // regs[2] = sp_base
        str r1, [sp, #0x0C] // regs[3] = cpsr
        ldr r0, [sp, #0x6C]
        str r0, [sp, #0x14] // regs[5] = original r1
        ldr r0, [sp, #0x70]
        str r0, [sp, #0x18] // regs[6] = original r2
        add r0, sp, #0x44
        ldmia r0, {r4, r5, r6, r7, r8, r9, r10, r11, r12}
        add r0, sp, #0x20
        stmia r0, {r4, r5, r6, r7, r8, r9, r10, r11, r12} // regs[8..16]
        mov r0, #1 // hook id: FatalError
        mov r1, sp // register frame
        bl CrashDumpWrite
        add r0, sp, #0x44
        ldmia r0, {r4, r5, r6, r7, r8, r9, r10, r11, r12}
        ldr r0, [sp, #0x10] // Restore original r0
        ldr r3, [sp, #0x1C] // Restore original r3
        add sp, sp, #0x78
        b 0x0200c2e8
    @crashFeGuardFail:
        add sp, sp, #0x34 // Unwind to exactly sp_base
        b 0x0200c2e8
    @crashFePc:
        .word 0x0200c2e4
    .org 0x020949f4
    @CrashDumpOS_Panic:
        // Original first instruction. r0-r2 are clobbered by the original
        // code's first call, so they only need to be preserved for the dump.
        push {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, r11, r12}
        push {r3, lr}
        // Wait until overlay 36 is loaded (r12 is saved on the stack by now).
        ldr r12, [@delay]
        cmp r12, 0
        bne @crashOpGuardFail
        sub sp, sp, #0x44 // 17-word register frame
        ldr r0, [@crashOpPc]
        mrs r1, cpsr
        add r2, sp, #0x44 // sp_base
        str r0, [sp, #0x00] // regs[0] = pc
        str lr, [sp, #0x04] // regs[1] = lr
        str r2, [sp, #0x08] // regs[2] = sp_base
        str r1, [sp, #0x0C] // regs[3] = cpsr
        add r3, sp, #0x4C
        ldmia r3, {r0, r1, r2}
        ldr r3, [sp, #0x48]
        add r4, sp, #0x10
        stmia r4, {r0, r1, r2, r3} // regs[4..7]
        add r3, sp, #0x58
        ldmia r3, {r4, r5, r6, r7, r8, r9, r10, r11, r12}
        add r3, sp, #0x20
        stmia r3, {r4, r5, r6, r7, r8, r9, r10, r11, r12} // regs[8..16]
        mov r0, #2 // hook id: OS_Panic
        mov r1, sp // register frame
        add r2, sp, #0x44 // sp_base
        bl CrashDumpWrite
        add r3, sp, #0x4C
        ldmia r3, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, r11, r12}
        add sp, sp, #0x7C
        b 0x0207bfbc
    @crashOpGuardFail:
        add sp, sp, #0x38 // Unwind to exactly sp_base
        b 0x0207bfbc
    @crashOpPc:
        .word 0x0207bfb8

    // Overworld HUD drawing
    .org 0x02008f44
        b CustomSetBrightnessExit
    .org 0x020491bc
        bl HijackCalcChecksumAndSplit
    .org 0x0202b4a8
        bl HijackCreateSimpleMenuAndCloseHUD
    .org 0x0202b7e0
        b HijackCloseSimpleMenuAndCreateHUD
    .org 0x02034ec4
        bl HijackCloseMenuRoutine
    .org 0x02034f2c
        bl HijackOpenMenuRoutine
    .org 0x02027940
        bl HijackNewWindowScreenCheckAndCheckOpenWindows
    .org 0x02028510
        bl HijackDeleteWindowAndCheckOpenWindows
    .org 0x02037e24
        bl HijackTeamNamePromptConfirm

//    // Generate custom missions
//    .org 0x0205e958
//        // Set main board to spawn 8 missions
//        mov r0, 7
//    .org 0x0205e968
//        // Set outlaw board to spawn 8 missions
//        mov r0, 7
//    .org 0x0205eb68 
//        // Provide index as an argument to our function       
//        mov r0, r9
//    .org 0x0205eb70
//        bl GenerateCustomMission
//    .org 0x0205eb84
//        // Skip setting random mission rewards
//        nop
//    .org 0x0205ec00
//        // Provide index as an argument to our function    
//        mov r0, r9
//        add r0, 8
//    .org 0x0205ec0c
//        bl GenerateCustomMission
//    .org 0x0205ec20
//        // Skip setting random mission rewards
//        nop
.close

.open "overlay0.bin", overlay0_start
    .org 0x022beb40
        bl HijackUnloadMenuStateCall
    // Run the play timer in the main menu
    .org 0x022bec40
        bl PlayTimerTickAndWaitTillVBlank
.close

.open "overlay29.bin", overlay29_start
    // Dungeon mode exit
    .org 0x234cdc0
        bl ResetSplitRemainingFrames
    // APS count
    .org 0x022ece10
        bl HijackSetLeaderActionAndCountAction
    .org 0x022f19f8
        bl HijackShouldLeaderKeepRunningAndPreventCount
    // Detect missed pause skips
    .org 0x0234c674
        mov r0, r5
        mov r1, r6
        mov r2, r7
        bl CustomMessageLogPauseLoop
        ldmia sp!,{r3,r4,r5,r6,r7,pc}
    // Dungeon rng
    .org 0x022dfc08
        // Instead of setting the 23-bits of the preseed we set it 
        // fully to prevent the previous state from affecting anything.
        bl HijackSetDungeonRngPreseedAndResetRngSeed
    .org 0x22eb450
        bl LogDungeonRand16Bit
    .org 0x22eb488
        bl LogDungeonRand16Bit
    .org 0x22eb4ac
        bl LogDungeonRand16Bit
    .org 0x22eb4d8
        bl LogDungeonRand16Bit
    .org 0x22eb508
        bl LogDungeonRand16Bit
    .org 0x23439c8
        bl LogDungeonRand16Bit
    .org 0x2343a30
        bl LogDungeonRand16Bit
    .org 0x2345804
        bl LogDungeonRand16Bit
    .org 0x2347e44
        bl LogDungeonRand16Bit
    .org 0x022eb450
        bl LogDungeonRand16Bit
    // Optimization: only call SubstitutePlaceholderStringTags when actually logging a message.
    // This skips a cart read which takes significant time
    .org 0x22ffc0c
        bl SkipAICardRead
    .org 0x22ffc6c
        bl SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser
    .org 0x22ffc80
        bl SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser
    .org 0x22ffc94
        bl SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser
    .org 0x22ffcc8
        bl SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser
    .org 0x22ffcdc
        bl SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser
    .org 0x22ffcfc
        bl SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser
.close

.open "overlay11.bin", overlay11_start
    // Main story shop
    .org 0x022e9de8
        // Reset rng before generating Kecleon items.
        bl HijackGenerateKecleonItems1AndLockRngAdvances
    .org 0x022e9df8
        bl HijackGenerateCroagunkItemsAndUnlockRngAdvances
    // Special episode shop
    .org 0x234d2fc
        bl HijackGenerateKecleonItems1AndLockRngAdvances
    .org 0x022e9da0
        bl HijackGenerateKecleonItems2AndUnlockRngAdvances
    .org 0x022eb150
        bl HijackSetBrightnessNonblockingEntry
    // Increase number of memory blocks in the overlay13 memory arena.
    .org 0x022e912c
        mov r1,#0x37
    // Fix mysterious crash on hardware with the name prompt
    .org 0x022e6b98
        bl HijackPlayerNamePromptAndCloseHUD
.close

.open "overlay13.bin", overlay13_start
    // Quiz partner order
    .org 0x238c438
        bl HijackRandIntAndResetRngSeed
.close

.open "overlay14.bin", overlay14_start
    // Sentry duty choices
    .org 0x238d024
        bl HijackRandRangeAndResetRngSeed
.close

.open "overlay1.bin", overlay1_start
    // Edit main menu
    .org 0x02331b9c
        // Bypass creating the team name option in case there is save data,
        // to avoid setting it twice.
        nop
    .org 0x02331a34
        // Add the team name option in the main menu at all times
        bl HijackIsAdventureLogNotEmptyAndAddRenameTeam
    // Set team name menu function
    .org 0x02337a9c
        // Instead of setting the team name in the main menu, set the RNG seed.
        // Overrides the function that checks whether the team name is different
        // than input, pretends it's not and then therefore skips setting it.
        bl SetFixedRNGSeed
        nop
        nop
        nop
        mov r0, 0
    // Keyboard creation
    .org 0x02337a20
        bl ShowKeyboardWithRandomDefaultValue
    .org 0x02337b74
        // Skip confirmation prompt after keyboard
        b 0x2337b9c
    .org 0x2337a94
        // Skip top screen change
        nop
.close