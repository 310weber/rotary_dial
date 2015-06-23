//*****************************************************************************
// Title		: Pulse to tone (DTMF) converter
// Author		: Boris Cherkasskiy
// Created		: 2011-10-24
// Modified		: Arnie Weber 2015-06-22
// 					https://bitbucket.org/310weber/rotary_dial/
//
//
// This code is distributed under the GNU Public License
// which can be found at http://www.gnu.org/licenses/gpl.txt
//
// DTMF generator logic is loosely based on the AVR314 app note from Atmel
//
//*****************************************************************************


//----- Include Files ---------------------------------------------------------
#include <avr/io.h>		// include I/O definitions (port names, pin names, etc)
#include <avr/interrupt.h>	// include interrupt support
#include <avr/sleep.h>
//#include <avr/wdt.h> 
#include <util/delay.h>
#include <stdbool.h>
#include <avr/eeprom.h>
#include "main.h" 


// EEPROM variables
// 7 speed dial number (dialed special function 3-9)
signed char EEMEM EEPROM_SpeedDial[7][SPEED_DIAL_SIZE] = {[0 ... 6][0 ... SPEED_DIAL_SIZE-1] = DIGIT_OFF};


// Global Variables 
volatile unsigned char cSWa = 0x00;               // step width of high frequency
volatile unsigned char cSWb = 0x00;               // step width of low frequency

volatile unsigned int iCurSinValA = 0;           // position freq. A in LUT (extended format)
volatile unsigned int iCurSinValB = 0;           // position freq. B in LUT (extended format)

volatile unsigned long ulDelayCounter = 0;		// Delay counter for sleep function

volatile bool bSF_DetectionActive = false;		// SF detection active [AW] Moved from local main() variables
volatile bool bCurDialState = true;		     	// Rotor status [AW] Moved from local main() variables

// Dial status structure
typedef struct struct_DialStatus
{	
	signed char iDialedDigit;					// Dialed/detected digit

	// SF dialed by holding rotor for few seconds (beep to indicate that SF is activated) before releasing it
	// SF defined as: 1:*; 2:#; 3-9: speed dial; 0: program speed dial number	
	bool bSF_Selected;							// Special Function selected
	
	signed char iSpeedDialDigitIndex;			// Speed dial digit index
	signed char iSpeedDialIndex; 				// Speed dial digi index (in the SD array)
	signed char arSpeedDial[SPEED_DIAL_SIZE];	// Selected speed dial arrays		
} type_DialStatus;

volatile type_DialStatus sDS;	// Global dial status structure


//----- BEGIN MAIN ------------------------------------------------------------
int main(void)
{
	// Program clock prescaller to divide +frequency by 1
	// Write CLKPCE 1 and other bits 0	
	CLKPR = (1<<CLKPCE);	
	// Write prescaler value with CLKPCE = 0
	CLKPR = 0x00;

	// Initialize I/O and global variables
	init();

	// Turn PWM OFF
	GenerateDigit(DIGIT_OFF, 0); 

	// Local dial status variables 
	volatile bool bPrevDialState = true;		// Rotor status
	volatile bool bPrevPulseState = false;	// Rotor pulse status
	volatile bool bCurPulseState = false;	// Rotor pulse status


	// Main loop
  	while (1)
	{ 
		bCurDialState = bit_is_set (PINB, PIN_DIAL);
		bCurPulseState = bit_is_set (PINB, PIN_PULSE);


		if (bPrevDialState != bCurDialState) 
		{
			if (!bCurDialState) 
			{
				// Dial just started
				// Enabling special function detection
				bSF_DetectionActive = true;
				sDS.bSF_Selected = false;

				sDS.iDialedDigit = 0;
				SleepMS (50);	// Delay 50ms
			}
			else 
			{
				// Disabling SF detection (should be already disabled)
				bSF_DetectionActive = false;

				// Check that we detect a valid digit
				if ((sDS.iDialedDigit <= 0) || (sDS.iDialedDigit > 10))
				{
					// Should never happen - no pulses detected OR count more than 10 pulses
					sDS.iDialedDigit = DIGIT_OFF;					

					// Do nothing
					SleepMS (50);	// Delay 50ms
				}
				else 
				{
					// Got a valid digit - process it			
					if (sDS.iDialedDigit == 10)
					{
						// 10 pulses => 0
						sDS.iDialedDigit = 0;
					}

					ProcessDialedDigit();
				}
					
				sDS.bSF_Selected = false;	// Reset SF flag
			}	
		} 
		else 
		{
			if (!bCurDialState) 
			{
				// Dial is running				
				// [AW] functions moved to INT0 routine
			}
			else
			{
				// Rotary dial at the rest position
				// Reset all variables
				bSF_DetectionActive = false;
				sDS.bSF_Selected = false;
				sDS.iDialedDigit = DIGIT_OFF;
			}
		}

		bPrevDialState = bCurDialState;
		bPrevPulseState = bCurPulseState;

		// Don't power down if special function detection is active		
		if (bSF_DetectionActive)
		{
			// SF detection in progress - we need timer to run (IDLE mode)
			set_sleep_mode(SLEEP_MODE_IDLE);		
			sleep_mode();

			// Special function mode detected?
			if (ulDelayCounter >= SF_DELAY_MS * T0_OVERFLOW_PER_MS)
			{
				// SF mode detected
				sDS.bSF_Selected = true;
				bSF_DetectionActive = false;

				// Indicate that we entered SF mode wit short beep
				GenerateDigit (DIGIT_BEEP, 200);
			}
		}
		else
		{
			// Don't need timer - sleep to power down mode
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);
			sleep_mode();
		}

	}

	return 0;
}
//----- END MAIN ------------------------------------------------------------



