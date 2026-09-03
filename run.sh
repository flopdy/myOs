#!/bin/bash

cp ~/repos/boot/edk2/Build/MdeModule/DEBUG_GCC/X64/MdeModulePkg/Application/myOs/myOs/OUTPUT/myOs.efi ~/repos/boot/USB/EFI/BOOT/BOOTX64.EFI

qemu-system-x86_64 \
-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
-drive if=pflash,format=raw,file=OVMF_VARS_4M.fd \
-drive format=raw,file=fat:rw:USB \
-monitor stdio
