#ifndef _SERIAL_H
#define _SERIAL_H

#define SERIAL_PORT 0x3F8 /* COM; TODO: detect in BIOS data area prior to unmapping that page (0x0) */

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

void init_serial(void);

void serial_send_byte(char c);
void serial_send(const char *str);
size_t prints(const char *fmt, ...);

#endif
