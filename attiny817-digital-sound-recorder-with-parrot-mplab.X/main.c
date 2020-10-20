/*
 * parrot.c
 *
 * Created: 02-Sep-16 13:11:07
 *  Author: M43978
 */

#include "main.h"
#include "dataflash.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define SAMPLE_FREQ (uint16_t)8000 /* Hz */

/* Buttons */
#define RECORD_PORT PORTB
#define RECORD_PIN PIN4_bm /* PB4 */
#define RECORD_CTRL RECORD_PORT.PIN4CTRL

#define PLAYBACK_PORT PORTB
#define PLAYBACK_PIN PIN5_bm /* PB5 */
#define PLAYBACK_CTRL PLAYBACK_PORT.PIN5CTRL

#define BACK_PORT PORTB
#define BACK_PIN PIN6_bm /* PB6 */
#define BACK_CTRL BACK_PORT.PIN6CTRL

#define FORWARD_PORT PORTB
#define FORWARD_PIN PIN7_bm /* PB7 */
#define FORWARD_CTRL FORWARD_PORT.PIN7CTRL

#define ERASE_PORT PORTA
#define ERASE_PIN PIN7_bm /* PA7 */
#define ERASE_CTRL ERASE_PORT.PIN7CTRL

/* LEDs */
#define LED_PORT PORTB
#define REC_LED_PIN PIN2_bm /* PB2 */
#define PWR_LED_PIN PIN3_bm /* PB3 */

/* Speaker output (DAC_OUT) */
#define SPEAKER_PORT PORTA
#define SPEAKER_PIN PIN6_bm /* PA6 */

/* Microphone input (ADC7) */
#define MIC_PORT PORTB
#define MIC_PIN PIN0_bm /* PB0 */
#define ADC_MIC_CHANNEL ADC_MUXPOS_AIN11_gc

/* Sampling Timer */
#define SAMPLE_TIMER TCA0.SINGLE
#define SAMPLE_TIMER_START() SAMPLE_TIMER.CTRLA |= TCA_SINGLE_ENABLE_bm
#define SAMPLE_TIMER_STOP() SAMPLE_TIMER.CTRLA &= ~(TCA_SINGLE_ENABLE_bm)
#define TIMER_EVENT_SOURCE EVSYS_SYNCCH0_TCA0_OVF_LUNF_gc

/* Memory related */
#define BUFFER_SIZE 128
#define ADDRESS_BYTES 4
#define DATA_START FIRST_4K

uint8_t  buffer0[BUFFER_SIZE], buffer1[BUFFER_SIZE];
uint8_t *buffers[]     = {buffer0, buffer1};
uint8_t  active_buffer = 0, alt_buffer = 1;
uint8_t  buffer_index = 0, buffer_end = 0;
uint8_t  dataflash_full   = 0;
uint32_t last_write_addr  = 0;
uint16_t table_entry_addr = 0;

void     IO_init(void);
void     sample_timer_init(void);
void     mic_hw_init(void);
void     speaker_hw_init(void);
void     record(void);
void     playback(void);
uint32_t fast_forward(uint32_t *current_position);
uint32_t go_backwards(uint32_t *current_position);
void     erase(void);
void     set_last_write_address(uint32_t address);
uint32_t get_last_write_address(void);
void     flash_led(void);

int main(void)
{
	/* Set CPU clock to 10MHz */
	_PROTECTED_WRITE(CLKCTRL_MCLKCTRLB, (CLKCTRL_PEN_bm | CLKCTRL_PDIV_2X_gc));

	/* Initialize dataflash and peripherals */
	dataflash_init();
	sample_timer_init();
	mic_hw_init();
	speaker_hw_init();

	/* Enable global interrupts */
	SREG |= (1 << 7);

	/* Get last write address for playback reference */
	last_write_addr = get_last_write_address();

	/* Initialize I/O */
	IO_init();

	/* Wait for buttons to be pressed */
	while (1) {
		/* If record button is pressed... */
		if (!(RECORD_PORT.IN & RECORD_PIN)) {
			_delay_ms(100);
			/* ...and released */
			if (!(RECORD_PORT.IN & RECORD_PIN)) {
				while (!(RECORD_PORT.IN & RECORD_PIN))
					;
			}
			/* If memory is not full */
			if (!dataflash_full) {
				record();
			} else {
				/* If memory is full indicate by flashing LED */
				flash_led();
			}
		}

		/* If playback button is pressed... */
		if (!(PLAYBACK_PORT.IN & PLAYBACK_PIN)) {
			_delay_ms(100);
			if (!(PLAYBACK_PORT.IN & PLAYBACK_PIN)) {
				/* ...and released */
				while (!(PLAYBACK_PORT.IN & PLAYBACK_PIN))
					;
			}
			/* If there is something to play */
			if (last_write_addr > (2 * BUFFER_SIZE)) {
				playback();
			}
		}

		/* If erase button is pressed... */
		if (!(ERASE_PORT.IN & ERASE_PIN)) {
			/* ...and held down for more than 2 seconds */
			_delay_ms(2000);
			if (!(ERASE_PORT.IN & ERASE_PIN)) {
				erase();
			}
		}
	}
}

