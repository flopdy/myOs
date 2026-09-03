# tiny efi c os <3

source is in edk2/MdeModulePkg/Applications/myOs/

USB contains the files for running - (minus ASSETS from an old project)

___

## running on hardware

to test ur gon need to format a usb to fat32, copy the dir EFI onto it

so your usb should contain /EFI/BOOT/BOOTX64.EFI

boot into firmware settings (del/f2), change boot priority/boot to the usb

when youre done, run 'shutdown'

to disable booting from usb either remove it, or change priority again to boot from ur primary os

___

yar