// Processing dialed digit
void ProcessDialedDigit (void)
{
	// Special functions 1 and 2 (* and #)
	if (sDS.bSF_Selected && (sDS.iDialedDigit == 1))				
	{
		// SF 1-*
		sDS.iDialedDigit = DIGIT_STAR;
	}
	else if (sDS.bSF_Selected && (sDS.iDialedDigit == 2))
	{
		// SF 2-#
		sDS.iDialedDigit = DIGIT_POUND;
	}

	// Speed dial functionality - entering and leaving SD mode
	if (sDS.bSF_Selected && (sDS.iDialedDigit == 0))
	{
		// SF 0 - write speed dial

		// SP programming already in progress?
		if (sDS.iSpeedDialDigitIndex < 0)
		{
			// Just entered SD mode						
			sDS.iSpeedDialDigitIndex = 0;

			// At this point we don't know SD index yet
			sDS.iSpeedDialIndex = -1;

			// Clear selected SD array
			for (unsigned char i=0; i<SPEED_DIAL_SIZE; i++)
			{
				sDS.arSpeedDial[i] = DIGIT_OFF;
			}

			// Beep upon entering SD mode, user has to enter SD index
			GenerateDigit (DIGIT_TUNE_ASC, 700);
			GenerateDigit (DIGIT_TUNE_DESC, 700);
		}
		else
		{
			// SD in progress and user entered SF 0 - save SD and exit SD mode
		
			// Save speed dial number
			WriteCurrentSpeedDial(sDS.iSpeedDialIndex);

			// Leave SD mode
			sDS.iSpeedDialIndex = -1;
			sDS.iSpeedDialDigitIndex = -1;

			// Beep to indicate that we done
			GenerateDigit (DIGIT_TUNE_DESC, 800);
		}
	}
	// Programming SD number
	else if (sDS.iSpeedDialDigitIndex >= 0)
	{
		// First digit dialed after selecting SD mode. SD index not set yet
		if (sDS.iSpeedDialIndex < 0)
		{
			// SD index supposed to be between 3 and 9
			if ((sDS.iDialedDigit >= 3) && (sDS.iDialedDigit <= 9))
			{
				sDS.iSpeedDialIndex = sDS.iDialedDigit;
			
				// Beep to indicate that we are in the SD mode
				GenerateDigit (DIGIT_TUNE_ASC, 800);
			}
			else
			{
				// Wrong SD index - beep and exit SD mode
			
				// Leave SD mode
				sDS.iSpeedDialIndex = -1;
				sDS.iSpeedDialDigitIndex = -1;
			
				// Long Beep to indicate error
				GenerateDigit (DIGIT_BEEP, 1000);
			}
		}
		else
		{
			// Programming SD already in progress

			// Do we have too many digits entered?
			if (sDS.iSpeedDialDigitIndex >= SPEED_DIAL_SIZE)
			{
				// YES - finish and save speed dial number

				// Save speed dial number
				WriteCurrentSpeedDial(sDS.iSpeedDialIndex);

				// Leave SD mode
				sDS.iSpeedDialIndex = -1;
				sDS.iSpeedDialDigitIndex = -1;

				// Beep to indicate that we done
				GenerateDigit (DIGIT_TUNE_DESC, 800);
			} 
			else
			{
				// All good - set new digit to the array
				sDS.arSpeedDial[sDS.iSpeedDialDigitIndex] = sDS.iDialedDigit;

				// Generic beep - do not gererate DTMF code
				GenerateDigit(DIGIT_BEEP_LOW, DTMF_DURATION_MS);

				// Next digit
				sDS.iSpeedDialDigitIndex++;
			}
		}
	}
	// Call SD stored number
	else if (sDS.bSF_Selected && (sDS.iDialedDigit >= 3) && (sDS.iDialedDigit <= 9))
	{
		// SF 3-9 -> Call speed dial number
		Dial_SpeedDialNumber(sDS.iDialedDigit);
	} 
	// Standard (non speed dial functionality)
	else
	{
		// Standard (no speed dial, no special function) mode
		// Generate DTMF code
		GenerateDigit(sDS.iDialedDigit, DTMF_DURATION_MS);  
	}
}


