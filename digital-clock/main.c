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

//SPI addresses for MAX7221
#define DRV_DIG_START 0x01
#define DRV_MODE 0x09
#define DRV_INTENSITY 0x0A
#define DRV_LIMIT 0x0B
#define DRV_ENA 0x0C

//Masks for DS1307 RTC data
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

//Clock has AM/PM led indicator
#define AM_LED_PORT PORTB0
#define PM_LED_PORT PORTB1

void write_spi(unsigned char, unsigned char);
void update_drv_time(void);
void update_drv_date(void);
void config_rtc(void);
void read_rtc(void);
void write_rtc(void);
char bin_to_bcd(char);

ISR(TIMER0_OVF_vect)
{
	debounce();
	blink_cnt++;
	if(blink_cnt > 100)
	{
		blink_cnt = 0;
		blink = ~blink;
	}	
}

//holds time data to/from RTC
volatile char sec;
volatile char min;
volatile char hrs;
volatile char dow;
volatile char date;
volatile char month;
volatile char year;
volatile char ctl;

//Order that time is set
enum set_digits {Month, Day, Year, Hour, Min};
volatile enum set_digits cur_set = Month;

//Clock runs normally in Run mode, but PB goes to set mode
enum modes {Run, Set};
volatile enum modes cur_mode = Run;

//Handle blink during set mode
volatile char blink_cnt;
volatile char blink;

int main(void)
{
	//Intensity of led display
	unsigned char intens = 0x07;
	
	char btn_cnt = 0;
	
	//Set up AM/PM leds
	DDRB |= _BV(DDB0);
	DDRB |= _BV(DDB1);
	
	//Set up Timer and debounce
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
	read_rtc();
	config_rtc();
	
	//Initialize LED Driver
	write_spi(DRV_LIMIT, 0x05);
	write_spi(DRV_MODE, 0xFF);
	write_spi(DRV_INTENSITY, intens);
	write_spi(DRV_ENA, 0x01);
	
	while(1)
	{
		//Process button inputs
		if(button_down(BTNMODE_MASK))
		{
			switch(cur_mode)
			{
				case Run: cur_mode = Set; break;
				case Set:
					if(cur_set < Min)
					{
						cur_set++;
						btn_cnt = 0;
					}
					else
					{
						cur_mode = Run;
						cur_set = Month;
						btn_cnt = 0;
						write_rtc();
					}
					break;
			}
		}
		
		if(button_down(BTNINC_MASK))
		{
			switch(cur_mode)
			{
				case Run: if(intens < 0xE) intens++; break;
				case Set: if(btn_cnt < 99) btn_cnt++; break;
			}
		}
		
		if(button_down(BTNDEC_MASK))
		{
			switch(cur_mode)
			{
				case Run: if(intens > 0) intens--; break;
				case Set: if(btn_cnt > 0) btn_cnt--; break;
			}
		}
		
		//Handles Run/Set mode differences
		if(cur_mode == Run)
		{
			read_rtc();
			if(PIND & _BV(PIND3))
			{
				update_drv_date();
			} 
			else 
			{
				update_drv_time();
			}
			if(hrs & RTC_AM_MASK)
			{
				PORTB |= _BV(PM_LED_PORT);
				PORTB &= ~_BV(AM_LED_PORT);
			}
			else
			{
				PORTB |= _BV(AM_LED_PORT);
				PORTB &= ~_BV(PM_LED_PORT);
			}
		}
		else
		{
			//User can us PBs to set time
			switch(cur_set)
			{
				case Month:
					if(btn_cnt > 12) btn_cnt = 12;
					month = bin_to_bcd(btn_cnt) & (RTC_10MONTH_MASK | RTC_MONTH_MASK);
					break;
				case Day:
					if(btn_cnt > 31) btn_cnt = 31;
					date = bin_to_bcd(btn_cnt) & (RTC_10DATE_MASK | RTC_DATE_MASK);
					break;
				case Year:
					year = bin_to_bcd(btn_cnt) & (RTC_10YR_MASK | RTC_YR_MASK);
					break;
				case Hour:
					if(btn_cnt > 23) btn_cnt = 23;
					if(btn_cnt <= 12)
					{
						hrs = bin_to_bcd(btn_cnt) & (RTC_10HR_MASK | RTC_HR_MASK);
						hrs |= 0b01000000; //12/24 is high, set AM
					}
					else
					{
						hrs = bin_to_bcd(btn_cnt) & (RTC_10HR_MASK | RTC_HR_MASK);
						hrs |= 0b01100000; //12/24 is high, set PM
					}
					break;
				case Min:
					if(btn_cnt > 59) btn_cnt = 59;
					min = bin_to_bcd(btn_cnt) & (RTC_10MIN_MASK | RTC_MIN_MASK);
					break;
			}
			sec = 0;
			dow = 1;
			
			if(cur_set < Hour)
			{
				update_drv_date();
				PORTB |= _BV(AM_LED_PORT); //AM led indicates date is being set
				PORTB &= ~_BV(PM_LED_PORT);
			}
			else
			{
				update_drv_time();
				PORTB |= _BV(PM_LED_PORT); //PM led indicates time is being set
				PORTB &= ~_BV(AM_LED_PORT);
			}
		}

		write_spi(DRV_INTENSITY,intens);
	}
}

