About exscapeOS
===============
exscapeOS is a small hobby OS written in C and x86 assembly.  
The main purpose of it for me (the sole developer) to learn about operating
system design and implementation, in addition to related technologies (ranging
from CPUs to low-level hardware/driver programming, UNIX/POSIX, etc).

Basic functionality (booting, accessing the file system) works with as little
as 3 MB RAM, though the OS is not aimed at low performance systems; the low
memory usage is more a byproduct of the simplicity.
Few if any modern CPU features are used so far, though: the OS is built for
Pentium-class CPUs (and later).

Testing exscapeOS
=================
First, be warned: there aren't a lot of fun things to do in the OS yet.
Most of development time is spent working on things that are generally invisible
to the OS user - the virtual memory subsystem, file systems and the VFS,
processes, ELF parsing, etc, etc.

If you're still interested: I develop and test mainly in QEMU, though I occasionally
use other tools for testing, including Parallels Desktop, Bochs and Simics.

Compiling from source
---------------------
It should - but may not always be - sufficient to clone the git repository,
and run "make" in the main project directory. This assumes a UNIX-like system.
I develop on Mac OS X (10.6), though it should work on Linux with few if any changes,
and likely *BSD as well.

If it compiled - great: bootable.iso is the main output file you'd be interested in.
It's a regular El Torito bootable CD image.
My current QEMU command line can be seen in the Makefile ("make run").

Using exscapeOS
---------------
As of this writing, the OS boots to a kernel-mode shell. It can spawn userspace
processes, though - try "ls_initrd". Also see the "help" command.
The OS has very basic FAT32 support, which is still in the early stages (along
with the VFS). It's read-only, very slow, and possibly buggy - but shouldn't
cause data corruption, since it's read-only. Only one FAT32 partition is supported
right now - any more and it won't even boot.
