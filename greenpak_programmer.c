/*
Experimental tool for programming GreenPAK NVM. Currently we can program RAM but NVM does not stick.

Addiva AB, John Eklund, 2021
Addiva Elektronik AB, Henrik Nordström, 2023
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

#define NVM_CONFIG 0x02 // 010 = NVM data, se s. 12
#define EEPROM_CONFIG 0x03 // 011, EEPROM data, se s. 12

#define VDD 2 // Arduino Digital Pin 2

#define NVM 0
#define EEPROM 1


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
int load_hex(char *filename, int i2c_bus, unsigned char *buf)
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


int ackPolling(int addressForAckPolling)
{
	int nack_count = 0;
	while (1) {
//		Wire.beginTransmission(addressForAckPolling); // 7-bit address of the device to transmit to (with write)
//		if (Wire.endTransmission() == 0) { // transmit write-queued bytes. Send a stop message after transmission, releasing the I2C bus
//			return 1;
//		}
		if (nack_count >= 1000)
		{
			puts("Geez! Something went wrong while programming!");
			return 0;
		}
		nack_count++;
		delay(1);
	}
}

// TODO:
// The transfer must be initiated manually by cycling the PAK VDD or by
// generating a soft reset using I2C. By setting register <1601> in address 0xC8,
// the device re-enables the Power-On Reset (POR) sequence and reloads the
// register data from the NVM into the registers.
// The functionality of the device is based upon the registers. The registers will not
// be reloaded from the NVM until power is cycled or a reset command is issued.
void powercycle()
{
	puts("Please power cycle!");
//	digitalWrite(VDD, LOW);
//	delay(500);
//	digitalWrite(VDD, HIGH);
//	puts("Done Power Cycling!");
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
	return 1;
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

// writeChip - Erases and then writes either the device’s NVM data or EEPROM data using the specified device address.
int writeChip(int i2c_bus, uint8_t device_address, block_address block, char *filename)
{
	int pagenr;
	int byteidx;
	int reg;
	unsigned char filebuf[256];
	int length;
	unsigned char value;

	if (!filename)
		err(EXIT_FAILURE, "No filename given");

	length = load_hex(filename, i2c_bus, filebuf);
	if (length < 0) {
		puts("Error: couldn't open hex file.");
		return 0;
	} else if (length < 256) {
		puts("Error: too short data (< 256) in hex file.");
		return 0;
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
	return 1;

}

void usage(void) {
	puts("GreenPak programmer - for programming NVM of SLG46824 or SLG46826\n" \
	"Usage: greenpakprog [ADAPTER_NR 1..8] [DEVICE_ADDRESS] [OPTION] <filename.hex>\n" \
	"Where OPTION is one of:\n" \
	"r = read NVM\n" \
	"e = erase NVM\n" \
	"w = write <filename.hex> to NVRAM\n" \
	"R = read EEPROM\n" \
	"W = write <filename.hex> to EEPROM\n" \
	"E = erase EEPROM\n" \
	"x = read RAM\n" \
	"X = write <filename.hex> to RAM\n" \
	"Example: greenpakprog 3 8 r\n" \
	"Example: greenpakprog 3 8 w SGL46826.hex\n");
	exit(1);
}

int main(int argc, char ** argv)
{
	int i2c_bus;
	int adapter_nr;
	int device_address;
	char selection = ' ';
	char * filename = NULL;

	if (argc < 4)
		usage();

	adapter_nr = atoi(argv[1]);
	device_address = atoi(argv[2]);
	if (strlen(argv[3]) != 1)
		usage();
	selection = argv[3][0];
	if (argc >= 5)
		filename = argv[4];

	i2c_bus = i2c_init(adapter_nr);

	switch (selection) {
	case 'r':
		readChip(i2c_bus, device_address, SLG46_NVM);
		break;
	case 'R':
		readChip(i2c_bus, device_address, SLG46_EEPROM);
		break;
	case 'x':
		readChip(i2c_bus, device_address, SLG46_RAM);
		break;
	case 'e':
		if (eraseChip(i2c_bus, device_address, SLG46_NVM) < 0)
			err(EXIT_FAILURE, "Erasing did not complete correctly!");
		break;
	case 'E':
		if (eraseChip(i2c_bus, device_address, SLG46_EEPROM) < 0)
			err(EXIT_FAILURE, "Erasing did not complete correctly!");
		break;
	case 'w':
		if (writeChip(i2c_bus, device_address, SLG46_NVM, filename) < 0)
			err(EXIT_FAILURE, "Writing did not complete correctly!");
		break;
	case 'W':
		if (writeChip(i2c_bus, device_address, SLG46_EEPROM, filename) < 0)
			err(EXIT_FAILURE, "Writing did not complete correctly!");
		break;
	case 'X':
		if (writeChip(i2c_bus, device_address, SLG46_RAM, filename) < 0)
			err(EXIT_FAILURE, "Writing did not complete correctly!");
		break;
	default:
		printf("Wrong option '%c'\n", selection);
		break;
	}
}
