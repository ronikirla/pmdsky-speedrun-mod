# DSpico USB Examples
This repository contains examples of using the DSpico USB port from a DS application.

The `examples` folder contains the following examples:
- mass-storage - Allows access to the DSpico micro SD card over USB
- usb-microphone - Makes the DSi/3DS work as a USB microphone for your PC
    - Note: does not work with regular DS devices currently
- usb-speaker - Makes the DS/DSi/3DS work as a USB speaker for your PC
- usb-video - Allows to use the camera of your DSi or 3DS on your PC via USB

The `platform` folder contains the tiny usb platform code for the DSpico.

> [!IMPORTANT]
> There is currently a bug in the DSpico Firmware that sometimes causes the DSpico the crash if you use USB, reset the console and use USB again. If this happens, unplug USB and reset the console such that the DSpico performs a power cycle. You can plug the USB back in afterwards.

## Environment
The examples require a pre-calico devkitpro/libnds environment to build. With such an environment setup, just run `make` in the folder of the example to compile it.

## License
The platform code and examples are licensed under the Zlib license. For details, see `LICENSE.txt`.

## Credits
- [tinyusb](https://github.com/hathach/tinyusb) for the USB library. The examples are also largely based on the examples that come with tinyusb.
- [libcamera](https://github.com/FTC55/libcamera) by [FTC55](https://github.com/FTC55) for camera initialization code
- Banner icons by [nitehack](https://www.github.com/nitehack)
