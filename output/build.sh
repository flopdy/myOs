#!/bin/bash

cd ~/repos/boot/edk2/
build
cp ~/repos/boot/edk2/Build/MdeModule/DEBUG_GCC/X64/MdeModulePkg/Application/myOs/myOs/OUTPUT/myOs.efi ~/repos/boot/output/BOOTX64.EFI

cd ~/repos/boot/output/
ls
