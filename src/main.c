#include "ch32fun.h"
#include <stdio.h>

#define SSD1306_128X64
#include "ssd1306_i2c.h"
#include "ssd1306.h"

//#define RTC_CLOCK_SOURCE_LSI
#define RTC_CLOCK_SOURCE_LSE
#define RTC_CLOCK_FREQ 32768 // in Hz

// -------------------------------------------------------------------------------------------------
// Variables

// RTC stuff
volatile static unsigned char month_days[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
static const unsigned char week_days[7] = { 4,5,6,0,1,2,3 };
//Thu=4, Fri=5, Sat=6, Sun=0, Mon=1, Tue=2, Wed=3

static const char *months[] = {
  NULL,
  "January",
  "February",
  "March",
  "April",
  "May",
  "June",
  "July",
  "August",
  "September",
  "October",
  "November",
  "December"
};

static const char *days[] = {
  "Sunday",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday"
};

volatile unsigned char
ntp_hour, ntp_minute, ntp_second, ntp_week_day, ntp_date, ntp_month, leap_days, leap_year_ind;

volatile unsigned short temp_days;

volatile unsigned int
ntp_year, days_since_epoch, day_of_year;

// Screen status
volatile static unsigned char oled_ready = 0;

// -------------------------------------------------------------------------------------------------
// ISRs

// -----------------------------------------------
// PA0 change interrupt service routine
void EXTI0_IRQHandler(void) __attribute__((interrupt));
void EXTI0_IRQHandler(void)
{
  // Clear flag (reference manual says write 1 to clear it)
  EXTI->INTFR |= EXTI_INTF_INTF0;

  funDigitalWrite(PB12, funDigitalRead(PB12) ? FUN_LOW : FUN_HIGH);
}

// Need to declare this as it is used in the IRQ handler
static inline void toDate(uint32_t);

// -----------------------------------------------
// RTC interrupt service routine
void RTC_IRQHandler(void) __attribute__((interrupt));
void RTC_IRQHandler(void)
{
  // Check which interrupt was
  if (RTC->CTLRL & RTC_CTLRL_SECF) // One-second interrupt
  {
    uint16_t high1, high2, low;
    uint32_t s;
    char str[32];

    // Clear flag
    RTC->CTLRL &=~ RTC_CTLRL_SECF;

    if (!oled_ready) return;

    high1 = RTC->CNTH;
    low = RTC->CNTL;
    high2 = RTC->CNTH;

    // handle rollover
    if (high1 != high2) s = (high2 << 16) | RTC->CNTL;
    else s = (high1 << 16) | low;

    toDate(s);

    // Clear screen
    ssd1306_setbuf(0);

    sprintf(str, "%s %02d %d", months[ntp_month], ntp_date, ntp_year);
    ssd1306_drawstr(0,0, str, 1);

    ssd1306_drawstr(0,16, (char*)days[ntp_week_day], 1);

    sprintf(str, "%02d:%02d:%02d", ntp_hour, ntp_minute, ntp_second);
    ssd1306_drawstr(32,48, str, 1);

    ssd1306_refresh();
  }
  else if (RTC->CTLRL & RTC_CTLRL_ALRF) // Alarm interrupt
  {
    // Clear flag
    RTC->CTLRL &=~ RTC_CTLRL_ALRF;

    funDigitalWrite(PB12, FUN_LOW);
    printf("Alarm!\n");
  }
}

// -------------------------------------------------------------------------------------------------
// RTC related inline functions

// -----------------------------------------------
// RTC initial setup
static inline void rtc_init()
{
  // Enable access to the RTC and backup registers
  RCC->APB1PCENR |= RCC_PWREN | RCC_BKPEN;
  PWR->CTLR |= PWR_CTLR_DBP;

  // this is needed to reset RTC on external manual reset (nRST pin) and first time flashing
  if ( (!(RCC->RSTSCKR & RCC_PORRSTF) && (BKP->DATAR1 != 0xDEAD)) || (RCC->RSTSCKR & RCC_PINRSTF) )
  {
    // Clear nRST pin reset flag
    RCC->RSTSCKR |= RCC_RMVF;

    // Reset backup domain. This operation must be performed
    // if you want to change RTC clock source
    RCC->BDCTLR |= RCC_BDRST;
    RCC->BDCTLR &=~RCC_BDRST;

    // store flag in backup domain register
    BKP->DATAR1 = 0xDEAD;

    #ifdef RTC_CLOCK_SOURCE_LSI
      // Enable LSI
      RCC->RSTSCKR |= RCC_LSION;
      while ( !(RCC->RSTSCKR & RCC_LSIRDY) );

      // Set clock source for RTC
      RCC->BDCTLR &=~ RCC_RTCSEL;
      RCC->BDCTLR |= RCC_RTCSEL_LSI;
    #endif

    #ifdef RTC_CLOCK_SOURCE_LSE
      // Enable LSE
      RCC->BDCTLR |= RCC_LSEON;
      uint16_t timeout = 0;
      while ( !(RCC->BDCTLR & RCC_LSERDY) )
      {
        Delay_Ms(1);
        timeout++;
        if (timeout > 1000)
        {
          printf("Could not start LSE.\n");
          break;
        }
      }
      // Set clock source for RTC
      RCC->BDCTLR &=~ RCC_RTCSEL;
      RCC->BDCTLR |= RCC_RTCSEL_LSE;
    #endif

    // Enable RTC
    RCC->BDCTLR |= RCC_RTCEN;

    // Enter configuration mode
    while ( !(RTC->CTLRL & RTC_FLAG_RTOFF) );
    RTC->CTLRL |= RTC_CTLRL_CNF;

    // Set prescaler so 1 tick = 1 s
    RTC->PSCRH = 0;
    RTC->PSCRL = RTC_CLOCK_FREQ - 1;

    // Set counting start value
    RTC->CNTH = 0x68A5;
    RTC->CNTL = 0x3A90;

    // Set alarm 15 seconds after start
    RTC->ALRMH = 0x68A5;
    RTC->ALRML = 0x3A9F;

    // Enable one-second and alarm interrupts
    while ( !(RTC->CTLRL & RTC_FLAG_RTOFF) );
    RTC->CTLRH |= (RTC_CTLRH_SECIE | RTC_CTLRH_ALRIE);
    NVIC_EnableIRQ(RTC_IRQn);

    // Exit configuration mode and actually start RTC
    while ( !(RTC->CTLRL & RTC_FLAG_RTOFF) );
    RTC->CTLRL &=~ RTC_CTLRL_CNF;

  }
  // IDK if it actually needed, but in RM:
  // "after the PB1 is reset or PB1 clock is stopped, the bit should be reset firstly"
  RTC->CTLRL &=~ RTC_CTLRL_RSF;

  // Wait for sync so when performing read we get valid counter value
  while ( !(RTC->CTLRL & RTC_CTLRL_RSF) );
}

// -----------------------------------------------
// Epoch seconds to readable date
// Taken from: https://github.com/sidsingh78/EPOCH-to-time-date-converter/blob/master/epoch_conv.c
static inline void toDate(uint32_t epoch)
{
  leap_days = 0;
  leap_year_ind = 0;

  // Add or substract time zone here. 
  epoch -= 18000; //GMT -5:00 = -18000 seconds 

  ntp_second = epoch % 60;
  epoch /= 60;
  ntp_minute = epoch % 60;
  epoch /= 60;
  ntp_hour = epoch % 24;
  epoch /= 24;

  days_since_epoch = epoch;      //number of days since epoch
  ntp_week_day = week_days[days_since_epoch % 7];  //Calculating WeekDay

  // calculate day of year - subtract days (and increment years) until days are less than the year's total number of days...
  day_of_year = days_since_epoch;
  ntp_year = 1970;
  leap_year_ind = 0;
  while (((day_of_year >= 365) && (leap_year_ind == 0)) || ((day_of_year >= 366) && (leap_year_ind == 1)))
  {
    if (leap_year_ind)
    {
      day_of_year -= 366;
      leap_days++;
    }
    else day_of_year -= 365;
    ntp_year++;
    leap_year_ind = (((ntp_year % 4 == 0) && (ntp_year % 100 != 0)) || (ntp_year % 400 == 0));
  }
  day_of_year = day_of_year + 1;

  if (leap_year_ind) // in a leap year?
  {
    month_days[1] = 29;     //February = 29 days for leap years

    // if on or beyond February 29, count this year's leap day
    if (day_of_year >= 60) leap_days++;
  }
  else month_days[1] = 28; //February = 28 days for non-leap years

  temp_days = 0;

  for (ntp_month = 0; ntp_month <= 11; ntp_month++) //calculating current Month
  {
    if (day_of_year <= temp_days) break;
    temp_days = temp_days + month_days[ntp_month];
  }

  temp_days = temp_days - month_days[ntp_month - 1]; //calculating current Date
  ntp_date = day_of_year - temp_days;
}

// -------------------------------------------------------------------------------------------------
// Program entry point

int main()
{
  SystemInit();

  funGpioInitAll(); // Enable GPIOs

  funPinMode( PA0, GPIO_CFGLR_IN_PUPD ); // Set PA0 to input
  funPinMode( PB2, GPIO_CFGLR_OUT_10Mhz_PP ); // Set PB2 to output
  funPinMode( PB12, GPIO_CFGLR_OUT_10Mhz_PP ); // Set PB12 to output

  // Relay is active low
  funDigitalWrite(PB12, FUN_HIGH);

  // Enable PA0 change interrupt on rising edge
  EXTI->INTENR |= EXTI_INTENR_MR0;
  EXTI->RTENR |= EXTI_RTENR_TR0;
  NVIC_EnableIRQ(EXTI0_IRQn);

  rtc_init();

  printf("Initializing I2C OLED... ");

  if(!ssd1306_i2c_init())
  {
    ssd1306_init();
    printf("OK\n");
    oled_ready = 1;

    for (;;)
    {
      funDigitalWrite(PB2, FUN_HIGH); // Turn on PB2
      Delay_Ms(100);

      funDigitalWrite(PB2, FUN_LOW);  // Turn off PB2
      Delay_Ms(900);
    }
  }
  else
  {
    printf("Failed\n");
    for(;;)
    {
      funDigitalWrite(PB2, funDigitalRead(PB2) ? FUN_LOW : FUN_HIGH);
      Delay_Ms(500);
    }
  }
}
