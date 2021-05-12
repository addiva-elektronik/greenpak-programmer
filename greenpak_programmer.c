/*
Experimental tool for programming GreenPAK NVM. Currently we can program RAM but NVM does not stick.

Compiling:
 gcc -static greenpak_programmer.c -o greenpak_programmer
i e:
 CROSS_COMPILE := ~/UGW/ugw_sw/openwrt/staging_dir/toolchain-mips_24kc+nomips16_gcc-8.3.0_musl/bin/mips-openwrt-linux-musl-
 CC	:= $(CROSS_COMPILE)gcc

Addiva AB, John Eklund, 2021
*/

// #include <Wire.h> // Arduino I2C
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

#define NVM_CONFIG 0x02 // 010 = NVM data, se s. 12
#define EEPROM_CONFIG 0x03 // 011, EEPROM data, se s. 12

#define VDD 2 // Arduino Digital Pin 2

#define NVM 0
#define EEPROM 1

/*

https://www.dialog-semiconductor.com/sites/default/files/isp_guide_slg46824_26_rev.1.2.pdf

https://www.kernel.org/doc/html/latest/i2c/dev-interface.html
IMPORTANT: because of the use of inline functions, you have to use ‘-O’ or some variation when you compile your program!

https://github.com/shenki/linux-i2c-example
Simple I2C example

https://github.com/bentiss/i2c-read-register/blob/master/i2c-read-register.c

https://www.arduino.cc/en/Reference/WireSetClock
Arduino I2C Wire reference

*/

// static inline
__s32 i2c_smbus_access(int file, char read_write, __u8 command, int size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args;

	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;
	return ioctl(file, I2C_SMBUS, &args);
}

__s32 i2c_smbus_write_quick(int file, __u8 value)
{
	return i2c_smbus_access(file, value, 0, I2C_SMBUS_QUICK, NULL);
}

__s32 i2c_smbus_read_byte(int file)
{
	union i2c_smbus_data data;
	if (i2c_smbus_access(file, I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &data))
		return -1;
	else
		return 0x0FF & data.byte;
}

__s32 i2c_smbus_write_byte(int file, __u8 value)
{
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}

__s32 i2c_smbus_read_byte_data(int file, __u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_BYTE_DATA ,&data))
		return -1;
	else
		return 0x0FF & data.byte;
}

__s32 i2c_smbus_write_byte_data(int file, __u8 command, __u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_BYTE_DATA, &data);
}

__s32 i2c_smbus_read_word_data(int file, __u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_WORD_DATA, &data))
		return -1;
	else
		return 0x0FFFF & data.word;
}

__s32 i2c_smbus_write_word_data(int file, __u8 command, __u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_access(file,I2C_SMBUS_WRITE,command, I2C_SMBUS_WORD_DATA, &data);
}

__s32 i2c_smbus_process_call(int file, __u8 command, __u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	if (i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_PROC_CALL, &data))
		return -1;
	else
		return 0x0FFFF & data.word;
}


/* Returns the number of read bytes */
__s32 i2c_smbus_read_block_data(int file, __u8 command, __u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_BLOCK_DATA, &data))
		return -1;
	else {
		for (i = 1; i <= data.block[0]; i++)
			values[i-1] = data.block[i];
		return data.block[0];
	}
}

