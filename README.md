About exscapeOS
===============
exscapeOS is a small hobby OS written in C and x86 assembly.
The main purpose of it for me (the sole developer) to learn about operating
system design and implementation, in addition to related technologies (ranging
from CPUs to low-level hardware/driver programming, UNIX/POSIX, etc).

Basic functionality (booting, accessing the file system) works with less than
8 MB RAM, though the OS is not aimed at low spec systems; the low memory usage
is more a byproduct of the simplicity - and (as of 2013-01-01) lack of caching.  
Few if any modern CPU features are used so far, though: the OS is built for
Pentium-class CPUs (and later), however MMX support is required.

Some things that are implemented (also see the CHECKLIST file) are:
* The very basics (GDT, IDT, protected mode setup, paging)
* Preemptive multitasking (no threads yet, though)
* User mode support with Newlib as the C library
* ELF parsing
* fork(), execve() for multitasking in user mode
* pipe() and related syscalls (e.g. dup2())
* Read-only FAT32 (and 32 only) support (disks aren't *required*, but supported)
* RTC (date/time) stuff, both in kernel mode and user mode (gettimeofday(), time() etc.), though with 1 second-resolution only
* VERY basic networking: RTL8139 driver, IP + ARP + ICMP echo and nothing more, and only in kernel mode
* ... and much, much more significant and nonsignificant stuff.

I successfully "ported" Lua the same day I'm writing this, which I suppose
is a good sign that things are pretty POSIX compatible!  
Inter-process signals are still missing, but are high up on the TODO
list.
(By "ported" I mostly mean "successfully compiled", as only Makefile and luaconf.h
edits were necessary to make it work!)

Testing exscapeOS
=================
First, be warned: there aren't a lot of fun things to do in the OS yet.
Most of development time is spent working on things that are generally
invisible to the OS user - the virtual memory subsystem, file systems
and the VFS, processes, ELF parsing, etc, etc.

If you're still interested: I develop and test mainly in QEMU, though I
occasionally use other tools for testing, including Parallels Desktop, Bochs
and Simics, so it should run most everywhere. I have barely tested it at all
on real hardware, unfortunately.
In all honesty though, there is usually at LEAST one new bug found every time I
try to run it on a new platform (be it a new VM/emulator software or a new computer).

Compiling from source
---------------------
This is unfortunately a fairly long story, and may not be fully up-to-date / may be incomplete.
First, some requirements:
* Unix-like OS (I've use Mac OS X 10.6 and 10.9; I've also tried Debian Wheezy i386 and Arch Linux)
* Some common misc. packages (bash, wget, patch, find, and likely others)
* GCC (for the host OS; a cross compiler is built using that)
* nasm
* mkisofs (or xorrisofs from the xorriso package: edit the Makefile to replace mkisofs with xorrisofs in that case)
* Stuff that the exscapeOS-targeted GCC needs to compile: the MPC, MPFR and GMP libs.
* QEMU, or something else to run it in (having QEMU will make it easier)
* Automake 1.12.x OR OLDER (see build.sh); Newlib requires features removed in 1.13

MPC, MPFR and GMP can be installed on debian with:
    sudo apt-get install lib{mpc,mpfr,gmp}-dev
... or, on Arch Linux, with:
    sudo pacman -S gmp mpfr libmpc

Finally, with prerequisites out of the way...  
1) Edit toolchain/build.sh to change PREFIX to somewhere convenient, e.g. /tmp/exscapeos (or perhaps /usr/local/cross for something more permanent)
1.1) Also make sure to check the path to the automake etc. programs. I have them as symlinks to e.g. /usr/bin/automake-1.12.
2) cd toolchain;  bash build.sh # This should build and install GCC, binutils and Newlib for exscapeOS, and will take a while. Make sure you have write permission to $PREFIX!
3) make # to build the OS itself, plus Lua  
4) make nofat # to run in QEMU  

If it finally compiled - great: bootable.iso is the main output file you'd be interested in.  
It's a regular El Torito bootable CD image.  
Either that or you can use "make nofat" as above, to start in QEMU. See the Makefile for the command line, otherwise.
If you want to boot it manually, isofiles/boot/kernel.bin and isofiles/boot/initrd.img are what you want.

Using exscapeOS
---------------
As of this writing (2013-01-01 -- updated a bit 2014-12-07), the OS boots to a kernel-mode shell, except for the first virtual terminal, which starts the user-mode shell. "exit" in the user-mode shell returns to the kernel-mode shell (kshell), which has more commands (e.g. "help").

kshell is not intended to be a permanent thing; the OS naturally evolved that way. After adding print support and keyboard support, I obviously wanted a way to enter commands, so I started on a basic shell.
Many months later, I had multitasking, user mode support, ELF loading and a C library, so I started work on a more proper (but not scriptable) shell, sh (or eshell).

Anayway... "help" works (in the kernel mode shell, kshell, only), as does "ls" (try ls /bin).
sh starts the user mode shell. Don't let the new name fool you; it's not at all POSIX compatible yet. It does support very basic redirects and such, but the parsing is very sub-par. I have no plans to support scripting, but I do have plans to port e.g bash (or perhaps something simpler) in the future.

The OS has basic FAT32 support - read-only, however. The VFS is barely existent and
needs a rewrite from the ground up, but that shouldn't be overly obvious from a user's
viewpoint.
ext2 support is in the works; everything except actually reading files (using the read() syscall) seems to work now, including symlink parsing. There is a function which reads an entire file, but not one that can read arbitrary blocks (as of Dec 2014).

The console system has a few nice features: Alt+F1 though Alt+F4 can be used to use multiple virtual consoles.  
They support several (currently 150, see src/include/console.h) screens worth of scrollback: scroll using shift + arrow keys, or one screen at a time with shift + alt + arrow keys.

Contact
========
You can reach me at serenity@exscape.org if you have any questions, comments
or whatever else.  
I welcome pretty much all emails written by humans! :)
