.nds
.include "symbols.asm"

.open "arm9.bin", arm9_start
    .org 0x02008f44
        b CustomSetBrightnessExit
    // Overworld HUD drawing
    .org HandleFades
        //b CustomHandleFades
    // Update the timer every frame but only if overlay36 is fully loaded
    .org 0x020036b4
        ldr r0, [@overlay36_loaded]
        ldr r0, [r0]
        cmp r0, 0
        beq @continue
        ldr r0, [@delay]
        cmp r0, 0
        subne r0, 1
        str r0, [@delay]
        bleq FrameRoutine
        b @continue
    @overlay36_loaded:
        .word 0x020047BC
    @delay:
        .word 0x00000010
    .org 0x020036ec
    @continue:
    .org 0x020491bc
        bl HijackCalcChecksumAndSplit
.close

.open "overlay29.bin", overlay29_start
    // Dungeon rng
    .org 0x022dfc08
        // Instead of setting the 23-bits of the preseed we set it 
        // fully to prevent the previous state from affecting anything.
        bl HijackSetDungeonRngPreseedAndResetRngSeed
    // Dungeon HUD drawing
    .org 0x022e93cc
        bl HijackTopScreenInitFuncAndDrawTopScreenText
    .org 0x022e9528
        bl HijackFreeTopScreenAndClearTopScreenText
    .org 0x0234d518
        mov r0, r4
        mov r1, r5
        bl UpdateDungeonFadeStruct
        ldmia sp!, {r3,r4,r5,pc}
    // Dungeon mode exit
    .org 0x234cdc0
        bl ResetSplitRemainingFrames
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