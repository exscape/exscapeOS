#include <types.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/ata.h>
#include <kernel/interrupts.h>
#include <string.h>

/* Globals */
ata_channel_t channels[2];
ata_device_t devices[4];

static void ata_interrupt_handler(uint32 esp) {
	/* Minus 32 to map from ISR number to IRQ number (14 or 15), then
	 * minus 14 to map from IRQ number (14 or 15) to channel (0 or 1 aka. primary or secondary). */
	uint8 channel = ((registers_t *)esp)->int_no - 32 - 14;
	assert(channel == 0 || channel == 1);
	esp=esp;
	panic("ATA interrupt!");
}

static uint8 ata_reg_read(uint8 channel, uint16 reg) {
	assert(channel == 0 || channel == 1);

	if (reg == ATA_REG_DEV_ALT_STATUS) {
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

static void ata_reg_write(uint8 channel, uint16 reg, uint8 data) {
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

static void ata_error(uint8 channel, uint8 status) {
	assert(status & ATA_SR_ERR);
	assert(!(status & ATA_SR_BSY));
	assert(!(status & ATA_SR_DRQ));
	uint8 err = ata_reg_read(channel, ATA_REG_ERROR);

	printk("ATA error code: %02x\n", err);
	panic("ATA error!");
}

void ata_init(void) {
	disable_interrupts();

	/* make sure there's no nonsense in the structures we use */
	memset(&channels, 0, sizeof(channels));
	memset(&devices, 0, sizeof(devices));

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
	ata_reg_write(ATA_PRIMARY, ATA_REG_DEV_CONTROL, ATA_REG_DEV_CONTROL_NIEN);
	ata_reg_write(ATA_SECONDARY, ATA_REG_DEV_CONTROL, ATA_REG_DEV_CONTROL_NIEN);

	for (int ch = 0; ch < 2; ch++) {
		for  (int drive = 0; drive < 2; drive++) {

			/* select the drive */
			ata_reg_write(ch, ATA_REG_DRIVE_SELECT, ATA_DRIVE | (drive << 4));

			/* This drive's entry in the /devices/ structure */
			uint8 dev = ch * 2 + drive;

			/* wait for the select to take effect */
			for (int i=0; i<4; i++)
				ata_reg_read(ch, ATA_REG_STATUS);

			/* Prepare for identify */
			ata_reg_write(ch, ATA_REG_SECTOR_COUNT, 0);
			ata_reg_write(ch, ATA_REG_LBA_LO, 0);
			ata_reg_write(ch, ATA_REG_LBA_MID, 0);
			ata_reg_write(ch, ATA_REG_LBA_HI, 0);

			/* Send the identify command */
			ata_reg_write(ch, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

			/* check the status reg */
			uint8 status = ata_reg_read(ch, ATA_REG_STATUS);

			if (status == 0) {
				/* drive does not exist */
				devices[dev].exists = false;
				continue;
			}
			
			/* wait for the busy flag to clear */
			while (true) {
				if (! (status & ATA_SR_BSY))
					break;
				status = ata_reg_read(ch, ATA_REG_STATUS);
			}

			/* check for ATAPI/SATA drives */
			uint8 lo = ata_reg_read(ch, ATA_REG_LBA_MID);
			uint8 hi = ata_reg_read(ch, ATA_REG_LBA_HI);

			if (lo == 0 && hi == 0) {
				/* Regular ATA */
				devices[dev].is_atapi = false;
			}
			else if (lo == 0x14 && hi == 0xeb) {
				/* Parallel ATAPI */
				devices[dev].is_atapi = true;
				devices[dev].exists = true; /* it exists, but should not be USED; it can be displayed, though */
				continue;
			}
			else {
				panic("Unsupported device (probably Serial ATA/Serial ATAPI)");
			}

			/* It would appear this device exists and should be used! */
			devices[dev].exists = true;
			devices[dev].channel = ch;
			devices[dev].drive = drive;

			/* Poll until DRQ or ERR is set */
			while ( (status & ATA_SR_DRQ) == 0 && (status & ATA_SR_ERR) == 0)
				status = ata_reg_read(ch, ATA_REG_STATUS);

			/* make sure there was no error */
			if ((status & ATA_SR_ERR)) {
				printk("Error on IDENTIFY: ch=%d, drive=%d\n", ch, drive);
				ata_error(ch, status);
				panic("Error on IDENTIFY");
			}

			/* DRDY should be set by now, as should DRQ */
			assert(status & ATA_SR_DRDY);
			assert(status & ATA_SR_DRQ);

			/* read the 256 words that should be waiting for us */
			uint16 words[256];
			for (int i=0; i < 256; i++)
				words[i] = inw(channels[ch].base);

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

			if (words[83] & (1 << 10)) {
				/* LBA48 is supported */
				devices[dev].capabilities |= ATA_CAPABILITY_LBA48;
			}

			/* Read the drive size (try LBA28 first) */
			uint32 lba28_size = *((uint32 *)&words[60]);
			if (lba28_size == 0 || (words[49] & (1 << 9)) == 0 /* LBA supported bit */)
				panic("Drive does not support LBA! exscapeOS does not support CHS addressing, so this drive cannot be used.");
			//lba28_size = ( (lba28_size & 0xffff) << 16 ) | ((lba28_size & 0xffff0000) >> 16);
			devices[dev].size = lba28_size;
			devices[dev].capabilities |= ATA_CAPABILITY_LBA28;

			if (devices[dev].capabilities & ATA_CAPABILITY_LBA48) {
				/* Check the LBA48 size */
				uint64 lba48_size = *((uint64 *)&words[100]);
				devices[dev].size = lba48_size;
			}

			/* Figure out the max ATA/ATAPI version this disk supports */
			devices[dev].ata_ver = 0;
			for (int i=2; i <= 14; i++) {
				if (words[80] & (1 << i))
					devices[dev].ata_ver = i;
			}

			if (devices[dev].ata_ver < 2)
				panic("Invalid ATA version for disk");

			/* Figure out the highest UDMA mode supported (not that we use it at the moment) */
			devices[dev].max_udma_mode= 0xff;
			for (int i = 0; i < 5; i++) {
				if (words[88] & (1 << i))
					devices[dev].max_udma_mode = i;
			}

			/* Check cache capabilities */
			if (words[82] & (1 << 5))
				devices[dev].capabilities |= ATA_CAPABILITY_WRITE_CACHE;
			if (words[86] & (1 << 12))
				devices[dev].capabilities |= ATA_CAPABILITY_FLUSH_CACHE;

			assert((words[82] & (1 << 4)) == 0); /* "Shall be cleared to zero ... PACKET not supported */

			strlcpy(devices[dev].model, model, 41);
			strlcpy(devices[dev].serial, serial, 21);
		} /* end drive loop */
	} /* end channel loop */
	
	/* We're done detecting drives! */

#if 1
	/* Print the info we have about each device */
	for (int dev = 0; dev < 4; dev++) {
		/* a shorthand */
		ata_device_t *d = &devices[dev];

		if (d->exists && d->is_atapi == false) {
			printk("%s %s: %s (%s) (%u sectors)\n",
				(d->channel == ATA_PRIMARY ? "Primary" : "Secondary"),
				(d->drive == ATA_MASTER ? "master" : "slave"),
				trim(d->model), /* NOTE: trim may change the string */
				trim(d->serial), /* NOTE: trim may change the string */
				d->size);

			printk("    %s %s %s %s, ATA version %u, UDMA mode %u supported\n",
					(d->capabilities & ATA_CAPABILITY_LBA28 ? "LBA28" : ""),
					(d->capabilities & ATA_CAPABILITY_LBA48 ? "LBA48" : ""),
					(d->capabilities & ATA_CAPABILITY_WRITE_CACHE ? "WRITE_CACHE" : ""),
					(d->capabilities & ATA_CAPABILITY_FLUSH_CACHE ? "FLUSH_CACHE" : ""),
					d->ata_ver,
					d->max_udma_mode);
		}
		else {
			printk("%s %s: %s\n",
					(dev <= 1 ? "Primary" : "Secondary"),
					((dev == 0 || dev == 2) ? "master" : "slave"),
					((d->exists == true && d->is_atapi == true) ? "ATAPI device" : "not present")); 
		}
	}

#endif

	//panic("Success");

	/* Re-enable ATA interrupts (clear the nIEN flag, along with SRST/HOB) */
	ata_reg_write(ATA_PRIMARY, ATA_REG_DEV_CONTROL, 0);
	ata_reg_write(ATA_SECONDARY, ATA_REG_DEV_CONTROL, 0);

	enable_interrupts();
	force_current_console = false;
}