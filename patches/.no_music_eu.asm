// Enables the no music debug flag. Unhide this if you want to build the rom without music.

.nds
.include "symbols.asm"

.open "arm9.bin", arm9_start

.org 0x02017edc

.area 4
    mov r0, 1
.endarea

.org 0x02017f5c

.area 4
    mov r0, 1
.endarea

.org 0x0201810c

.area 4
    mov r0, 1
.endarea

.org 0x0201818c

.area 4
    mov r0, 1
.endarea

.close