/* Part of RECORD routine - Grab result from ADC & rotate buffers */
ISR(ADC0_RESRDY_vect)
{
	/* Interrupt flag cleared on reading of ADC result register */
	*(buffers[active_buffer] + buffer_index++) = ADC0.RES;

	if (buffer_index == BUFFER_SIZE) {
		/* Reset buffer index */
		buffer_index = 0;
		/* Change active buffer */
		active_buffer ^= 1;
		alt_buffer ^= 1;
		/* Buffer full flag */
		buffer_end = 1;
	}
}

void record(void)
{
	buffer_index = 0;
	buffer_end   = 0;

	/* Turn on record LED */
	LED_PORT.OUTSET = REC_LED_PIN;

	/* Start write process to dataflash */
	dataflash_program_multiple_start(last_write_addr);

	/* Enable event forwarding to ADC */
	EVSYS.ASYNCUSER1 = EVSYS_ASYNCUSER1_SYNCCH0_gc;

	/* Start sampling timer */
	SAMPLE_TIMER_START();

	do {
		if (buffer_end) {
			/* Send buffer */
			dataflash_full = dataflash_program_multiple_continue(buffers[alt_buffer], BUFFER_SIZE);
			/* Keep track of how much has been written */
			last_write_addr += BUFFER_SIZE;
			/* Clear flag */
			buffer_end = 0;
		}
		/* Keep recording until memory is full or record button is pressed again */
	} while (!(dataflash_full) && (RECORD_PORT.IN & RECORD_PIN));

	/* Stop sampling timer */
	SAMPLE_TIMER_STOP();

	/* Stop event forwarding to ADC */
	EVSYS.ASYNCUSER1 = 0;

	/* If stopped because button released */
	if (!(dataflash_full)) {
		/* Store remaining buffer */
		dataflash_program_multiple_continue(buffers[active_buffer], BUFFER_SIZE);
		last_write_addr += BUFFER_SIZE;
	}

	/* Finalize write to dataflash */
	dataflash_program_multiple_stop();

	/* Save last write address */
	set_last_write_address(last_write_addr);

	/* Wait for record button release so it doesn't just get called again */
	while (!(RECORD_PORT.IN & RECORD_PIN))
		;

	/* Turn off record LED */
	LED_PORT.OUTCLR = REC_LED_PIN;
}

/* Part of PLAYBACK routine - Load new data into DAC & rotate buffers */
ISR(TCA0_OVF_vect)
{

	/* Load next data into DAC */
	DAC0.DATA = *(buffers[active_buffer] + buffer_index++);

	/* If end of buffer reached */
	if (buffer_index == BUFFER_SIZE) {
		/* Reset buffer index */
		buffer_index = 0;
		/* Change active buffer */
		active_buffer ^= 1;
		alt_buffer ^= 1;
		/* Buffer full flag */
		buffer_end = 1;
	}

	/* Clear interrupt flag */
	SAMPLE_TIMER.INTFLAGS = 0xFF;
}

