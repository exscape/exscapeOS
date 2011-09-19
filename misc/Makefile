all: create_initrd

clean:
	rm create_initrd && rm -rf *.dSYM

create_initrd:
	gcc -Wall -Werror -std=gnu99 -O0 -ggdb3 -o create_initrd src/create_initrd.c
