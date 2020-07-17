/*
 * dataflash.c
 *
 * Created: 05-Sep-16 10:38:51
 *  Author: M43978
 */

#include "dataflash.h"

volatile uint32_t offset = 0;

static uint8_t spi(uint8_t byte)
{
	SPI0.DATA = byte;
	while (!(SPI0.INTFLAGS & SPI_IF_bm))
		;
	return SPI0.DATA;
}

static void tx_spi(uint8_t d)
{
	spi(d);
}

static uint8_t rx_spi(void)
{
	return spi(0xFF);
}

static void poll_SO()
{
	CE_LOW();
	while (!(DF_PORT.IN & SO))
		;
	CE_HIGH();
}

static void send_cmd(uint8_t opcode)
{
	CE_LOW();
	tx_spi(opcode);
	CE_HIGH();
}

static void send_address(uint32_t addr)
{
	tx_spi(((addr >> 16) & 0xFF));
	tx_spi(((addr >> 8) & 0xFF));
	tx_spi((addr & 0xFF));
}

uint8_t dataflash_init()
{
	DF_PORT.DIRCLR = SO;
	DF_PORT.DIRSET = (SI | SCK | CE);
	SPI0.CTRLA     = SPI_MASTER_bm | SPI_PRESC_DIV4_gc | SPI_CLK2X_bm;
	SPI0.CTRLB     = SPI_SSD_bm;
	SPI0.CTRLA |= SPI_ENABLE_bm;

	CE_HIGH();

	uint8_t status_register = dataflash_read_status_register();

	if (status_register != 0x00) {
		dataflash_write_status_register(0x00);
		status_register = dataflash_read_status_register();
	}

	if (status_register != 0x00) {
		return 1;
	}

	return 0;
}

uint8_t dataflash_read_status_register()
{
	CE_LOW();
	tx_spi(OP_RDSR);
	uint8_t byte = rx_spi();
	CE_HIGH();
	return byte;
}

void dataflash_poll_status_register(uint8_t mask)
{
	uint8_t status;
	do {
		status = dataflash_read_status_register();
	} while (status & mask);
}

void dataflash_write_status_register(uint8_t byte)
{
	send_cmd(OP_EWSR);

	CE_LOW();
	tx_spi(OP_WRSR);
	tx_spi(byte);
	CE_HIGH();
}

uint8_t dataflash_read_id(uint32_t addr)
{
	uint8_t byte;
	CE_LOW();
	tx_spi(OP_RDID);
	send_address(addr);
	byte = rx_spi();
	CE_HIGH();
	return byte;
}

uint32_t dataflash_jedec_id_read()
{
	uint32_t id = 0;
	CE_LOW();
	tx_spi(OP_JEDEC_ID);
	id = (id | rx_spi()) << 8;
	id = (id | rx_spi()) << 8;
	id = (id | rx_spi());
	CE_HIGH();
	return id;
}

uint8_t dataflash_read_byte(uint32_t addr)
{
	while (dataflash_read_status_register() & BUSY)
		;

	CE_LOW();
	tx_spi(OP_READ);
	send_address(addr);
	uint8_t byte = rx_spi();
	CE_HIGH();

	return byte;
}

void dataflash_read_multiple_start(uint32_t addr)
{
	CE_LOW();
	tx_spi(OP_READ);

	send_address(addr);
	offset = addr;
}

void dataflash_read_multiple_continue(uint8_t *buff, uint8_t buff_length)
{
	do {
		*(buff++) = rx_spi();
		offset++;
	} while (--buff_length);

	if (offset == END_ADDRESS) {
		offset = 0;
	}
}

void dataflash_read_multiple_stop()
{
	CE_HIGH();
}

void dataflash_program_byte(uint32_t addr, uint8_t byte)
{
	send_cmd(OP_WREN);
	while (!(dataflash_read_status_register() & WEN))
		;

	CE_LOW();
	tx_spi(OP_BYTE_PROG);
	send_address(addr);
	tx_spi(byte);
	CE_HIGH();

	while (dataflash_read_status_register() & BUSY)
		;
}

static void AAI_program_start(uint32_t addr)
{
	offset = addr;
	if (offset > (END_ADDRESS - 2)) {
		offset -= 2;
	}

	send_cmd(OP_EBSY);
	send_cmd(OP_WREN);

	CE_LOW();
	tx_spi(OP_AAI_WORD_PROG);
	send_address(addr);
}

static void AAI_program_continue(uint8_t byte1, uint8_t byte2)
{
	if (!CE_IS_LOW()) {
		CE_LOW();
		tx_spi(OP_AAI_WORD_PROG);
	}
	tx_spi(byte1);
	tx_spi(byte2);
	CE_HIGH();

	offset += 2;

	poll_SO();
}

static void AAI_program_stop(void)
{
	send_cmd(OP_WRDI);
	send_cmd(OP_DBSY);
}

void dataflash_program_multiple_start(uint32_t addr)
{
	AAI_program_start(addr);
}

uint8_t dataflash_program_multiple_continue(uint8_t *buff, uint8_t length)
{
	uint8_t byte1, byte2;
	do {
		byte1 = *(buff++);
		byte2 = *(buff++);
		AAI_program_continue(byte1, byte2);
		length -= 2;
	} while ((length) && (offset < END_ADDRESS));

	if (offset >= END_ADDRESS) {
		/* If end of memory */
		return 1;
	} else {
		return 0;
	}
}

void dataflash_program_multiple_stop()
{
	AAI_program_stop();
}

void dataflash_erase_chip(void)
{
	send_cmd(OP_WREN);
	send_cmd(OP_ERASE_CHIP);

	dataflash_poll_status_register(BUSY);
	if (dataflash_read_status_register() & WEN) {
		send_cmd(OP_WRDI);
	}
}

static void erase(uint8_t opcode, uint32_t addr)
{
	send_cmd(OP_WREN);

	CE_LOW();
	tx_spi(opcode);
	send_address(addr);
	CE_HIGH();

	dataflash_poll_status_register(BUSY);
	send_cmd(OP_WRDI);
}

void dataflash_erase_sector_4k(uint32_t addr)
{
	erase(OP_ERASE_4K, addr);
}

void dataflash_erase_block_32k(uint32_t addr)
{
	erase(OP_ERASE_32K, addr);
}

void dataflash_erase_block_64k(uint32_t addr)
{
	erase(OP_ERASE_64K, addr);
}
