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

	printf("Opened %s device\n", path);
	return file;
}

// IN:
// buf[256]
// OUT: length of data read or -1: Error opening file
int load_hex(char *filename, int i2c_file, unsigned char *buf)
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
		printf("load_hex len: %d newaddr: 0x%02X rectype: %d\n", len, newaddr, rectype);

		for (int i = 0; i < len; i++) {
			tmp[0] = *p++;
			tmp[1] = *p++;
			tmp[2] = 0;
			assert(index < 256);
			buf[index++] = strtoul(tmp, NULL, 16);
		}
	}
	printf("load_hex total len: %d\n", index);
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

// reads either the device’s NVM data or EEPROM data using the specified device address
int readChip(int i2c_file, unsigned short NVMorEEPROM)
{
	uint8_t device_address = 0x08; // Base address 8
	//int control_code = device_address << 3; // 0x08 << 3 = 0x40
	int pagenr, byteidx, reg;
	int value;
	puts("readChip");

	if (NVMorEEPROM == NVM)
		device_address |= NVM_CONFIG; // 8 | 0b010 (2) = 0xa
	else if (NVMorEEPROM == EEPROM)
		device_address |= EEPROM_CONFIG; // 8 | 0b011 (3) = 0xb

	if (ioctl(i2c_file, I2C_SLAVE_FORCE, device_address) < 0) // I2C_SLAVE. device_address / control_code
		err(errno, "Couldn't set device address '0x%02x'", device_address);
	printf("set address 0x%02x\n", device_address);

	for (pagenr = 0; pagenr < 16; pagenr++) { // 16 pages of 16 bytes in every page = reg 0..255
		//if (read(i2c_file, buf, 16) != 16)
		for (byteidx = 0; byteidx < 16; byteidx++) {
			reg = pagenr << 4 | byteidx;
			value = i2c_smbus_read_byte_data(i2c_file, reg);
			if (value < 0) {
				printf(" --");
			} else
				printf(" %02x", value);
			delay(1);
		}
		putchar('\n');
	}
	return 1;
}


// eraseChip - erases either the device’s NVM data or EEPROM data using the specified device address
// Se s. 10
int eraseChip(int i2c_file, unsigned short NVMorEEPROM)
{
	uint8_t device_address = 0x08; // always 8 when erasing register data (A10..A8 = 000)
	// Erased chip: base address 0 (0, 1, 2, 3)
	// Empty or programmed chip: base address 8 (8, 9, a, b)

	unsigned int pagenr;
	unsigned int data;

	if (NVMorEEPROM != NVM && NVMorEEPROM != EEPROM)
		return 0;

	if (ioctl(i2c_file, I2C_SLAVE_FORCE, device_address) < 0) // I2C_SLAVE
		err(errno, "Couldn't set device address '0x%02x'", device_address);
	printf("set address 0x%02x\nErasing ", device_address);
		if (NVMorEEPROM == NVM) {
			printf("NVM page:");
		} else {
			printf("EEPROM page:");
		}

	for (pagenr = 0; pagenr < 15; pagenr++) { // 0 .. 14 = cycle thru ERSEB0-3. NVM page 15 = reserved.
		printf(" %02X", pagenr);

		if (NVMorEEPROM == NVM) {
			data = 0x80 | pagenr; // ERSE (bit 7) = 1, ERSEB4 = 0 = NVM. Low nibble = page address
		} else {
			data = 0x90 | pagenr; // ERSE (bit 7) = 1, ERSEB4 = 1 = EEPROM. Low nibble = page address
		}
		i2c_smbus_write_byte_data(i2c_file, 0xE3, data); // 0xE3 == ERSR register

/* When writing to the “Page Erase Byte” (Address: 0xE3), the SLG46824/6 produces a non-I2C compliant
ACK after the “Data” portion of the I2C command. This behavior might be interpreted as a NACK
depending on the implementation of the I2C master.
Despite the presence of a NACK, the NVM and EEPROM erase functions will execute properly.
- Please reference "Issue 2: Non-I2C Compliant ACK Behavior for the NVM and EEPROM Page Erase Byte"
in the SLG46824/6 (XC revision) errata document for more information. */

		delay(100);
	}
	putchar('\n');

	powercycle();
	return 1;
}

// 1: page erase E3 till adr 8
// 2: skriv data till NVM area 8+2 = 0xa / 0+2 = 2