// Initialization
void init (void)
{
	TIMSK  = (1<<TOIE0);                // Int T0 Overflow enabled

	TCCR0A = (1<<WGM00) | (1<<WGM01);   // 8Bit PWM; Compare/match output mode configured later
	TCCR0B = TIMER_PRESCALE_MASK0 & TIMER_CLK_DIV1;
	TCNT0 = 0;
	OCR0A = 0;
	
	// Configure I/O pins
	PORTB = 0;	// Reset all outputs. Force PWM output (PB0) to 0
	DDRB   = (1 << PIN_PWM_OUT);	// PWM output (OC0A pin)
	PORTB  = 0;  // [AW] Disable Pull-ups - external HW debounce

	// Disable unused modules to save power
	PRR = (1<<PRTIM1) | (1<<PRUSI) | (1<<PRADC);
	ACSR = (1<<ACD);

	// Configure pin change interrupt
	MCUCR = (1 << ISC01) | (0 << ISC00);         // [AW] Set INT0 for falling edge detection
	GIMSK = (1 << INT0) | (1 << PCIE);           // [AW] Added INT0
	PCMSK = (1 << PIN_DIAL) | (1 << PIN_PULSE);

	// Initialize (global) dial status structure (sDS)
	sDS.iDialedDigit = DIGIT_OFF;

	// Variables to detect special functions (SF)
	// SF dialed by holding rotor for few seconds (beep to indicate that SF activated) before releasing it
	// SF defined as: 1:*; 2:#; 3-9: speed dial; 0: program speed dial number
	sDS.bSF_Selected = false;	// Special Function selected

	// Speed dial stuff
	sDS.iSpeedDialDigitIndex = -1;	// Speed dial digit index
	sDS.iSpeedDialIndex = -1; // Speed dial digi index (in the SD array)	
	for (unsigned char i=0; i<SPEED_DIAL_SIZE; i++)	// Clear selected SD array
	{
		sDS.arSpeedDial[i] = DIGIT_OFF;
	}

	// Interrupts enabled
	sei();                     	     
}


// Generate DTMF tone, duration x ms
void GenerateDigit (signed char scDigit, unsigned int uiDuarationMS)
{
	if (scDigit >= 0 && scDigit <= DIGIT_POUND)
	{
		// Standard digits 0-9, *, #
		cSWa = auc_frequency[scDigit][0];  
		cSWb = auc_frequency[scDigit][1]; 
		EnablePWM();

		// Wait x ms
		SleepMS(uiDuarationMS);
	} 
	else if (scDigit==DIGIT_BEEP)
	{
		// Beep ~1000Hz (66)
		cSWa = 66;  
		cSWb = 0;
		EnablePWM();

		// Wait x ms
		SleepMS(uiDuarationMS);
	}
	else if (scDigit==DIGIT_BEEP_LOW)
	{
		// Beep ~500Hz (33)
		cSWa = 33;  
		cSWb = 0;
		EnablePWM();

		// Wait x ms
		SleepMS(uiDuarationMS);
	}
	else if (scDigit==DIGIT_TUNE_ASC)
	{
		cSWa = 34;	// C=523.25Hz  
		cSWb = 0;
		EnablePWM();
		
		SleepMS(uiDuarationMS/3);
		cSWa = 43;	// E=659.26Hz
		SleepMS(uiDuarationMS/3);
		cSWa = 51;	// G=784Hz
		SleepMS(uiDuarationMS/3);
	}
	else if (scDigit==DIGIT_TUNE_DESC)
	{
		cSWa = 51;	// G=784Hz
		cSWb = 0;
		EnablePWM();

		SleepMS(uiDuarationMS/3);
		cSWa = 43;	// E=659.26Hz
		SleepMS(uiDuarationMS/3);
		cSWa = 34;	// C=523.25Hz  
		SleepMS(uiDuarationMS/3);
	}


	// Stop DTMF transmitting
	// Disable PWM output (compare match mode 0) and force it to 0
	cbi(TCCR0A, COM0A1);
	cbi(TCCR0A, COM0A0);
	cbi(PORTB, PIN_PWM_OUT);
	cSWa = 0;
	cSWb = 0;
}


