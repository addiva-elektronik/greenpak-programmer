/* SPDX-License-Identifier: MIT
 * Copyright (C) 2021, 2023 Addiva Elektronik AB
 *
 * Tool for programming GreenPAK 46xxx chips using I2C.
 *
 * Authors:
 * 	John Eklund, Addiva AB
 * 	Henrik Nordström, Addiva Elektronik AB
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // tolower
#include <time.h> // nanosleep
#include <err.h>
#include <errno.h>

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <i2c/smbus.h>

#include <poll.h>

#include <unistd.h> // write
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <assert.h>

#include <i2c/smbus.h>

#include <getopt.h>

#ifndef DEFAULT_I2C_BUS
#define DEFAULT_I2C_BUS 2
#endif
#ifndef DEFAULT_I2C_ADDRESS
#define DEFAULT_I2C_ADDRESS 8
#endif

#define _str(_x) ""#_x
#define _(_x) _str(_x)

typedef enum {
	SLG46_RAM = 1,
	SLG46_NVM = 2,
	SLG46_EEPROM = 3,
} block_address;

/*

https://www.dialog-semiconductor.com/sites/default/files/isp_guide_slg46824_26_rev.1.2.pdf

*/

int i2c_init(int adapter_nr)
{
	char path[20];
	int file;

	snprintf(path, 19, "/dev/i2c-%d", adapter_nr);

	file = open(path, O_RDWR);
	if (file < 0)
		err(errno, "Tried to open '%s'", path);

	return file;
}

// IN:
// buf[256]
// OUT: length of data read or -1: Error opening file
int load_hex(char *filename, unsigned char *buf)
{
	char line[256];
	char tmp[5];
	char *p;
	int index = 0;
	FILE *in = fopen(filename, "r");

	if (in == NULL)
		return -1;

	while ((p = fgets(line, sizeof(line), in))) {
		if (*p++ != ':')
			continue;
		assert (strlen(p) >= 9);
		tmp[0] = *p++;
		tmp[1] = *p++;
		tmp[2] = 0;
		int len = strtoul(tmp, NULL, 16); // line length 0x 10
		strncpy(tmp, p, 4); // newaddr 0010 0020 etc
		p += 4;
		tmp[4] = 0;
		int newaddr = strtoul(tmp, NULL, 16);
		strncpy(tmp, p, 2);
		p += 2;
		tmp[2] = 0;
		int rectype = strtoul(tmp, NULL, 16);
		if (rectype == 1)
			break;
		assert(rectype == 0);
		assert(index == newaddr); // Only sequential for now.

		for (int i = 0; i < len; i++) {
			tmp[0] = *p++;
			tmp[1] = *p++;
			tmp[2] = 0;
			assert(index < 256);
			buf[index++] = strtoul(tmp, NULL, 16);
		}
	}
	return index;
}


void delay (int sleeptime_msec)
{
	struct timespec sleeptime;
	sleeptime.tv_sec = 0;
	sleeptime.tv_nsec = sleeptime_msec * 1000000; // nanoseconds from milliseconds
	nanosleep(&sleeptime, NULL);
}

void select_block(int i2c_bus, uint8_t device_address, block_address block)
{
	if (device_address & 0x7)
		err(EXIT_FAILURE, "Invalid device address %d (0x%02x)\n", device_address, device_address);

	device_address |= block;

	if (ioctl(i2c_bus, I2C_SLAVE_FORCE, device_address) < 0) // I2C_SLAVE. device_address / control_code
		err(errno, "Couldn't set device address '0x%02x'", device_address);
}

// reads either the device’s NVM data or EEPROM data using the specified device address
int readChip(int i2c_bus, uint8_t device_address, block_address block)
{
	select_block(i2c_bus, device_address, block);

	for (int pagenr = 0; pagenr < 16; pagenr++) { // 16 pages of 16 bytes in every page = reg 0..255
		uint8_t page_data[16];
		switch(block) {
		case SLG46_RAM:
			printf("RAM %xx:", pagenr);
			break;
		case SLG46_NVM:
			printf("NVM %xx:", pagenr);
			break;
		case SLG46_EEPROM:
			printf("EEPROM %xx:", pagenr);
			break;
		}
		if (i2c_smbus_read_i2c_block_data(i2c_bus, pagenr << 4, sizeof(page_data), page_data) != sizeof(page_data))
			err(EXIT_FAILURE, "Failed to read chip data");
		for (int byteidx = 0; byteidx < 16; byteidx++) {
			int value = page_data[byteidx];
			printf(" %02x", value);
		}
		putchar('\n');
	}
	return 0;
}


