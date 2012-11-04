#include <kernel/serial.h>
#include <kernel/console.h>
#include <kernel/kernutil.h>

// Port offsets
#define SERIAL_DATA 0
#define SERIAL_INT_ENABLE 1
#define SERIAL_BAUD_LOW 0 /* with DLAB set */
#define SERIAL_BAUD_HIGH 1 /* with DLAB set */
#define SERIAL_DLAB 0x80
#define SERIAL_INT_ID_FIFO 2
#define SERIAL_LCR 3 /* Line Control Register */
#define SERIAL_MCR 4 /* Modem Control Register */
#define SERIAL_LINE_STATUS 5
#define SERIAL_MODEM_STATUS 6
#define SERIAL_SCRATCH 7

static uint8 serial_reg_r(uint8 reg) {
	assert(reg <= 7);
	return inb(SERIAL_PORT + reg);
}

static void serial_reg_w(uint8 reg, uint8 value) {
	assert(reg <= 7);
	outb(SERIAL_PORT + reg, value);
}

void init_serial(void) {
	uint16 baud_divisor = 1; // 115200 baud

	serial_reg_w(SERIAL_INT_ENABLE, 0); // Disable interrutps
	serial_reg_w(SERIAL_LCR, SERIAL_DLAB); // Set DLAB, to send address
	serial_reg_w(SERIAL_BAUD_LOW, (baud_divisor & 0xff));
	serial_reg_w(SERIAL_BAUD_HIGH, (baud_divisor & 0xff00) >> 8);
	serial_reg_w(SERIAL_LCR, 0x03); // 8N1
	serial_reg_w(SERIAL_INT_ID_FIFO, 0xC7); // Enable FIFO, clear them, with 14-yte threshold (TODO: look this up)
	serial_reg_w(SERIAL_MCR, 0x0b); // Hmm
}

void serial_send_byte(char c) {
	while ((serial_reg_r(SERIAL_LINE_STATUS) & 0x20) == 0) { }
	serial_reg_w(SERIAL_DATA, c);
}

void serial_send(const char *str) {
	assert(str != NULL);
	const char *p = str;
	while (*p != NULL) {
		serial_send_byte(*p++);
	}
}
