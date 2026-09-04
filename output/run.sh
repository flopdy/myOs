#!/bin/bash

sudo qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/home/flopdy/repos/boot/OVMF_VARS_4M.fd \
  -drive format=raw,file=/dev/sda \
  -monitor stdio