// Erase a single page starting at offset
int erasePage(int i2c_bus, uint8_t device_address, block_address block, uint8_t offset)
{
	uint8_t page_erase_reg;
	uint8_t pagenr = offset >> 4;

	assert(pagenr << 4 == offset);

	select_block(i2c_bus, device_address, SLG46_RAM);

	if (block == SLG46_NVM) {
		page_erase_reg = 0x80 | pagenr; // ERSE (bit 7) = 1, ERSEB4 = 0 = NVM. Low nibble = page address
	} else {
		page_erase_reg = 0x90 | pagenr; // ERSE (bit 7) = 1, ERSEB4 = 1 = EEPROM. Low nibble = page address
	}
	i2c_smbus_write_byte_data(i2c_bus, 0xE3, page_erase_reg); // 0xE3 == Page Erase Register

/* When writing to the “Page Erase Byte” (Address: 0xE3), the SLG46824/6 produces a non-I2C compliant
ACK after the “Data” portion of the I2C command. This behavior might be interpreted as a NACK
depending on the implementation of the I2C master.
Despite the presence of a NACK, the NVM and EEPROM erase functions will execute properly.
- Please reference "Issue 2: Non-I2C Compliant ACK Behavior for the NVM and EEPROM Page Erase Byte"
in the SLG46824/6 (XC revision) errata document for more information. */

	delay(100);

	return 0;
}


// eraseChip - erases either the device’s NVM data or EEPROM data using the specified device address
int eraseChip(int i2c_bus, uint8_t device_address, block_address block)
{
	switch (block) {
	case SLG46_NVM:
		printf("Erase NVM page:");
		break;
	case SLG46_EEPROM:
		printf("Erase EEPROM page:");
		break;
	default:
		err(EXIT_FAILURE, "Invalid erase block %d", block);
	}

	for (int pagenr = 0; pagenr < 15; pagenr++) { // 0 .. 14 = cycle thru ERSEB0-3. NVM page 15 = reserved.
		printf(" %Xx", pagenr);
		fflush(stdout);

		erasePage(i2c_bus, device_address, block, pagenr * 16);
	}
	putchar('\n');

	return 0;
}

int resetChip(int i2c_bus, uint8_t device_address)
{
	int reset_bit = 1601;
	uint8_t reg = reset_bit / 8;
	uint8_t mask = 1 << (reset_bit % 8);

	printf("Resetting chip");
	fflush(stdout);
	select_block(i2c_bus, device_address, SLG46_RAM);

	int value = i2c_smbus_read_byte_data(i2c_bus, reg);
	if (value < 0)
		err(EXIT_FAILURE, "Failed to read reset register");
	value |= mask;
	if (i2c_smbus_write_byte_data(i2c_bus, reg, value) < 0)
		err(EXIT_FAILURE, "Failed to write reset register");

	printf("\n");

	return 0;
}

// writeChip - Erases and then writes either the device’s NVM data or EEPROM data using the specified device address.
int writeChip(int i2c_bus, uint8_t device_address, block_address block, char *filename)
{
	int pagenr;
	unsigned char filebuf[256];
	int length;

	if (!filename)
		err(EXIT_FAILURE, "No filename given");

	length = load_hex(filename, filebuf);
	if (length < 0) {
		puts("Error: couldn't open hex file.");
		return -1;
	} else if (length < 256) {
		puts("Error: too short data (< 256) in hex file.");
		return -1;
	}

	if (block != SLG46_RAM)
		eraseChip(i2c_bus, device_address, block);

	switch (block) {
	case SLG46_NVM:
		printf("Write NVM page:");
		break;
	case SLG46_EEPROM:
		printf("Write EEPROM page:");
		break;
	case SLG46_RAM:
		printf("Write RAM page:");
		break;
	default:
		err(EXIT_FAILURE, "Invalid block %d\n", block);
	}

	select_block(i2c_bus, device_address, block);

	for (pagenr = 0; pagenr < 15; pagenr++) { // 16 pages of 16 bytes in every page = reg 0..255
		printf(" %Xx", pagenr);
		fflush(stdout);
		if (i2c_smbus_write_i2c_block_data(i2c_bus, pagenr << 4, 16, &filebuf[pagenr*16]) < 0)
			err(errno, "I2C write failed");

		if (block != SLG46_RAM)
			delay(100);
	}
	printf("\n");

	return 0;
}