// writeChip - Erases and then writes either the device’s NVM data or EEPROM data using the specified device address.
int writeChip(int i2c_file, unsigned short NVMorEEPROM, char *filename)
{
	uint8_t device_address = 0; // 0x08; // Erased chip has base address 0
	int addressForAckPolling = 0x00;
	int pagenr;
	int byteidx;
	int reg;
	unsigned char filebuf[256];
	int length;
	unsigned char value;

	puts("writeChip");
	if (filename==NULL)
		return 0;

	if (NVMorEEPROM == NVM) {
		device_address |= NVM_CONFIG; // 010, se s. 12. Base address 8 => becomes 0x0a for writing
		addressForAckPolling = 0x00;
	} else if (NVMorEEPROM == EEPROM) { // 011, se s. 12
		device_address |= EEPROM_CONFIG;
		addressForAckPolling = device_address << 3;
	} else
		return 0;

	if (ioctl(i2c_file, I2C_SLAVE_FORCE, device_address) < 0) // I2C_SLAVE
		err(errno, "Couldn't set device address '0x%02x'", device_address);
	printf("set address 0x%02x\n", device_address);

	length = load_hex(filename, i2c_file, filebuf);
	if (length < 0) {
		puts("Error: couldn't open hex file.");
		return 0;
	} else if (length < 256) {
		puts("Error: too short data (< 256) in hex file.");
		return 0;
	}

printf("Setting filebuf[0xCA] to new device address 1 (was in file: %02x)\n", filebuf[0xCA]);
filebuf[0xCA] = 1;

	for (pagenr = 0; pagenr < 15; pagenr++) { // 16 pages of 16 bytes in every page = reg 0..255
		printf("Page 0x%02X:", pagenr);


		puts("Using write()");
		for (byteidx = 0; byteidx < 16; byteidx++) {
			value = filebuf[pagenr << 4 | byteidx];
			printf(" %02X", value);
		}
		putchar('\n');
//		if (i2c_smbus_write_block_data(i2c_file, pagenr << 4, 16, &filebuf[pagenr]) < 0)
		if (write(i2c_file, &(filebuf[pagenr << 4]), 16) != 16)
			err(errno, "I2C write failed");

		delay(100);
	}
	powercycle();
	return 1;

}


int main(int argc, char ** argv)
{
	int i2c_file;
	int adapter_nr = 0;

	unsigned short NVMorEEPROM = NVM; // SLG46826 also has EEPROM, however we don't use it
	bool printusage = 0;
	char selection = ' ';
	char * filename = NULL;

	if (argc < 3) {
		printusage = 1;
	} else if (strlen(argv[2]) != 1) {
		printusage = 1;
	}
	else
		selection = tolower(argv[2][0]);

	if (!printusage) {
		adapter_nr = atoi(argv[1]);
	}
	if (selection == 'w') {
		if (argc < 4)
			printusage = 1;
		else
			filename = argv[3];
	} else {
		if (argc >= 4)
			printusage = 1;
	}

	if (printusage) {
		puts("GreenPak programmer - for programming NVM of SLG46824 or SLG46826\n" \
		"Usage: greenpakprog [ADAPTER_NR 1..8] [OPTION] <filename.hex>\n" \
		"Where OPTION is one of:\n" \
		"r = read\n" \
		"e = erase\n" \
		"w = write <filename.hex>\n" \
		"Example: greenpakprog 3 r\n" \
		"Example: greenpakprog 7 w MDIO-MUX-SGL46826.hex\n");
		return 1;
	}

	i2c_file = i2c_init(adapter_nr);

	switch (selection) {
	case 'r':
		puts("Reading chip!");
		readChip(i2c_file, NVMorEEPROM);
		// puts("Done Reading!");
		break;
	case 'e':
		puts("Erasing Chip!");
		if (eraseChip(i2c_file, NVMorEEPROM)) {
			// puts("Done erasing!");
		} else {
			puts("Erasing did not complete correctly!");
		}
		delay(100);

		// ping();

		break;
	case 'w':
		puts("Writing Chip!");
/*		if (eraseChip(i2c_file, NVMorEEPROM)) {
			// puts("Done erasing!");
		} else {
			puts("Erasing did not complete correctly!");
		}
*/
		// ping();

		if (writeChip(i2c_file, NVMorEEPROM, filename)) {
			// puts("Done writing!");
		} else {
			puts("Writing did not complete correctly!");
		}

		// ping();

		readChip(i2c_file, NVMorEEPROM);
		// puts("Done Reading!");
		break;
	default:
		printf("Wrong option '%c'\n", selection);
		break;
	}
}
