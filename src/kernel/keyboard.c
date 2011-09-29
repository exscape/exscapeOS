#include <types.h>
#include <kernel/interrupts.h>
#include <kernel/kernutil.h> /* panic */
#include <kernel/monitor.h>
#include <kernel/kheap.h>

/* The different modifier keys we support */
#define MOD_NONE  0
#define MOD_CTRL  (1 << 0)
#define MOD_SHIFT (1 << 1)
#define MOD_ALT   (1 << 2)

/* The modifier keys currently pressed */
static unsigned char mod_keys = 0;

/* A ring buffer that stores keystrokes */
#define KEYBUFFER_SIZE 256
typedef struct ringbuffer {
	volatile unsigned char data[KEYBUFFER_SIZE];
	volatile unsigned char *read_ptr;
	volatile unsigned char *write_ptr; /* volatile is probably not needed */
	volatile uint32 counter; /* how much unread data is stored? */
} ringbuffer_t;

static volatile ringbuffer_t *keybuffer;

void keyboard_callback(registers_t regs);

/* Set up the keyboard handler */
void init_keyboard(void) {
	keybuffer = (ringbuffer_t *)kmalloc(sizeof(ringbuffer_t));
	keybuffer->read_ptr = keybuffer->data;
	keybuffer->write_ptr = keybuffer->data;
	keybuffer->counter = 0;

	register_interrupt_handler(IRQ1, keyboard_callback);
}

/* A US keymap, courtesy of Bran's tutorial */
unsigned char kbdmix[128] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '+', /*'´' */0, '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '-',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};		

// Excerpt from the US no-modifier key-table
  //'q', 'w', 'e', 'r',	/* 19 */
  //'t', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    //0,			/* 29   - Control */
  //'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 //'\'', '`',   0,		/* Left shift */
 //'<', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  //'m', ',', '.', '-',   0,				/* Right shift */

unsigned char kbdse_shift[128] =
{
    0,  27, '!', '\"', '#', 0 /* shift+4 */, '%', '&', '/', '(',	/* 9 */
  ')', '=', '?', '`', '\b',	/* Backspace */
  '\t',			/* Tab */

 'Q', 'W', 'E', 'R',   /* 19 */
  'T', 'Y', 'U', 'I', 'O', 'P', 'A', 'A', '\n', /* Enter key */
    0,          /* 29   - Control */
  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'O', /* 39 */
 '\'', '`',   0,        /* Left shift */
 '>', 'Z', 'X', 'C', 'V', 'B', 'N',            /* 49 */
  'M', ';', ':', '_',   0,              /* Right shift */

  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};		

unsigned char kbdse_alt[128] =
{
    0,  27, 0 /*alt+1*/, '@', 0, '$', 0, 0, '{', '[',	/* 9 */
  ']', '}', '\\', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};		

/* Returns a key from the keyboard buffer, if possible. */
unsigned char getchar(void) {
	/* If no characters are available, loop until there's something. */
	while (keybuffer->counter == 0) {
		asm volatile("hlt");
	}

	assert(keybuffer->counter != 0);
	unsigned char ret = *(keybuffer->read_ptr++);
	keybuffer->counter--;
	if (keybuffer->read_ptr > keybuffer->data + KEYBUFFER_SIZE)
		keybuffer->read_ptr = keybuffer->data;

	return ret;
}

void keyboard_callback(registers_t regs) {
	/* 
	 * Note: This code ignores escaped scancodes (0xe0 0x*) for now.
	 * After looking through a table of possibilities, none of them
	 * matter much! For instance, they are used to differ between
	 * left and right ctrl/alt, Keypad enter and return...
	 * Since there's no current support for arrow keys/the keypad,
	 * ignoring the 0xe0 byte means nothing bad.
	 */

	regs = regs; // avoid a warning
	unsigned char scancode = inb(0x60);
	unsigned char c;
	if (scancode == 0xe0)
		return; // For now

	if (mod_keys == (MOD_CTRL | MOD_ALT) && scancode == 0xd3) {
		// Ctrl+Alt+Delete!
		// I'm not sure about the proper keycode here.
		// 0xd3 is sent when Fn+backspace is released (0x53 on press).
		// There doesn't appear to BE a keycode sent on keydown with ctrl+alt+fn pressed.
		reset();
	}

//	printk("in: %02x\n", scancode);


	/*
	 * Check for modifier keycodes. If present, toggle their state (if necessary).
	 */
	switch (scancode) {
		case 0x2a: /* shift down */
		case 0x36: /* right shift down */
			mod_keys |= MOD_SHIFT;
			return;
			break;
		case 0xaa: /* shift up */
		case 0xb6: /* right shift up */
			mod_keys &= ~MOD_SHIFT;
			return;
			break;

		case 0x1d: /* ctrl down */
			mod_keys |= MOD_CTRL;
			return;
			break;
		case 0x9d: /* ctrl up */
			mod_keys &= ~MOD_CTRL;
			return;
			break;

		case 0x38: /* alt down */
			mod_keys |= MOD_ALT;
			return;
			break;
		case 0xb8: /* alt up */
			mod_keys &= ~MOD_ALT;
			return;
			break;

		default:
			break;
	}

	/* We're still here, so the scancode wasn't a modifier key changing state */

	if (mod_keys == MOD_NONE && !(scancode & 0x80)) {
		// No modifiers
		c = kbdmix[scancode];
	}
	else if (mod_keys == MOD_SHIFT && !(scancode & 0x80)) {
		// Shift + key
		c = kbdse_shift[scancode];
	}
	else if (mod_keys == MOD_ALT && !(scancode & 0x80)) {
		// Alt + key
		c = kbdse_alt[scancode];
	}
	else if ( !(scancode & 0x80) ) { // scancode isn't simply a supported key being released
		printk("Not implemented (scancode = %02x)\n", scancode);
		return;
	}
	else if (scancode & 0x80) {
		// Key was released
		return;
	}

	/* Add the key to the ring buffer */
	if (c == 0)
		return;

	if (keybuffer->counter == KEYBUFFER_SIZE) 
		panic("Keyboard ring buffer full! This shouldn't happen without bugs somewhere...");

	*(keybuffer->write_ptr++) = c;
	keybuffer->counter++;

	/* Wrap the write pointer */
	if (keybuffer->write_ptr > keybuffer->data + KEYBUFFER_SIZE)
		keybuffer->write_ptr = keybuffer->data;

#if 0
	if (!(scancode & 0x80) && c != 0) {
		putchar(c);

		if (c == 0x08) {
			/* 
			 * If this is a backspace, remove the previous character.
			 * The cursor is decremented only because putchar increments it!
			 * This isn't done in putchar() because printing a \b should only
			 * move the cursor, not actually delete anything.
			 */
			putchar(' '); 
			cursor.x--;
		}
		update_cursor(); /* as of right now, putchar() doesn't call this to save resources */
	}
#endif

}
