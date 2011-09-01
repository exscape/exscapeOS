#!/bin/bash

cp kernel.bin isofiles/boot &&
mkisofs -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles &&
/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu -cdrom bootable.iso -boot d1 
