OUTPUT_ARCH(arm)
MEMORY {
        out     : ORIGIN = 0x23D7FF0, LENGTH = 0x8010
}

/* Everything (mod + uplink) lives in the overlay 36 common area at
   0x23D7FF0. The USB device stack itself lives in the DSpico firmware,
   not the ROM; the uplink objects here are just the card-protocol and
   sampler code, small enough to share the mod's region. */
SECTIONS {
        .text : {

                FILL (0xABCD)

                __exidx_start = .;
                __exidx_end = .;
                abort = .;

                __text_start = . ;

                __init_array_start = .;
                KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
                KEEP (*(.init_array))
                __init_array_end = .;
                __fini_array_start = .;
                KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
                KEEP (*(.fini_array))
                __fini_array_end = .;

                *(.init)
                *(.text)
                *(.fini)
                *(.ctors)
                *(.dtors)
                *(.rodata)
                *(.rodata.*)
                *(.data)
                *(.data.*)
                *(COMMON)
                __text_end  = . ;
                . = ALIGN(4);
                __bss_start__ = . ;
                *(.bss)
                *(.bss.*)
                __bss_end__ = . ;

                _end = __bss_end__ ;
                __end__ = __bss_end__ ;
        } >out = 0xff
}
