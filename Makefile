#all:
#	i586-elf-gcc -o kernel.o -c kernel.c -Wall -Wextra -Werror -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./include -ggdb3 -std=gnu99 && i586-elf-ld -T linker.ld -o kernel.bin loader.o kernel.o

CC = i586-elf-gcc
LD = i586-elf-ld

AUXFILES := run.sh # isofiles osv också!
PROJDIRS := kernel lib
SRCFILES := $(shell find $(PROJDIRS) -type f -name '*.c')
HDRFILES := $(shell find $(PROJDIRS) -type f -name '*.h')
OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))

DEPFILES    := $(patsubst %.c,%.d,$(SRCFILES))

# All files to end up in a distribution tarball
ALLFILES := $(SRCFILES) $(HDRFILES) $(AUXFILES)

#WARNINGS := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
                -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
                -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
                -Wuninitialized -Wconversion -Wstrict-prototypes -Werror
WARNINGS := -Wall -Werror
CFLAGS := -ggdb3 -std=c99 -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./include -std=gnu99 $(WARNINGS)

all: $(OBJFILES)
	@nasm -o loader.o loader.s -f elf -F dwarf -g
	@i586-elf-ld -T linker.ld -o kernel.bin ${OBJFILES} loader.o # FIXME

clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) kernel.bin bootable.iso) loader.o

-include $(DEPFILES)

todolist:
	-@for file in $(ALLFILES); do fgrep -H -e TODO -e FIXME $$file; done; true

%.o: %.c Makefile
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run: all
	@cp kernel.bin isofiles/boot
	@mkisofs -quiet -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles
	@qemu -cdrom bootable.iso -monitor stdio

debug: all
	@cp kernel.bin isofiles/boot
	@mkisofs -quiet -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles
	@qemu -cdrom bootable.iso -s -S -monitor stdio
