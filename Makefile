#all:
#	i586-elf-gcc -o kernel.o -c kernel.c -Wall -Wextra -Werror -nostdlib -nostartfiles -nodefaultlibs -nostdinc -I./include -ggdb3 -std=gnu99 && i586-elf-ld -T linker.ld -o kernel.bin loader.o kernel.o

CC = clang # i586-elf-gcc should work too
LD = i586-elf-ld

AUXFILES := # FIXME: isofiles osv
PROJDIRS := src
#NOTE: projdirs doesn't include misc/, so that it dosen't try to link those utils with the kernel!
SRCFILES := $(shell find $(PROJDIRS) -type f -name '*.c')
HDRFILES := $(shell find $(PROJDIRS) -type f -name '*.h')
ASMFILES := $(shell find $(PROJDIRS) -type f -name '*.s')
OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))
OBJFILES += $(patsubst %.s,%.o,$(ASMFILES))

DEPFILES    := $(patsubst %.c,%.d,$(SRCFILES))

# All files to end up in a distribution tarball
ALLFILES := $(SRCFILES) $(HDRFILES) $(AUXFILES) $(ASMFILES)

WARNINGS := -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
                -Wwrite-strings -Wredundant-decls -Wnested-externs -Winline \
				-Wuninitialized -Wstrict-prototypes -Wno-unused-function \
				-Wno-unused-parameter -Wno-cast-align -Wno-self-assign -Werror

CFLAGS := -O0 -ggdb3 -nostdlib -nostdinc -I./src/include -std=gnu99 -ccc-host-triple i586-pc-linux-gnu -march=i586 $(WARNINGS)

all: $(OBJFILES)
	@$(LD) -T linker.ld -o kernel.bin ${OBJFILES}
	@cp kernel.bin isofiles/boot
	@cd misc; ./create_initrd initrd_contents/* > /dev/null ; cd ..
	@cp misc/initrd.img isofiles/boot
	@mkisofs -quiet -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles
	@/opt/local/bin/ctags -R *
	@bash net-scripts/prepare.sh

clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) kernel.bin bootable.iso misc/initrd.img)

-include $(DEPFILES)

todolist:
	-@for file in $(ALLFILES); do fgrep -H -e TODO -e FIXME $$file; done; true
	@cat TODO

%.o: %.c Makefile
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@ -fno-builtin

%.o: %.s Makefile
	@nasm -o $@ $< -f elf -F dwarf -g

run: all
	@sudo qemu -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -net nic,model=rtl8139,macaddr='10:20:30:40:50:60' -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=net-scripts/ifdown.sh -serial file:serial-output -d cpu_reset

debug: all
	@sudo qemu -cdrom bootable.iso -hda hdd.img -hdb fat32.img -s -S -monitor stdio -net nic,model=rtl8139,macaddr='10:20:30:40:50:60' -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=net-scripts/ifdown.sh -serial file:serial-output