void playback(void)
{
	buffer_index = 0;
	buffer_end   = 0;

	uint32_t remaining        = last_write_addr;
	uint32_t current_position = DATA_START;

	/* Start read process from dataflash */
	dataflash_read_multiple_start(DATA_START);

	/* Read first data into buffers */
	dataflash_read_multiple_continue(buffers[active_buffer], BUFFER_SIZE);
	dataflash_read_multiple_continue(buffers[alt_buffer], BUFFER_SIZE);

	/* Keep track of how much is left */
	remaining -= 2 * BUFFER_SIZE;
	current_position += 2 * BUFFER_SIZE;

	/* Enable DAC */
	DAC0.CTRLA |= DAC_ENABLE_bm;

	/* Enable sample timer overflow ISR and start timer */
	SAMPLE_TIMER.INTCTRL = TCA_SINGLE_OVF_bm;
	SAMPLE_TIMER.CNT     = 0;
	SAMPLE_TIMER_START();

	do {
		if (buffer_end) {
			/* Fill alternate buffer with new data */
			dataflash_read_multiple_continue(buffers[alt_buffer], BUFFER_SIZE);
			remaining -= BUFFER_SIZE;
			current_position += BUFFER_SIZE;
			buffer_end = 0;
		}
		if (!(FORWARD_PORT.IN & FORWARD_PIN)) {
			SAMPLE_TIMER_STOP();
			DAC0.CTRLA &= ~(DAC_ENABLE_bm);
			dataflash_read_multiple_stop();
			current_position += fast_forward(&remaining);
			dataflash_read_multiple_start(current_position);
			dataflash_read_multiple_continue(buffers[active_buffer], BUFFER_SIZE);
			dataflash_read_multiple_continue(buffers[alt_buffer], BUFFER_SIZE);
			remaining -= 2 * BUFFER_SIZE;
			DAC0.CTRLA |= DAC_ENABLE_bm;
			SAMPLE_TIMER_START();
		}
		if (!(BACK_PORT.IN & BACK_PIN)) {
			SAMPLE_TIMER_STOP();
			DAC0.CTRLA &= ~(DAC_ENABLE_bm);
			dataflash_read_multiple_stop();
			current_position -= go_backwards(&remaining);
			dataflash_read_multiple_start(current_position);
			dataflash_read_multiple_continue(buffers[active_buffer], BUFFER_SIZE);
			dataflash_read_multiple_continue(buffers[alt_buffer], BUFFER_SIZE);
			remaining -= 2 * BUFFER_SIZE;
			current_position += 2 * BUFFER_SIZE;
			DAC0.CTRLA |= DAC_ENABLE_bm;
			SAMPLE_TIMER_START();
		}
	} while ((remaining) && (PLAYBACK_PORT.IN & PLAYBACK_PIN));

	/* Stop timer and disable sample timer overflow ISR */
	SAMPLE_TIMER_STOP();
	SAMPLE_TIMER.INTCTRL = 0;

	/* Disable DAC */
	DAC0.CTRLA &= ~(DAC_ENABLE_bm);

	/* Finalize read from dataflash */
	dataflash_read_multiple_stop();

	/* Wait for button release */
	while (!(PLAYBACK_PORT.IN & PLAYBACK_PIN))
		;
}

uint32_t fast_forward(uint32_t *current_position)
{
	uint32_t diff = *current_position;
	do {
		*current_position += 1;
		_delay_us(100);
	} while (!(FORWARD_PORT.IN & FORWARD_PIN) && (*current_position < last_write_addr));

	return (*current_position - diff);
}

uint32_t go_backwards(uint32_t *current_position)
{
	uint32_t diff = *current_position;
	do {
		*current_position -= 1;
		_delay_us(100);

	} while (!(BACK_PORT.IN & BACK_PIN) && (*current_position > (DATA_START)));
	return (diff - *current_position);
}

void erase(void)
{
	/* Flash LED to indicate button held down long enough */
	flash_led();
	/* Turn on LED */
	LED_PORT.OUTSET = REC_LED_PIN;
	/* Erase memory */
	dataflash_erase_chip();
	/* Reset table entry variables */
	last_write_addr  = DATA_START;
	table_entry_addr = 0;
	dataflash_full   = 0;
	/* Wait until button is released */
	while (!(ERASE_PORT.IN & ERASE_PIN))
		;
	/* Turn off LED */
	LED_PORT.OUTCLR = REC_LED_PIN;
}

void set_last_write_address(uint32_t addr)
{
	/* Update current table entry if not first one */
	if (dataflash_read_byte(START_ADDRESS) != 0xFF) {
		table_entry_addr += ADDRESS_BYTES;
	}

	uint8_t location = table_entry_addr;
	uint8_t byte     = 0;

	/* Split address into bytes */
	uint8_t addr_bytes[ADDRESS_BYTES]
	    = {((addr >> 24) & 0xFF), ((addr >> 16) & 0xFF), ((addr >> 8) & 0xFF), (addr & 0xFF)};

	/* Write address bytes as table entry */
	for (byte = 0; byte < ADDRESS_BYTES; byte++) {
		dataflash_program_byte(location++, addr_bytes[byte]);
	}
}

