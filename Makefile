#all:
#	i586-elf-gcc -o kernel.o -c kernel.c -Wall -Wextra -Werror -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./include -ggdb3 -std=gnu99 && i586-elf-ld -T linker.ld -o kernel.bin loader.o kernel.o

CC = i586-elf-gcc
LD = i586-elf-ld

AUXFILES := # FIXME: isofiles osv
PROJDIRS := src/kernel src/lib
SRCFILES := $(shell find $(PROJDIRS) -type f -name '*.c')
HDRFILES := $(shell find $(PROJDIRS) -type f -name '*.h')
ASMFILES := $(shell find $(PROJDIRS) -type f -name '*.s')
OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))
OBJFILES += $(patsubst %.s,%.o,$(ASMFILES))

DEPFILES    := $(patsubst %.c,%.d,$(SRCFILES))

# All files to end up in a distribution tarball
ALLFILES := $(SRCFILES) $(HDRFILES) $(AUXFILES) $(ASMFILES)

WARNINGS := -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align -fmax-errors=0 \
                -Wwrite-strings \
                -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
                -Wuninitialized -Wstrict-prototypes -Werror \
#				-Wconversion
# TODO: activate Wconversion and fix the errors!
#WARNINGS := -Wall -Werror

CFLAGS := -O0 -ggdb3 -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./src/include -std=gnu99 $(WARNINGS)
# Optimized CFLAGS
#CFLAGS := -O3 -march=pentium -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./src/include -std=gnu99 $(WARNINGS)

all: $(OBJFILES)
	@$(LD) -T linker.ld -o kernel.bin ${OBJFILES}
	@cp kernel.bin isofiles/boot
	@mkisofs -quiet -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles

clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) kernel.bin bootable.iso)

-include $(DEPFILES)

todolist:
	-@for file in $(ALLFILES); do fgrep -H -e TODO -e FIXME $$file; done; true
	@cat TODO

%.o: %.c Makefile
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@ -fno-builtin

%.o: %.s Makefile
	@nasm -o $@ $< -f elf -F dwarf -g

run: all
	@qemu -cdrom bootable.iso -monitor stdio -s

debug: all
	@qemu -cdrom bootable.iso -s -S -monitor stdio
