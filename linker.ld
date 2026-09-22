OUTPUT_ARCH(arm)
MEMORY {
        uplink  : ORIGIN = 0x23A8000, LENGTH = 0x2FF80
        out     : ORIGIN = 0x23D7FF0, LENGTH = 0x8010
}

/* The uplink (TinyUSB + DSpico CDC + sampler) objects are placed in the
   overlay 36 headroom region (0x23A8000..0x23D7FF0); the existing mod keeps
   its region at 0x23D7FF0. EXCLUDE_FILE keeps the two sets of sections apart. */
SECTIONS {
        .text_uplink : {

                FILL (0xABCD)

                __text_uplink_start = . ;

                uplink.o (.text .init .fini)
                uplink_sampler.o (.text)
                usb_descriptors.o (.text)
                dspico_card.o (.text)
                dcd_dspico.o (.text)
                tusb.o (.text)
                tusb_fifo.o (.text)
                usbd.o (.text)
                usbd_control.o (.text)
                cdc_device.o (.text)

                uplink.o (.rodata .rodata.*)
                uplink_sampler.o (.rodata .rodata.*)
                usb_descriptors.o (.rodata .rodata.*)
                dspico_card.o (.rodata .rodata.*)
                dcd_dspico.o (.rodata .rodata.*)
                tusb.o (.rodata .rodata.*)
                tusb_fifo.o (.rodata .rodata.*)
                usbd.o (.rodata .rodata.*)
                usbd_control.o (.rodata .rodata.*)
                cdc_device.o (.rodata .rodata.*)

                uplink.o (.data .data.*)
                uplink_sampler.o (.data .data.*)
                usb_descriptors.o (.data .data.*)
                dspico_card.o (.data .data.*)
                dcd_dspico.o (.data .data.*)
                tusb.o (.data .data.*)
                tusb_fifo.o (.data .data.*)
                usbd.o (.data .data.*)
                usbd_control.o (.data .data.*)
                cdc_device.o (.data .data.*)

                . = ALIGN(4);
                __bss_uplink_start__ = . ;
                uplink.o (.bss .bss.*)
                uplink_sampler.o (.bss .bss.*)
                usb_descriptors.o (.bss .bss.*)
                dspico_card.o (.bss .bss.*)
                dcd_dspico.o (.bss .bss.*)
                tusb.o (.bss .bss.*)
                tusb_fifo.o (.bss .bss.*)
                usbd.o (.bss .bss.*)
                usbd_control.o (.bss .bss.*)
                cdc_device.o (.bss .bss.*)
                __bss_uplink_end__ = . ;
        } >uplink = 0xff

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

                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .init)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .text)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .fini)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .ctors)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .dtors)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .rodata)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .rodata.*)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .data)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .data.*)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) COMMON)
                __text_end  = . ;
                . = ALIGN(4);
                __bss_start__ = . ;
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .bss)
                *(EXCLUDE_FILE(uplink.o uplink_sampler.o usb_descriptors.o dspico_card.o dcd_dspico.o tusb.o tusb_fifo.o usbd.o usbd_control.o cdc_device.o) .bss.*)
                __bss_end__ = . ;

                _end = __bss_end__ ;
                __end__ = __bss_end__ ;
        } >out = 0xff
}
