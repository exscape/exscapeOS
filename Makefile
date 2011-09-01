#all:
#	i586-elf-gcc -o kernel.o -c kernel.c -Wall -Wextra -Werror -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./include -ggdb3 -std=gnu99 && i586-elf-ld -T linker.ld -o kernel.bin loader.o kernel.o

CC = i586-elf-gcc
LD = i586-elf-ld

AUXFILES := run.sh # isofiles osv också!
PROJDIRS := include kernel
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
	@nasm -o loader.o loader.s -f elf
	@i586-elf-ld -T linker.ld -o kernel.bin ${OBJFILES} loader.o # FIXME

clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) kernel.bin bootable.iso) loader.o

-include $(DEPFILES)

todolist:
	-@for file in $(ALLFILES); do fgrep -H -e TODO -e FIXME $$file; done; true

%.o: %.c Makefile
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run:
	@cp kernel.bin isofiles/boot
	@mkisofs -quiet -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles
	@/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu -cdrom bootable.iso -boot d1 -cocoanodialogs -monitor stdio

debug:
	@cp kernel.bin isofiles/boot
	@mkisofs -quiet -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles
	@/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu -cdrom bootable.iso -boot d1 -cocoanodialogs -s -S -monitor stdio
