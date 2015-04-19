//*****************************************************************************
// Title		: Pulse to tone (DTMF) converter
// Author		: Boris Cherkasskiy
// Created		: 2011-10-24
//
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//
// DTMF generator logic is loosely based on the AVR314 app note from Atmel
//
//*****************************************************************************


#ifndef MAIN_H
#define MAIN_H


#ifndef TIMER_INTERRUPT_HANDLER
#define TIMER_INTERRUPT_HANDLER		SIGNAL
#endif

#ifndef PIN_INTERRUPT_HANDLER
#define PIN_INTERRUPT_HANDLER		SIGNAL
#endif


// Define constants
#define TIMER_CLK_STOP			0x00	///< Timer Stopped
#define TIMER_CLK_DIV1			0x01	///< Timer clocked at F_CPU
#define TIMER_CLK_DIV8			0x02	///< Timer clocked at F_CPU/8
#define TIMER_CLK_DIV64			0x03	///< Timer clocked at F_CPU/64
#define TIMER_CLK_DIV256		0x04	///< Timer clocked at F_CPU/256
#define TIMER_CLK_DIV1024		0x05	///< Timer clocked at F_CPU/1024

#define TIMER_PRESCALE_MASK0		0x07	///< Timer Prescaler Bit-Mask
#define TIMER_PRESCALE_MASK1		0x0F	///< Timer Prescaler Bit-Mask

#define  N_samples  128              // Number of samples in lookup table

#define DIGIT_BEEP	-10
#define DIGIT_BEEP_LOW	-13
#define DIGIT_TUNE_ASC	-11
#define DIGIT_TUNE_DESC	-12
#define DIGIT_OFF	-1
#define DIGIT_STAR	10
#define DIGIT_POUND	11

#define DTMF_DURATION_MS 100

// PWM frequency = 4Mhz/256 = 15625Hz; overflow cycles per MS = 15
#define T0_OVERFLOW_PER_MS 15
#define SF_DELAY_MS 2000
#define SPEED_DIAL_SIZE 30

// PB0 (OC0A) as PWM output
#define PIN_PWM_OUT PB0

#define PIN_DIAL PB2
#define PIN_PULSE PB1


// Helper functions
#ifndef BV
	#define BV(bit)			(1<<(bit))
#endif
#ifndef cbi
	#define cbi(reg,bit)	reg &= ~(BV(bit))
#endif
#ifndef sbi
	#define sbi(reg,bit)	reg |= (BV(bit))
#endif


//************************** SIN TABLE *************************************
// Samples table : one period sampled on 128 samples and
// quantized on 7 bit
//**************************************************************************
const unsigned char auc_SinParam [N_samples] = {
	64, 67,  70, 73,
	76, 79,  82, 85,
	88, 91,  94, 96,
	99, 102, 104,106,
	109,111, 113,115,
	117,118, 120,121,
	123,124, 125,126,
	126,127, 127,127,
	127,127, 127,127,
	126,126, 125,124,
	123,121, 120,118,
	117,115, 113,111,
	109,106, 104,102,
	99, 96,  94, 91,
	88, 85,  82, 79,
	76, 73,  70, 67,
	64, 60,  57, 54,
	51, 48,  45, 42,
	39, 36,  33, 31,
	28, 25,  23, 21,
	18, 16,  14, 12,
	10, 9,   7,  6,
	4,  3,   2,  1,
	1,  0,   0,  0,
	0,  0,   0,  0,
	1,  1,   2,  3,
	4,  6,   7,  9,
	10, 12, 14,  16,
	18, 21, 23,  25,
	28, 31, 33,  36,
	39, 42, 45,  48,
	51, 54, 57,  60
};


//***************************  x_SW  ***************************************
// Fck=Xtal/prescaler
// Table of x_SW (excess 8): x_SW = ROUND(8 * N_samples * f * 510 / Fck)
//**************************************************************************

//high frequency
//1209hz  ---> x_SW = 79
//1336hz  ---> x_SW = 87
//1477hz  ---> x_SW = 96
//1633hz  ---> x_SW = 107
//
//low frequency
//697hz  ---> x_SW = 46
//770hz  ---> x_SW = 50
//852hz  ---> x_SW = 56
//941hz  ---> x_SW = 61
//
// 		|  1209 | 1336 | 1477 | 1633
//  697 |   1	|  2   |   3  |   A
//  770 |   4	|  5   |   6  |   B
//  852 |   7	|  8   |   9  |   C
//  941 |	*	|  0   |   #  |   D


const unsigned char auc_frequency [12][2] = {
	{87, 61},	// 0
	{79, 46},	// 1
	{87, 46},	// 2
	{96, 46},	// 3
	{79, 50},	// 4
	{87, 50},	// 5
	{96, 50},	// 6
	{79, 56},	// 7
	{87, 56},	// 8
	{96, 56},	// 9
	{79, 61},	// *
	{96, 61},	// #
};


// Function prototypes
int main(void);

void ProcessDialedDigit (void);

void init (void);

void GenerateDigit(signed char scDigit, unsigned int uiDuarationMS);

void EnablePWM (void);
void SleepMS (unsigned int uiMsec);

void Dial_SpeedDialNumber(unsigned char ucSpeedDialIndex);
void WriteCurrentSpeedDial(unsigned char ucSpeedDialIndex);

#endif


