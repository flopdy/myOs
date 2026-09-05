# tiny efi c os <3

a smol lidl ~unix style os that uses the efi provided protocols

source: [myOs/myOs.c](https://github.com/flopdy/myOs/tree/main/edk2/MdeModulePkg/Application/myOs/myOs.c)

## setting up on hardware

check [releases](https://github.com/flopdy/myOs/releases/latest) for binaries

youll need:

* x86-64 (AMD64) uefi firmware
* usb/ssd with partition in FAT32 format filesystem
* at least 40kB free storage (recommended 45kB+ lol its so tiny)
* folder setup: /EFI/BOOT/BOOTX64.EFI
