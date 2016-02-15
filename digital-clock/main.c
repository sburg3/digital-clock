/*
 * DigitalClock.c
 *
 * Created: 2/12/2016 5:33:11 PM
 * Author : Steven
 */ 

#define F_CPU 1000000UL
#include <avr/io.h>
#include <util/delay.h>
#include "debounce.h"
#include "i2cmaster.h"

#define DRV_DIG_START 0x01
#define DRV_MODE 0x09
#define DRV_INTENSITY 0x0A
#define DRV_LIMIT 0x0B
#define DRV_ENA 0x0C

#define RTC_ADDR 0xD0
#define RTC_10SEC_MASK 0x70
#define RTC_SEC_MASK 0x0F
#define RTC_10MIN_MASK 0x70
#define RTC_MIN_MASK 0x0F
#define RTC_AM_MASK 0x20
#define RTC_10HR_MASK 0x10
#define RTC_HR_MASK 0x0F
#define RTC_DOW_MASK 0x03
#define RTC_10DATE_MASK 0x30
#define RTC_DATE_MASK 0x0F
#define RTC_10MONTH_MASK 0x10
#define RTC_MONTH_MASK 0x0F
#define RTC_10YR_MASK 0xF0
#define RTC_YR_MASK 0x0F


void write_spi(unsigned char, unsigned char);
void update_drv_time(void);
void update_drv_date(void);
void config_rtc(void);
void read_rtc(void);
void write_rtc(void);

ISR(TIMER0_OVF_vect)
{
	debounce();
}

volatile char sec;
volatile char min;
volatile char hrs;
volatile char dow;
volatile char date;
volatile char month;
volatile char year;
volatile char ctl;

int main(void)
{
	unsigned char intens = 0x07;
	
	//Turn on life led
	DDRB |= _BV(DDB0);
	PORTB |= _BV(PORTB0);
	
	//Set up Timer
	TCCR0B = _BV(CS01) | _BV(CS00);
	TIMSK0 = _BV(TOIE0);
	
	debounce_init();
	sei();
	
	//Set up SPI
	//B5 - SCK, B4 - MISO, B3 - MOSI, B2 - /SS
	DDRB |= _BV(DDB5) | _BV(DDB3) | _BV(DDB2);
	SPCR |= _BV(SPE) | _BV(MSTR);
	
	//Set up I2C
	i2c_init();
	
	//Initialize RTC
	config_rtc();
	
	//Initialize LED Driver
	write_spi(DRV_LIMIT, 0x05);
	write_spi(DRV_MODE, 0xFF);
	write_spi(DRV_INTENSITY, intens);
	write_spi(DRV_ENA, 0x01);
		
	sec = 0b00100001;
	min = 0b01000011;
	hrs = 0b01100110;
	dow = 0b00000001;
	date = 0b00010100;
	month = 0b00000010;
	year = 0b00010110;
	
	write_rtc();
	
	while(1)
	{
		if(button_down(BTNINC_MASK))
		{
			if(intens < 0xE) intens++;
		}
		if(button_down(BTNDEC_MASK))
		{
			if(intens > 0x0) intens--;
		}
		
		if(PIND & _BV(PIND3))
		{
			update_drv_date();
		} 
		else 
		{
			update_drv_time();
		}
		
		read_rtc();

		write_spi(DRV_INTENSITY,intens);
	}
}

void write_spi(unsigned char high_byte, unsigned char low_byte)
{
	PORTB &= ~_BV(PORTB2);
	SPDR = high_byte;
	while(!(SPSR & _BV(SPIF)));
	SPDR = low_byte;
	while(!(SPSR & _BV(SPIF)));
	PORTB |= _BV(PORTB2);
}

void update_drv_time(void)
{
	write_spi(DRV_DIG_START + 0, (sec & RTC_SEC_MASK));
	write_spi(DRV_DIG_START + 1, (sec & RTC_10SEC_MASK) >> 4);
	write_spi(DRV_DIG_START + 2, (min & RTC_MIN_MASK));
	write_spi(DRV_DIG_START + 3, (min & RTC_10MIN_MASK) >> 4);
	write_spi(DRV_DIG_START + 4, (hrs & RTC_HR_MASK));
	write_spi(DRV_DIG_START + 5, (hrs & RTC_10HR_MASK) >> 4);
}

void update_drv_date(void)
{
	write_spi(DRV_DIG_START + 0, (year & RTC_YR_MASK));
	write_spi(DRV_DIG_START + 1, (year & RTC_10YR_MASK) >> 4);
	write_spi(DRV_DIG_START + 2, (date & RTC_DATE_MASK));
	write_spi(DRV_DIG_START + 3, (date & RTC_10DATE_MASK) >> 4);
	write_spi(DRV_DIG_START + 4, (month & RTC_MONTH_MASK));
	write_spi(DRV_DIG_START + 5, (month & RTC_10MONTH_MASK) >> 4);
}

void config_rtc(void)
{
	i2c_start_wait(RTC_ADDR + I2C_WRITE);
	i2c_write(0x07); //set ctrl reg
	i2c_write(0x10); //turn off sq wave
	i2c_stop();
}

void write_rtc(void)
{
	i2c_start_wait(RTC_ADDR + I2C_WRITE);
	i2c_write(0x00);
	i2c_write(sec);
	i2c_write(min);
	i2c_write(hrs);
	i2c_write(dow);
	i2c_write(date);
	i2c_write(month);
	i2c_write(year);
	i2c_stop();
}

void read_rtc(void)
{
	i2c_start_wait(RTC_ADDR + I2C_WRITE);
	i2c_write(0x00);
	i2c_rep_start(RTC_ADDR + I2C_READ);
	sec = i2c_readAck();
	min = i2c_readAck();
	hrs = i2c_readAck();
	dow = i2c_readAck();
	date = i2c_readAck();
	month = i2c_readAck();
	year = i2c_readNak();
	i2c_stop();
}