ENTRY (_start)
STARTUP(crt0.o)

SECTIONS{
    . = 0x10000000; /* start at 256 MB, far above code at 0x100000 */

    .text :{
        *(.text)
    }

    .rodata ALIGN (0x1000) : {
        *(.rodata)
    }

    .data ALIGN (0x1000) : {
        *(.data)
    }

    .bss : {
        sbss = .;
        *(COMMON)
        *(.bss)
        ebss = .;
    }

	end = .;
	_end = .;
	__end = .;
}