__s32 i2c_smbus_write_block_data(int file, __u8 command, __u8 length, const __u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > 32)
		length = 32;
	for (i = 1; i <= length; i++)
		data.block[i] = values[i-1];
	data.block[0] = length;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_BLOCK_DATA, &data);
}
/*
Omskrivna. Tveksamt om dessa kan fungera då längd nu inte skickas in till ioctl (parameter 4 "size" = konstant I2C_SMBUS_BLOCK_DATA)
__s32 i2c_smbus_read_block_data_raw(int file, __u8 command, __u8 length, __u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_BLOCK_DATA, &data))
		return -1;
	else {
		for (i = 0; i < length; i++)
			values[i] = data.block[i];
		return length;
	}
}

__s32 i2c_smbus_write_block_data_raw(int file, __u8 command, __u8 length, const __u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > 32)
		length = 32;
	for (i = 0; i < length; i++)
		data.block[i] = values[i];
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_BLOCK_DATA, &data);
}
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
/* Note that only a subset of the I2C and SMBus protocols can be achieved by the means of
read() and write() calls. In particular, so-called combined transactions
(mixing read and write messages in the same transaction) aren’t supported.
For this reason, this interface is almost never used by user-space programs.

// Using I2C Write, equivalent of i2c_smbus_write_word_data(file, reg, 0x6543):
	buf[0] = reg;
	buf[1] = 0x43;
	buf[2] = 0x65;
	if (write(file, buf, 3) != 3)
		err(errno, "I2C write failed");
// Using I2C Read, equivalent of i2c_smbus_read_byte(file):
	if (read(file, buf, 1) != 1)
		err(errno, "I2C read failed");
	else {
		// buf[0] contains the read byte
	}
*/

/*
int i2c_read(int file, uint8_t reg)
{
	int data; // uint8_t
	// Using SMBus commands:

//	data = i2c_smbus_read_word_data(file, reg);
	data = i2c_smbus_read_byte_data(file, reg);
	if (data < 0)
		err(errno, "SMBUS read failed: %d", data);

	printf("i2c read register 0x%02x: 0x%02x\n", reg, data);
	return data;
}

void i2c_write(int file, uint8_t reg, int data) // uint8_t data
{
	// Using SMBus commands:

//	data = i2c_smbus_write_word_data(file, reg, data);
	data = i2c_smbus_write_byte_data(file, reg, data);
	if (data < 0)
		err(errno, "SMBUS write failed: %d", data);

	printf("i2c write register 0x%02x: 0x%02x\n", reg, data);
}
*/



///////////////

int i2c_set_registers(int file, int page_addr, char *data, size_t len)
{
	char buf[len + 1];
	buf[0] = page_addr; // byte-adress 0, 16, 32 per 16-bytes-block för det hex-data som skrivs
puts("i2c_set_registers");
	memcpy(&buf[1], data, len);

//	if (write(file, buf, len + 1) != (ssize_t)(len + 1)) {
//		perror("I2c write:");
//		exit(1);
//	}
	return 0;
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
	return index; // i2c_set_registers(i2c_file, 0, buf, len);
}
// i2c_set_registers(i2c_file, 0, buf, len);


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

/*
// i2c_readbuf - test att läsa med read() till buffert. Fungerade ej korrekt.
// Även med i2c-read-register.c (nedladdat exempel som använde read())
void i2c_readbuf(int i2c_file)
{
	unsigned char buf[16];
	buf[0]=0x11; // Specify which register to read
#define POLL_I2C_READ
#ifdef POLL_I2C_READ
	struct pollfd pfd;
	pfd.fd = i2c_file;
	int p;
#endif

	if (write(i2c_file, buf, 1) != 1) {
		perror("I2c write:");
		exit(1);
	}
#define POLL_I2C_READ

#ifdef POLL_I2C_READ
	p = poll(&pfd, 1, 100); // 11 ms
	if (p == 0)
		err(errno, "Poll timeout expired\n");
	else if (p == -1)
		err(errno, "Poll error");
	else {
#endif
		if (read(i2c_file, buf, 2) != 2)
			err(errno, "I2C read failed");
		//printf("%02x\n", buf[0]);
		printf("%02x%02x\n", buf[0], buf[1]);
	}
}
*/

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

// Sätt till 8 tillfälligt hårdkodat för att läsa RAM på chip som RAM-programmerats med bootscriptet:
// device_address = 8;
// device_address = 0x57; // 8 / 0xa

	if (ioctl(i2c_file, I2C_SLAVE_FORCE, device_address) < 0) // I2C_SLAVE. device_address / control_code
		err(errno, "Couldn't set device address '0x%02x'", device_address);
	printf("set address 0x%02x\n", device_address);

