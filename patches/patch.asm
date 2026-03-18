.nds
.include "symbols.asm"

.open "arm9.bin", arm9_start
    // Optimization: Skip waiting for VCount 0 at the start of a frame
    .org 0x0200345c
        nop
    // Overworld HUD drawing
    .org 0x02008f44
        b CustomSetBrightnessExit
    // Trampoline to inject custom code to VBlank interrupt handler
    .org 0x02003704
        b 0x02094850
    @vblank:
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
        stmdb sp!,{r3,lr}; // Run original instruction from the trampoline
        b @vblank
    @overlay36_loaded:
        .word 0x020047BC
    @delay:
        .word 0x00000010
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
    // Optimization: only call SubstitutePlaceholderStringTags when actually logging a message.
    // This skips a cart read which takes signficant time
    .org 0x22ffc0c
        nop
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
.close

.open "overlay29.bin", overlay29_start
    // Dungeon rng
    .org 0x022dfc08
        // Instead of setting the 23-bits of the preseed we set it 
        // fully to prevent the previous state from affecting anything.
        bl HijackSetDungeonRngPreseedAndResetRngSeed
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
    .org 0x0230d118
        //nop
.close

.open "overlay11.bin", overlay11_start
    // Main story shop
    .org 0x022e9de8
        // Reset rng before generating Kecleon items.
        // Missions are generated immediately after so this resets that too.
        bl HijackGenerateKecleonItems1AndResetRngSeed
    // Special episode shop
    .org 0x234d2fc
        bl HijackGenerateKecleonItems1AndResetRngSeed
    .org 0x022eb150
        bl HijackSetBrightnessNonblockingEntry
    // Increase number of memory blocks in the overlay13 memory arena.
    .org 0x022e912c
        mov r1,#0x37
    // Fix myserious crash on hardware with the name prompt
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