uint32_t get_last_write_address(void)
{
	uint16_t location = START_ADDRESS, byte = 0;
	/* Find most recent entry in table */
	do {
		byte = dataflash_read_byte(location++);
	} while ((byte != 0xFF) && (location < DATA_START));

	/* If there is at least one entry */
	if (location > 2) {

		uint8_t addr_bytes[ADDRESS_BYTES];

		/* Rewind to beginning of table entry */
		location -= (ADDRESS_BYTES + 1);

		/* Remember table entry */
		table_entry_addr = location;

		/* Read entry */
		for (byte = 0; byte < ADDRESS_BYTES; byte++) {
			addr_bytes[byte] = dataflash_read_byte(location++);
		}

		/* Return 32 bit value */
		return (((uint32_t)addr_bytes[0] << 24) | ((uint32_t)addr_bytes[1] << 16) | ((uint32_t)addr_bytes[2] << 8)
		        | ((uint32_t)addr_bytes[3]));

		/* If there are no entries, erase chip just to be sure and return 0 */
	} else {
		dataflash_erase_chip();
		return DATA_START;
	}
}

void flash_led(void)
{
	uint8_t i = 3;
	do {
		LED_PORT.OUTSET = REC_LED_PIN;
		_delay_ms(200);
		LED_PORT.OUTCLR = REC_LED_PIN;
		_delay_ms(200);
	} while (--i);
}

/*** Initialization Functions ***/

void IO_init(void)
{
	LED_PORT.DIRSET = (PWR_LED_PIN | REC_LED_PIN);
	LED_PORT.OUTSET = PWR_LED_PIN;
	LED_PORT.OUTCLR = REC_LED_PIN;

	/* RECORD BUTTON */
	RECORD_PORT.DIRCLR = RECORD_PIN;
	RECORD_CTRL        = PORT_PULLUPEN_bm;
	/* PLAYBACK BUTTON */
	PLAYBACK_PORT.DIRCLR = PLAYBACK_PIN;
	PLAYBACK_CTRL        = PORT_PULLUPEN_bm;
	/* ERASE BUTTON */
	ERASE_PORT.DIRCLR = ERASE_PIN;
	ERASE_CTRL        = PORT_PULLUPEN_bm;
	/* BACK BUTTON */
	BACK_PORT.DIRCLR = BACK_PIN;
	BACK_CTRL        = PORT_PULLUPEN_bm;
	/* FORWARD BUTTON */
	FORWARD_PORT.DIRCLR = FORWARD_PIN;
	FORWARD_CTRL        = PORT_PULLUPEN_bm;

	/* Add delay for pull ups to take effect */
	_delay_ms(100);
}

void sample_timer_init(void)
{
	/* Set up event channel input */
	EVSYS.SYNCCH0 = TIMER_EVENT_SOURCE;
	/* Count val = F_CPU / (SAMPLE_FREQ * 2 (ovf pr period) * PRESC_DIV) - 1 */
	SAMPLE_TIMER.PER = F_CPU / (4 * SAMPLE_FREQ);
	/* Clock prescaler */
	SAMPLE_TIMER.CTRLA = TCA_SINGLE_CLKSEL_DIV4_gc;
	/* Clear flags */
	SAMPLE_TIMER.INTFLAGS = 0xFF;

	/* Start to record, enable interrupt and start to playback */
}

void mic_hw_init(void)
{
	/* ADC input */
	MIC_PORT.DIRCLR = MIC_PIN;
	/* 8 bit resolution */
	ADC0.CTRLA = ADC_RESSEL_bm;
	/* Reference 2.5V */
	VREF.CTRLA |= VREF_ADC0REFSEL_2V5_gc;
	/* Prescaler DIV32 */
	ADC0.CTRLC = ADC_PRESC_DIV8_gc;
	/* ADC input PA7 */
	ADC0.MUXPOS = ADC_MIC_CHANNEL;
	/* Enable */
	ADC0.CTRLA |= ADC_ENABLE_bm;
	/* Start Event Input enable (start conversion on event input) */
	ADC0.EVCTRL = ADC_STARTEI_bm;
	/* Clear interrupt flags */
	ADC0.INTFLAGS = 0xFF;
	/* Enable ADC result ready interrupt */
	ADC0.INTCTRL = ADC_RESRDY_bm;

	/* Ready for enabling event forwarding to ADC and start timer to
	 * start conversions
	 */
}

void speaker_hw_init(void)
{
	/* DAC output pin */
	SPEAKER_PORT.DIRSET = SPEAKER_PIN;
	/* DAC reference 2.5V */
	VREF.CTRLA |= VREF_DAC0REFSEL_2V5_gc;
	/* Enable DAC output to pin */
	DAC0.CTRLA = DAC_OUTEN_bm;

	/* Ready to load first data and enable */
}
