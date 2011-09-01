all:
	i586-elf-gcc -o kernel.o -c kernel.c -Wall -Wextra -Werror -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./include -ggdb3 -std=gnu99 && i586-elf-ld -T linker.ld -o kernel.bin loader.o kernel.o && cat stage1 stage2 pad kernel.bin > floppy.img && /Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu -fda floppy.img