void usage(void) {
	puts("GreenPak programmer - for programming NVM of SLG46824 or SLG46826\n" \
	"Usage: greenpak-programmer [OPTION] [<filename.hex>]\n" \
	" -i --bus <number>     I2C Bus number or device (default " _(DEFAULT_I2C_BUS) ")\n"
	" -u --device <number>  Device base address on the bus (default " _(DEFAULT_I2C_ADDRESS) ")\n"
	" -w --write <file>     Write contents of file to device\n"
	" -e --erase            Erase chip without writing a file\n"
	" -r --read             Read chip content\n"
	" -x --ram              Operate on RAM / emulation\n"
	" -n --nvm              Operate on NVM (default)\n"
	" -N --eeprom           Operate on EEPROM\n"
	" -R --reset            Rese the chip\n"
	"Default with only filename specified is write NVM with reset\n"
	);
	exit(1);
}

int main(int argc, char ** argv)
{
	int i2c_bus;
	int adapter_nr = DEFAULT_I2C_BUS;
	int device_address = DEFAULT_I2C_ADDRESS;
	char * filename = NULL;
	block_address target = SLG46_NVM;
	enum {
		MODE_NONE, 
		MODE_READ,
		MODE_WRITE,
		MODE_ERASE,
		MODE_RESET,
	} mode = MODE_NONE;
	bool do_reset = false;

	while (1) {
		int c;
		static const struct option long_options[] = {
			{"help", no_argument, NULL, 'h'},
			{"bus", required_argument, NULL, 'i'},
			{"device", required_argument, NULL, 'u'},
			{"write", required_argument, NULL, 'w'},
			{"erase", no_argument, NULL, 'e'},
			{"read", no_argument, NULL, 'r'},
			{"reset", no_argument, NULL, 'R'},
			{"ram", no_argument, NULL, 'x'},
			{"nvm", no_argument, NULL, 'n'},
			{"eeprom", no_argument, NULL, 'N'},
			{0, 0, 0, 0 },
		};

		c = getopt_long(argc, argv, "hi:u:w:erRxnN", long_options, NULL);
		if (c < 0)
			break;

		switch(c) {
		case 'h':
			usage();
			break;
		case 'i':
			adapter_nr = atoi(optarg);
			break;
		case 'u':
			device_address = atoi(optarg);
			break;
		case 'w':
			mode = MODE_WRITE;
			filename = optarg;
			break;
		case 'e':
			mode = MODE_ERASE;
			break;
		case 'r':
			mode = MODE_READ;
			break;
		case 'R':
			do_reset = true;
			if (mode == MODE_NONE)
				mode = MODE_RESET;
			break;
		case 'x':
			target = SLG46_RAM;
			break;
		case 'n':
			target = SLG46_NVM;
			break;
		case 'N':
			target = SLG46_EEPROM;
			break;
		default:
			err(EXIT_FAILURE, "Unknown option '%c'", c);
			break;
		}
	}

	if (mode == MODE_NONE && optind < argc) {
		mode = MODE_WRITE;
		if (target == SLG46_NVM)
			do_reset = true;
		filename = argv[optind++];
	}

	if (optind < argc)
		usage();

	if (mode == MODE_NONE && !do_reset)
		usage();

	i2c_bus = i2c_init(adapter_nr);

	switch (mode) {
	case MODE_RESET:
	case MODE_NONE:
		break;
	case MODE_READ:
		readChip(i2c_bus, device_address, target);
		break;
	case MODE_WRITE:
		if (writeChip(i2c_bus, device_address, target, filename) < 0)
			err(EXIT_FAILURE, "Writing did not complete correctly!");
		if (target == SLG46_NVM && !do_reset)
			fprintf(stderr, "Notice: Chip needs reset to activate new config\n");
		break;
	case MODE_ERASE:
		if (eraseChip(i2c_bus, device_address, target) < 0)
			err(EXIT_FAILURE, "Erasing did not complete correctly!");
		break;
	}

	if (do_reset) {
		if (resetChip(i2c_bus, device_address) < 0)
			err(EXIT_FAILURE, "Could not reset chip");
	}
}
