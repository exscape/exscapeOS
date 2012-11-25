#!/bin/bash

CC=i586-elf-gcc
LD=i586-elf-ld
NASM=nasm

if [ "$#" -eq 0 ]; then
	echo "Usage: $0 <filename.c>"
	exit 1
fi

IN="$1"
OUT_O="${IN/\.c/.o}"
OUT="${OUT_O/\.o/}"

nasm -f elf crt0.s -g &&
i586-elf-gcc -D_EXSCAPEOS_USERSPACE -Wall -nostdlib -nostdinc -c -o "$OUT_O" "$IN" -ggdb3 -I./include &&
i586-elf-ld -T user-link.ld $OUT_O -o $OUT &&
echo "Done! Output file is $OUT" || echo "Failed to compile!"
