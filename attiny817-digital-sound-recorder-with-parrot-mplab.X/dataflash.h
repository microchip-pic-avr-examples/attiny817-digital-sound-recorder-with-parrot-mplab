/*
 * dataflash.h
 *
 * Created: 05-Sep-16 10:42:41
 *  Author: M43978
 */

#ifndef DATAFLASH_H_
#define DATAFLASH_H_

#include <stdio.h>
#include <stdlib.h>
#include <avr/io.h>

#define DF_PORT PORTA
#define SI PIN1_bm
#define SO PIN2_bm
#define SCK PIN3_bm
#define CE PIN4_bm

#define CE_HIGH() (DF_PORT.OUTSET = CE)
#define CE_LOW() (DF_PORT.OUTCLR = CE)
#define CE_IS_LOW() (!(DF_PORT.OUT & CE))

/*** OPCODES ***/
#define OP_READ 0x03       /* Read */
#define OP_HS_READ 0x0B    /* High Speed Read */
#define OP_ERASE_4K 0x20   /* Erase 4kB of memory array */
#define OP_ERASE_32K 0x52  /* Erase 32kB of memory array */
#define OP_ERASE_64K 0xD8  /* Erase 64kB of memory array */
#define OP_ERASE_CHIP 0x60 /* Erase full memory array */
#define OP_ERASE_CHIP_ALT 0xC7
#define OP_BYTE_PROG 0x02     /* Program one data byte */
#define OP_AAI_WORD_PROG 0xAD /* Auto Address Increment Programming */
#define OP_RDSR 0x05          /* Read Status Register */
#define OP_EWSR 0x50          /* Enable Write Status Register */
#define OP_WRSR 0x01          /* Write Status Register */
#define OP_WREN 0x06          /* Write Enable */
#define OP_WRDI 0x04          /* Write Disable */
#define OP_RDID 0x90          /* Read ID */
#define OP_RDID_ALT 0xAB
#define OP_JEDEC_ID 0x9F /* JEDEC ID read */
#define OP_EBSY 0x70     /* Enable SO to output RY/BY# status during AAI programming */
#define OP_DBSY 0x80     /* Disable SO as RY/BY# status during AAI programming */

/* Addresses for Read Device ID */
#define ADD_MAN_ID 0x00 /* Address for reading Manufacturer's ID */
#define ADD_DEV_ID 0x01 /* Address for reading Device ID */

/* Masks for Status Register */
#define BUSY 0x01
#define WEN 0x02
#define B_WP 0x3C
#define AAI 0x40
#define BPL 0x80
#define POWERUP 0x1C

/* Addresses */
#define START_ADDRESS 0x00000
#define FIRST_4K 0x01000
#define FIRST_32K 0x0C800
#define FIRST_64K 0x19000
#define END_ADDRESS 0xFFFFF

uint8_t  dataflash_init();
uint8_t  dataflash_read_status_register(void);
void     dataflash_poll_status_register(uint8_t mask); /* Use masks for Status Register above */
void     dataflash_write_status_register(uint8_t byte);
uint8_t  dataflash_read_id(uint32_t addr); /* Use addresses for Read Device ID above */
uint32_t dataflash_jedec_id_read();
uint8_t  dataflash_read_byte(uint32_t addr);
void     dataflash_read_multiple_start(uint32_t addr);
void     dataflash_read_multiple_continue(uint8_t *buff, uint8_t buff_length);
void     dataflash_read_multiple_stop();
void     dataflash_program_byte(uint32_t addr, uint8_t byte);
void     dataflash_program_multiple_start(uint32_t addr);
uint8_t  dataflash_program_multiple_continue(uint8_t *buff, uint8_t length);
void     dataflash_program_multiple_stop();
void     dataflash_erase_chip(void);
void     dataflash_erase_sector_4k(uint32_t addr);
void     dataflash_erase_block_32k(uint32_t addr);
void     dataflash_erase_block_64k(uint32_t addr);

#endif /* DATAFLASH_H_ */