// GreenPak sitter på i2cbus 1-8 kanal 8-a-b
// i2cdump -f -y 1 8
// i2cdump -f -y 1 0xa
// i2cdump -f -y 2 8
// i2cdump -f -y 2 0xa
// ...
// I Dialogs ISP-guide för Greenpak är det adress.

	for (pagenr = 0; pagenr < 16; pagenr++) { // 16 pages of 16 bytes in every page = reg 0..255
		//if (read(i2c_file, buf, 16) != 16)
		for (byteidx = 0; byteidx < 16; byteidx++) {
			reg = pagenr << 4 | byteidx;
			value = i2c_smbus_read_byte_data(i2c_file, reg);
			if (value < 0) {
				printf(" --");
//				err(errno, "I2C read failed");
			} else
				printf(" %02x", value);
			delay(1);
	//		Wire.beginTransmission(control_code); // 7-bit address of the device to transmit to (with write)
	//		Wire.write(i << 4); // write=queue bytes for transmission
				// endTransmission (false): sends a restart message after transmission. The bus will not
				// be released, which prevents another master device from transmitting between messages.
				// This allows one master device to send multiple transmissions while in control.
	//		Wire.endTransmission(false); // transmit write-queued bytes
			// requestFrom: request bytes from a device. Then retrieved with the available() and read() functions.
			// Sends a stop message after the request, releasing the I2C bus.
			// control_code: the 7-bit address of the device to request bytes from. 16: number of bytes to request
	//		Wire.requestFrom(control_code, 16);

	//		while (Wire.available()) { // Returns the number of bytes available for retrieval with read
	//			printf("0x%02X", Wire.read()); // Reads a byte
	//		}
		}
		putchar('\n');
	}
//	close(i2c_file);
	return 1;
}


// eraseChip - erases either the device’s NVM data or EEPROM data using the specified device address
// Se s. 10
int eraseChip(int i2c_file, unsigned short NVMorEEPROM)
{
	uint8_t device_address = 0x08; // always 8 when erasing register data (A10..A8 = 000)
	// Erased chip: base address 0 (0, 1, 2, 3)
	// Empty or programmed chip: base address 8 (8, 9, a, b)

//	int control_code = device_address << 3; // Address = XXXXAAAw, se s. 9. AAA = block address (bin 010 = NVM)
//	int addressForAckPolling = control_code;
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
//		Wire.beginTransmission(control_code); // 7-bit address of the device to transmit to (with write)
//		Wire.write(0xE3); // write=queue bytes for transmission. write to the “Page Erase Byte”

		if (NVMorEEPROM == NVM) {
			data = 0x80 | pagenr; // ERSE (bit 7) = 1, ERSEB4 = 0 = NVM. Low nibble = page address
//			Wire.write(0x80 | i);
		} else {
			data = 0x90 | pagenr; // ERSE (bit 7) = 1, ERSEB4 = 1 = EEPROM. Low nibble = page address
//			Wire.write(0x90 | i);
		}
		i2c_smbus_write_byte_data(i2c_file, 0xE3, data); // 0xE3 == ERSR register

/* When writing to the “Page Erase Byte” (Address: 0xE3), the SLG46824/6 produces a non-I2C compliant
ACK after the “Data” portion of the I2C command. This behavior might be interpreted as a NACK
depending on the implementation of the I2C master.
Despite the presence of a NACK, the NVM and EEPROM erase functions will execute properly.
To accommodate for the non-I2C compliant ACK behavior of the Page Erase Byte, we've removed the
software check for an I2C ACK and added the "Wire.endTransmission();" line to generate a stop condition.
- Please reference "Issue 2: Non-I2C Compliant ACK Behavior for the NVM and EEPROM Page Erase Byte"
in the SLG46824/6 (XC revision) errata document for more information. */

//	if (Wire.endTransmission() == 0) { // send a stop message after transmission, releasing the I2C bus
//		printf("ack ");
//	}
//	else {
//		printf("nack ");
//		return 0;
//	}

//		Wire.endTransmission(); // transmit write-queued bytes. Send a stop message after transmission, releasing the I2C bus

/*		if (!ackPolling(addressForAckPolling)) {
			return 0;
		} else {
			printf("ready ");
			delay(100);
		}
*/
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
		// puts("Writing NVM");
		// Set the device address to 0x00 since the chip has just been erased
//		device_address = 0x00;
		// Set the control code to 0x00 since the chip has just been erased
		//device_address = 0x00;
		device_address |= NVM_CONFIG; // 010, se s. 12. Base address 8 => becomes 0x0a for writing
		addressForAckPolling = 0x00;
	} else if (NVMorEEPROM == EEPROM) { // 011, se s. 12
		// puts("Writing EEPROM");
		// device_address = device_address << 3;
		device_address |= EEPROM_CONFIG;
		addressForAckPolling = device_address << 3;
	} else
		return 0;