//Write to 7221 7-seg driver
void write_spi(unsigned char high_byte, unsigned char low_byte)
{
	PORTB &= ~_BV(PORTB2);
	SPDR = high_byte;
	while(!(SPSR & _BV(SPIF)));
	SPDR = low_byte;
	while(!(SPSR & _BV(SPIF)));
	PORTB |= _BV(PORTB2);
}

//Put time on 7-segs
void update_drv_time(void)
{
	write_spi(DRV_DIG_START + 0, (sec & RTC_SEC_MASK));
	write_spi(DRV_DIG_START + 1, (sec & RTC_10SEC_MASK) >> 4);
	
	//Blink minute during Set
	if(cur_mode == Set && cur_set == Min)
	{
		write_spi(DRV_DIG_START + 2, ((min | blink) & RTC_MIN_MASK));
		write_spi(DRV_DIG_START + 3, ((min | blink) & RTC_10MIN_MASK) >> 4);
	}
	else
	{
		write_spi(DRV_DIG_START + 2, (min & RTC_MIN_MASK));
		write_spi(DRV_DIG_START + 3, (min & RTC_10MIN_MASK) >> 4);	
	}

	//Blink hour during Set
	if(cur_mode == Set && cur_set == Hour)
	{
		write_spi(DRV_DIG_START + 4, ((hrs | blink) & RTC_HR_MASK));
		write_spi(DRV_DIG_START + 5, ((hrs | blink) & RTC_10HR_MASK) >> 4);
	}
	else
	{
		write_spi(DRV_DIG_START + 4, (hrs & RTC_HR_MASK));
		write_spi(DRV_DIG_START + 5, (hrs & RTC_10HR_MASK) >> 4);	
	}
}

//Put date on 7-segs
void update_drv_date(void)
{
	//Blink year during Set
	if(cur_mode == Set && cur_set == Year)
	{
		write_spi(DRV_DIG_START + 0, ((year | blink) & RTC_YR_MASK));
		write_spi(DRV_DIG_START + 1, ((year | blink) & RTC_10YR_MASK) >> 4);
	}
	else
	{
		write_spi(DRV_DIG_START + 0, (year & RTC_YR_MASK));
		write_spi(DRV_DIG_START + 1, (year & RTC_10YR_MASK) >> 4);	
	}
	
	//Blink day
	if(cur_mode == Set && cur_set == Day)
	{
		write_spi(DRV_DIG_START + 2, ((date | blink) & RTC_DATE_MASK));
		write_spi(DRV_DIG_START + 3, ((date | blink) & RTC_10DATE_MASK) >> 4);
	}
	else
	{
		write_spi(DRV_DIG_START + 2, (date & RTC_DATE_MASK));
		write_spi(DRV_DIG_START + 3, (date & RTC_10DATE_MASK) >> 4);
	}

	//Blink month
	if(cur_mode == Set && cur_set = Month)
	{
		write_spi(DRV_DIG_START + 4, ((month | blink) & RTC_MONTH_MASK));
		write_spi(DRV_DIG_START + 5, ((month | blink) & RTC_10MONTH_MASK) >> 4);
	}
	else
	{
		write_spi(DRV_DIG_START + 4, (month & RTC_MONTH_MASK));
		write_spi(DRV_DIG_START + 5, (month & RTC_10MONTH_MASK) >> 4);
	}
}

//Initialize RTC: no square wave out, ena oscillator
void config_rtc(void)
{
	i2c_start_wait(RTC_ADDR + I2C_WRITE);
	i2c_write(0x07); //set ctrl reg
	i2c_write(0x10); //turn off sq wave
	i2c_stop();
	
	i2c_start_wait(RTC_ADDR + I2C_WRITE);
	i2c_write(0x00); //set sec reg
	i2c_write(sec & 0x7F); //turn on osc
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

//RTC requires bcd encoding
char bin_to_bcd(char in)
{
	char tens = 0;
	while(in >= 10)
	{
		tens++;
		in -=10;
	}
	
	return (tens << 4) | in;
}
