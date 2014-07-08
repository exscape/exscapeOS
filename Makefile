TARGET = build

WARNINGS := -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
                -Wwrite-strings -Wredundant-decls -Wnested-externs -Winline \
				-Wuninitialized -Wstrict-prototypes \
				-Wno-unused-parameter -Wno-cast-align -Werror -Wno-unused-function # TODO: re-add unused-function

PREFIX = /usr/local/cross
GCCINC = $(PREFIX)/lib/gcc/i586-pc-exscapeos/4.7.2/include
TOOLCHAININC = $(PREFIX)/i586-pc-exscapeos/include

CC = i586-pc-exscapeos-gcc
CFLAGS := -O0 -nostdlib -nostdinc -I./src/include -I$(GCCINC) -I$(TOOLCHAININC) -std=gnu99 -march=i586 $(WARNINGS) -ggdb3 -D__DYNAMIC_REENT__ -D_EXSCAPEOS_KERNEL
LD = i586-pc-exscapeos-ld
NATIVECC = gcc # Compiler for the HOST OS, e.g. Linux, Mac OS X

PROJDIRS := src/kernel src/include src/lib
SRCFILES := $(shell find $(PROJDIRS) -type f -name '*.c')
HDRFILES := $(shell find $(PROJDIRS) -type f -name '*.h')
ASMFILES := $(shell find $(PROJDIRS) -type f -name '*.s')
OBJFILES := $(patsubst %.c,%.o,$(SRCFILES))
OBJFILES += $(patsubst %.s,%.o,$(ASMFILES))

USERSPACEPROG := $(shell find src/userspace/ -maxdepth 2 -name 'Makefile' -exec dirname {} \;)

DEPFILES    := $(patsubst %.c,%.d,$(SRCFILES))

# All files to end up in a distribution tarball
ALLFILES := $(SRCFILES) $(HDRFILES) $(AUXFILES) $(ASMFILES)

#QEMU := /usr/local/bin/qemu-system-i386
QEMU := /usr/local/cross/bin/qemu # qemu 0.14.1, new versions don't work with networking for me; not sure why!

all: $(OBJFILES)
	@set -e; if [ ! -d "initrd/bin" ]; then \
		mkdir -p initrd/bin initrd/etc; \
	fi
	@$(LD) -T linker-kernel.ld -o kernel.bin ${OBJFILES}
	@cp kernel.bin isofiles/boot
	@set -e; for prog in $(USERSPACEPROG); do \
		make -C $$prog; \
	done
	@set -e; if [ ! -f "initrd/bin/lua" ]; then \
		cd contrib && bash lua.sh ; cd ..; \
	fi
	@python misc/create_initrd.py > /dev/null # let stderr through!
	@mkisofs -quiet -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o bootable.iso isofiles
#	@/opt/local/bin/ctags -R *
	@if [ -f "initrd/bin/eshell" ]; then \
		mv initrd/bin/eshell initrd/bin/sh; \
	fi

clean:
	-$(RM) $(wildcard $(OBJFILES) $(DEPFILES) kernel.bin bootable.iso misc/initrd.img)
	@for prog in $(USERSPACEPROG); do \
		make -C $$prog clean; \
		rm -f initrd/bin/`basename $$prog` ; \
	done
	@rm -f initrd/bin/sh

-include $(DEPFILES)

todolist:
	-@for file in $(ALLFILES); do fgrep -H -e TODO -e FIXME $$file; done; true
	@cat TODO

%.o: %.c Makefile
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@ -fno-builtin

%.o: %.s Makefile
	@nasm -o $@ $< -f elf -F dwarf -g

nofat: all
	$(QEMU) -cdrom bootable.iso -monitor stdio -s -serial file:serial-output -m 64

net: all
	@bash net-scripts/prepare.sh
#@sudo $(QEMU) -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -net nic,model=rtl8139,macaddr='10:20:30:40:50:60' -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=net-scripts/ifdown.sh -serial file:serial-output -d cpu_reset -m 64
	@sudo $(QEMU) -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -serial file:serial-output -d cpu_reset -m 64 -net nic,vlan=0,macaddr=00:aa:00:18:6c:00,model=rtl8139 -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=no

netdebug: all
	@bash net-scripts/prepare.sh
#@sudo $(QEMU) -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -S -net nic,model=rtl8139,macaddr='10:20:30:40:50:60' -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=net-scripts/ifdown.sh -serial file:serial-output -d cpu_reset -m 64
	@sudo $(QEMU) -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -S -serial file:serial-output -d cpu_reset -m 64 -net nic,vlan=0,macaddr=00:aa:00:18:6c:00,model=rtl8139 -net tap,ifname=tap2,script=net-scripts/ifup.sh,downscript=no

run: all
	@sudo $(QEMU) -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -serial file:serial-output -d cpu_reset -m 64

debug: all
	@sudo $(QEMU) -cdrom bootable.iso -hda hdd.img -hdb fat32.img -monitor stdio -s -S -serial file:serial-output -d cpu_reset -m 64