// Enable PWM output by configuring compare match mode - non inverting PWM
void EnablePWM (void)
{
	sbi(TCCR0A, COM0A1);
	cbi(TCCR0A, COM0A0);
}


// Wait x ms
void SleepMS(unsigned int uiMsec)
{	
	ulDelayCounter = 0;
	
	set_sleep_mode(SLEEP_MODE_IDLE);		
	while(ulDelayCounter <= uiMsec * T0_OVERFLOW_PER_MS)
	{
		sleep_mode();
	}
}


// Dial speed dial number (it erases current SD number in the global structure)
void Dial_SpeedDialNumber (unsigned char iSpeedDialIndex)
{
	if ((iSpeedDialIndex >= 3) && (iSpeedDialIndex <= 9))
	{
		// If dialed index 3 => using array index 0
		eeprom_read_block (&sDS.arSpeedDial, &EEPROM_SpeedDial[iSpeedDialIndex-3][0], SPEED_DIAL_SIZE);

		for (unsigned char i=0; i<SPEED_DIAL_SIZE; i++)
		{
			// Dial the number
			// Skip dialing invalid digits
			if ( (sDS.arSpeedDial[i] >= 0) && (sDS.arSpeedDial[i] <= DIGIT_POUND) )
			{
				GenerateDigit(sDS.arSpeedDial[i], DTMF_DURATION_MS);  

				// Pause between DTMF tones
				SleepMS (DTMF_DURATION_MS);    
			}
		}
	}
}


// Write current speed dial array (from the global structure) to the EEPROM
void WriteCurrentSpeedDial(unsigned char iSpeedDialIndex)
{
	if ((iSpeedDialIndex >= 3) && (iSpeedDialIndex <= 9))
	{
		// If dialed index 3 => using array index 0
		eeprom_update_block (&sDS.arSpeedDial, &EEPROM_SpeedDial[iSpeedDialIndex-3][0], SPEED_DIAL_SIZE);
	}
}


// Timer overflow interrupt service routine
TIMER_INTERRUPT_HANDLER(SIG_OVERFLOW0)
{ 
	unsigned char ucSinA;
	unsigned char ucSinB;

	// A component (high frequency) is always used
	// move Pointer about step width ahead
	iCurSinValA += cSWa;      
	// normalize Temp-Pointer 
	unsigned int i_TmpSinValA = (char)(((iCurSinValA + 4) >> 3) & (0x007F)); 
	ucSinA = auc_SinParam[i_TmpSinValA];


	//	B component (low frequency) is optional
	if (cSWb > 0)
	{
		// move Pointer about step width ahead
		iCurSinValB += cSWb;	
		// normalize Temp-Pointer	
		unsigned int i_TmpSinValB = (char)(((iCurSinValB + 4) >> 3) & (0x007F));		
		ucSinB = auc_SinParam[i_TmpSinValB];
	}
	else
	{
		ucSinB = 0;
	}

	// calculate PWM value: high frequency value + 3/4 low frequency value
	OCR0A = (ucSinA + (ucSinB - (ucSinB >> 2)));

	ulDelayCounter++;
}


// [AW] Handler for external interrupt on INT0 (PB2, pin 7)
ISR(INT0_vect)
{
	if (!bCurDialState)
	{
	    // Disabling SF detection
		bSF_DetectionActive = false;

		// A pulse just started
		sDS.iDialedDigit++;
	}
}

// [AW] Interrupt handlers updated to new code convention
// Interrupt initiated by pin change on any enabled pin
ISR(PCINT0_vect)
{
	// Do nothing, just wake up MCU
	_delay_us(100);
}

// [AW] Handler for any unspecified 'bad' interrupts
ISR(BADISR_vect)
{
	// Do nothing, just wake up MCU
	_delay_us(100);
}