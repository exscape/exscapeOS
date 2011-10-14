#include <types.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/ata.h>
#include <kernel/interrupts.h>

/* Globals */
ata_channel_t channels[2];
ata_device_t devices[4];

void ata_interrupt_handler(uint32 esp) {
	esp=esp;
	panic("ATA interrupt!");
}

uint8 ata_reg_read(uint8 channel, uint16 reg) {
	assert(channel == 0 || channel == 1);

	if (reg == ATA_REG_DEV_CONTROL) {
		return inb(channels[channel].ctrl);
	}
	else if (reg <= 7) {
		return inb(channels[channel].base + reg);
	}
	else
		panic("Invalid ATA register used it ata_reg_read()!");

	/* not reached */
	return 0;
}

void ata_reg_write(uint8 channel, uint16 reg, uint8 data) {
	assert(channel == 0 || channel == 1);

	if (reg == ATA_REG_DEV_CONTROL) {
		outb(channels[channel].ctrl, data);
	}
	else if (reg <= 7) {
		outb(channels[channel].base + reg, data);
	}
	else
		panic("Invalid ATA register used it ata_reg_read()!");
}

void ata_init(void) {
	force_current_console = true;
	register_interrupt_handler(IRQ14, ata_interrupt_handler);
	register_interrupt_handler(IRQ15, ata_interrupt_handler);

	/* Set up the channel info */
	channels[ATA_PRIMARY].base = ATA_REG_BASE_PRI;
	channels[ATA_PRIMARY].ctrl = ATA_REG_DEV_CONTROL_PRI;
	channels[ATA_PRIMARY].bmide = 0;
	channels[ATA_PRIMARY].nIEN = 1; /* disable interrupts */
	channels[ATA_SECONDARY].base = ATA_REG_BASE_SEC;
	channels[ATA_SECONDARY].ctrl = ATA_REG_DEV_CONTROL_SEC;
	channels[ATA_SECONDARY].bmide = 0;
	channels[ATA_SECONDARY].nIEN = 1; /* disable interrupts */
	
	/* Disable ATA interrupts (temporarily, while we IDENTIFY) */
	/* TODO: use ata_reg_write() */
	ata_reg_write(ATA_PRIMARY, ATA_REG_DEV_CONTROL, ATA_REG_DEV_CONTROL_NIEN);
	ata_reg_write(ATA_SECONDARY, ATA_REG_DEV_CONTROL, ATA_REG_DEV_CONTROL_NIEN);
	
	/* select the master, on the primary channel */
	ata_reg_write(ATA_PRIMARY, ATA_REG_DRIVE_SELECT, ATA_MASTER);

	/* Prepare for identify */
	ata_reg_write(ATA_PRIMARY, ATA_REG_SECTOR_COUNT, 0);
	ata_reg_write(ATA_PRIMARY, ATA_REG_LBA_LO, 0);
	ata_reg_write(ATA_PRIMARY, ATA_REG_LBA_MID, 0);
	ata_reg_write(ATA_PRIMARY, ATA_REG_LBA_HI, 0);

	/* Send the identify command */
	ata_reg_write(ATA_PRIMARY, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

	/* check the status reg */
	uint8 status = ata_reg_read(ATA_PRIMARY, ATA_REG_STATUS);

	if (status == 0) {
		/* drive does not exist */
		return;
	}
	
	/* wait for the busy flag to clear */
	while (true) {
		if (! (status & ATA_SR_BSY))
			break;
		status = ata_reg_read(ATA_PRIMARY, ATA_REG_STATUS);
	}

	/* make sure there was no error */
	if ((status & ATA_SR_ERR))
		panic("ATA error");

	/* DRDY should be set by now */
	assert(status & ATA_SR_DRDY);

	/* read the 256 words that should be waiting for us */
	uint16 words[256];
	for (int i=0; i < 256; i++)
		words[i] = inw(ATA_REG_BASE_PRI);

	char *model = ((char *)words) + 27*2;
	char *serial = (char *)words + 10*2;

	/* The data is in litle endian. We need to swap it to make it readable. */
	/* byte swap the model string data */
	for (int i = 27; i <= 46; i++) {
		words[i] = ((words[i] & 0xff) << 8) | ((words[i] & 0xff00) >> 8);
	}
	/* byte swap the serial number */
	for (int i = 10; i <= 19; i++) {
		words[i] = ((words[i] & 0xff) << 8) | ((words[i] & 0xff00) >> 8);
	}

	/* Print the info we got */
	printk("Model: %40s\n", model);
	printk("Serial: %20s\n", serial);

	panic("Success");
}
