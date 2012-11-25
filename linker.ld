ENTRY (loader)

SECTIONS{
    . = 0x00100000;


	/* The following header is to be used instead of the uncommented one
	 * whenever the higher half kernel is implemented!
	 */
	/* .__mbHeader : AT( ADDR(.__mbHeader) - KERNEL'S_VIRTUAL_OFFSET_HERE ) {
	   *(.__mbHeader)
	   }
	*/
	.__mbHeader : {
		*(.__mbHeader)
	}

	__start_text = .;
    .text :{
        *(.text)
    }
	__end_text = .;

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