// Varje gång control_code ändras för att adressera ett annat block (se s. 12), kör ioctl
	if (ioctl(i2c_file, I2C_SLAVE_FORCE, device_address) < 0) // I2C_SLAVE
		err(errno, "Couldn't set device address '0x%02x'", device_address);
	printf("set address 0x%02x\n", device_address);

// printf("Control Code: 0x%02X\n", device_address);

puts("load_hex");
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
//		i2c_write(i2c_file, 0x08, control_code);
//		Wire.beginTransmission(control_code); // 7-bit address of the device to transmit to (with write)
//		Wire.write(i << 4); // write=queue bytes for transmission
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

////////
/*
puts("Using i2c_smbus_write_byte_data()");
		for (byteidx = 0; byteidx < 16; byteidx++) {
			reg = pagenr << 4 | byteidx;
			value = filebuf[reg];
			printf(" %02X", value);
			if (i2c_smbus_write_byte_data(i2c_file, reg, (unsigned char) filebuf[reg]) < 0)
//			if (i2c_smbus_write_byte(i2c_file, (unsigned char) filebuf[reg]) < 0)
				err(errno, "SMBUS write failed [%01X]: %d", reg, value);

			delay(1);
		}
		putchar('\n');
*/
		delay(100);
	}
//	close(i2c_file);
	powercycle();
	return 1;

//	i2c_set_registers(i2c_file, 0, filebuf, length);

// When the NVM is erased, the NVM address containing the I2C device address will be set to 0000.
// After the erase, the chip will maintain its current device address within the configuration registers until
// the device is reset as described above. Once the chip has been reset, the I2C device address must be
// set in address 0xCA within the configuration registers each time the GreenPAK is power-cycled or reset.
// This must be done until the new I2C device address page has been written in the NVM.
//if (NVMorEEPROM == NVM) {
//	data_array[0xC][0xA] = device_address_new; // = bits [1623:1620], se s. 10
//}
	/*
	Write each byte of data_array[][] array to the chip
	Wire.write(data_array[i][j]); // write=queue bytes for transmission

	if (Wire.endTransmission() == 0) { // transmit write-queued bytes. Send a stop message after transmission, releasing the I2C bus
		printf(" ack "); // 0 = success
	} else {
		puts(" nack\nOh No! Something went wrong while programming!");
		return 0;
	}

	if (!ackPolling(addressForAckPolling)) {
		return 0;
	} else {
		puts("ready\n");
		delay(100);
	}
	*/
}


int main(int argc, char ** argv)
{
	// Wire.begin(); // Initiate, join i2c bus as master
	// Wire.setClock(400000); // 400 kHz clock frequency for I2C communication
		// 100000 =standard mode, 400000 =fast mode
		// Some processors also support 10000 =low speed mode, 1000000 =fast mode plus & 3400000 =high speed mode

//	pinMode(VDD, OUTPUT);	// This will be the GreenPAK's VDD
//	digitalWrite(VDD, HIGH);
//	delay(100);

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
