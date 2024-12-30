/*
 * tag_firmware.c
 *
 * Created: 06/12/2024 15:24:56
 * Author : felix
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>

// Internal 8MHz osc without CKDIV8
#define F_CPU		8000000UL

// Outputs
#define DISP_F		0
#define DISP_E		1
#define DISP_L		2
#define DISP_I		3
#define DISP_X		4
#define LED_BACK	6

// Inputs
#define PHOTODIODE	PA7

uint8_t pwmval_F=0;
uint8_t pwmval_E=0;
uint8_t pwmval_L=0;
uint8_t pwmval_I=0;
uint8_t pwmval_X=0;
uint8_t pwmval_BACK=0;

uint8_t pwmval_F_corr=0;
uint8_t pwmval_E_corr=0;
uint8_t pwmval_L_corr=0;
uint8_t pwmval_I_corr=0;
uint8_t pwmval_X_corr=0;
uint8_t pwmval_BACK_corr=0;

float light_prescaler = 0.2;
uint8_t displaybit = 0b00000000;							// Bits corresponding to PORTA bits
volatile unsigned long tick_animation = 0;					// long is 32 bits (4.29 billion), long long is 64 bits
volatile uint8_t tick_pwm = 0;
unsigned long animation_step = 0;
unsigned long old_step = 0;


void ADC_init(void)
{
	uint16_t result;
	
	// Default VCC as reference
	//ADMUX |= (1 << REFS1) | (1 << REFS0);					// 1.1V internal reference
	
	ADCSRA = (1 << ADPS2) | (1 << ADPS1);					// 64 ADC clock prescaler
	ADCSRA |= (1 << ADEN);									// ADC enable
	
	//Dummy-Readout
	ADCSRA |= (1 << ADSC);									// ADC single conversion
	while (ADCSRA & (1 << ADSC)) {}							// Wait until conversion is finished
	
	result = ADCW;											// Read register of result and do nothing with it
}

uint16_t ADC_Read(uint8_t channel)
{
	ADMUX = (ADMUX & ~(0x1F)) | (channel & 0x1F);			// Select ADC channel without interfering with other bits in register
	ADCSRA |= (1 << ADSC);									// ADC single conversion
	while (ADCSRA & (1 << ADSC)) {}							// Wait until conversion is finished
	
	return ADCW;											// Return value
}


void animation_fillup(void)
{
	// Scale the "animation_step" from tick_animation
	// e.g. one step every 200 interrupts
	animation_step = tick_animation / 200;		// divide by 200 equals 4ms per step
	
	if (animation_step != old_step)
	{
		old_step = animation_step;

		// Each phase is 256 steps: 0..255, 256..511, etc.
		if (animation_step < 256) {
			// Phase 0
			pwmval_F   = (uint8_t)animation_step;
			pwmval_E   = 0;
			pwmval_L   = 0;
			pwmval_I   = 0;
			pwmval_X   = 0;
		}
		else if (animation_step < 512) {
			// Phase 1
			uint16_t phase = animation_step - 256;
			pwmval_F = 255;
			pwmval_E = phase;
			pwmval_L = 0;
			pwmval_I = 0;
			pwmval_X = 0;
		}
		else if (animation_step < 768) {
			// Phase 2
			uint16_t phase = animation_step - 512;
			pwmval_F = 255;
			pwmval_E = 255;
			pwmval_L = phase;
			pwmval_I = 0;
			pwmval_X = 0;
		}
		else if (animation_step < 1024) {
			// Phase 3
			uint16_t phase = animation_step - 768;
			pwmval_F = 255;
			pwmval_E = 255;
			pwmval_L = 255;
			pwmval_I = phase;
			pwmval_X = 0;
		}
		else if (animation_step < 1280) {
			// Phase 4
			uint16_t phase = animation_step - 1024;
			pwmval_F = 255;
			pwmval_E = 255;
			pwmval_L = 255;
			pwmval_I = 255;
			pwmval_X = phase;
		}
		else if (animation_step < 1536) {
			// Phase 5
			uint16_t phase = animation_step - 1280;
			pwmval_F = 255 - phase;
			pwmval_E = 255;
			pwmval_L = 255;
			pwmval_I = 255;
			pwmval_X = 255;
		}
		else if (animation_step < 1792) {
			// Phase 6
			uint16_t phase = animation_step - 1536;
			pwmval_F = 0;
			pwmval_E = 255 - phase;
			pwmval_L = 255;
			pwmval_I = 255;
			pwmval_X = 255;
		}
		else if (animation_step < 2048) {
			// Phase 7
			uint16_t phase = animation_step - 1792;
			pwmval_F = 0;
			pwmval_E = 0;
			pwmval_L = 255 - phase;
			pwmval_I = 255;
			pwmval_X = 255;
		}
		else if (animation_step < 2304) {
			// Phase 8
			uint16_t phase = animation_step - 2048;
			pwmval_F = 0;
			pwmval_E = 0;
			pwmval_L = 0;
			pwmval_I = 255 - phase;
			pwmval_X = 255;
		}
		else if (animation_step < 2560) {
			// Phase 9
			uint16_t phase = animation_step - 2304;
			pwmval_F = 0;
			pwmval_E = 0;
			pwmval_L = 0;
			pwmval_I = 0;
			pwmval_X = 255 - phase;
		}

		// Reset counter at end of animation
		if (animation_step >= 2560) {
			tick_animation = 0;
		}
	}
}

int main(void)
{
    // Outputs
	DDRA |= (1 << DISP_F) | (1 << DISP_E) | (1 << DISP_L) | (1 << DISP_I) | (1 << DISP_X) | (1 << LED_BACK);
	
	// Timer1 setup
	TCCR1B |= (1 << WGM12);						// CTC mode
	TCCR1B |= (1 << CS10);						// Prescaler 1
	OCR1A = 100;								// Top value
	TIMSK1 |= (1 << OCIE1A);					// Timer1 A interrupt enable
	
	sei();										// Enable interrupts globally
	
    while (1) 
    {
		animation_fillup();
		pwmval_BACK = 255;
		
		// Apply correction factor for maximum brightnes
		pwmval_F_corr = (int) (pwmval_F * light_prescaler);
		pwmval_E_corr = (int) (pwmval_E * light_prescaler);
		pwmval_L_corr = (int) (pwmval_L * light_prescaler);
		pwmval_I_corr = (int) (pwmval_I * light_prescaler);
		pwmval_X_corr = (int) (pwmval_X * light_prescaler);
		pwmval_BACK_corr = (int) (pwmval_BACK * light_prescaler);
	}
}


ISR (TIM1_COMPA_vect)
{		
	displaybit = 0x00;

	// Step 2: For each output, compare tick_pwm with its pwmval:
	if (tick_pwm < pwmval_F_corr)    displaybit |= (1 << DISP_F);
	if (tick_pwm < pwmval_E_corr)    displaybit |= (1 << DISP_E);
	if (tick_pwm < pwmval_L_corr)    displaybit |= (1 << DISP_L);
	if (tick_pwm < pwmval_I_corr)    displaybit |= (1 << DISP_I);
	if (tick_pwm < pwmval_X_corr)    displaybit |= (1 << DISP_X);
	if (tick_pwm < pwmval_BACK_corr) displaybit |= (1 << LED_BACK);

	// Set ports to high
	PORTA = displaybit;

	// Increment counters and reset if necessary
	tick_animation++;
	tick_pwm++;
	if (tick_pwm >= 255) tick_pwm = 0;
}
