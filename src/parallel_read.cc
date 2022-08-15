#include <Arduino.h>
#include <DMAChannel.h>

DMAChannel dmachannel;

#define DMABUFFER_SIZE	4096
uint32_t dmaBuffer[DMABUFFER_SIZE];

int counter = 0;
unsigned long prevTime;
unsigned long currTime;
bool error = false;
bool dmaDone = false;
uint32_t errA, errB, errorIndex;

// copied from pwm.c
void xbar_connect(unsigned int input, unsigned int output)
{
	if (input >= 88) return;
	if (output >= 132) return;

	volatile uint16_t *xbar = &XBARA1_SEL0 + (output / 2);
	uint16_t val = *xbar;
	if (!(output & 1)) {
		val = (val & 0xFF00) | input;
	} else {
		val = (val & 0x00FF) | (input << 8);
	}
	*xbar = val;
}


void dmaInterrupt()
{
	dmachannel.clearInterrupt();	// tell system we processed it.
	asm("DSB");						// this is a memory barrier

	prevTime = currTime;
	currTime = micros();  

	error = false;

	uint32_t prev = ( dmaBuffer[0] >> 18 ) & 0xFF;
	for( int i=1; i<4096; ++i )
	{
		uint32_t curr = ( dmaBuffer[i] >> 18 ) & 0xFF;

		if ( ( curr != prev + 1 ) && ( curr != 0 ) )
		{
			error = true;
			errorIndex = i;
			errA = prev;
			errB = curr;
			break;
		}

		prev = curr;
	}

	dmaDone = true;
}


void kickOffDMA()
{
	prevTime = micros();
	currTime = prevTime;

	dmachannel.enable();	
}


void setup()
{
	Serial.begin(115200);	

	// set the GPIO1 pins to input
	GPIO1_GDIR &= ~(0x03FC0000u);

	// Need to switch the IO pins back to GPI1 from GPIO6
	IOMUXC_GPR_GPR26 &= ~(0x03FC0000u);

	// configure DMA channels
	dmachannel.begin();
	dmachannel.source( GPIO1_DR ); 
	dmachannel.destinationBuffer( dmaBuffer, DMABUFFER_SIZE * 4 );  

	dmachannel.interruptAtCompletion();  
	dmachannel.attachInterrupt( dmaInterrupt );

	// clock XBAR - apparently not on by default!
	CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON);

	// set the IOMUX mode to 3, to route it to XBAR
	IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_06 = 3;

	// set XBAR1_IO008 to INPUT
	IOMUXC_GPR_GPR6 &= ~(IOMUXC_GPR_GPR6_IOMUXC_XBAR_DIR_SEL_8);  // Make sure it is input mode

	// daisy chaining - select between EMC06 and SD_B0_04
	IOMUXC_XBAR1_IN08_SELECT_INPUT = 0;
	
	// Tell XBAR to dDMA on Rising
	XBARA1_CTRL0 = XBARA_CTRL_STS0 | XBARA_CTRL_EDGE0(1) | XBARA_CTRL_DEN0;

	// connect the IOMUX_XBAR_INOUT08 to DMA_CH_MUX_REQ30
	xbar_connect(XBARA1_IN_IOMUX_XBAR_INOUT08, XBARA1_OUT_DMA_CH_MUX_REQ30);

	// trigger our DMA channel at the request from XBAR
	dmachannel.triggerAtHardwareEvent( DMAMUX_SOURCE_XBAR1_0 );

	kickOffDMA();
}


void loop()
{
	delay( 100 );

	if ( dmaDone )
	{
		Serial.printf( "Counter %8d Buffer 0x%08X time %8u  %s", counter, dmaBuffer[0], currTime - prevTime, error ?  "ERROR" : "no error"  );
	
		if ( error )
		{
			Serial.printf( " [%d] 0x%08X 0x%08X", errorIndex, errA, errB );
		}

		Serial.printf( "\n");

		dmaDone = false;
		delay( 1000 );

		Serial.printf( "Kicking off another \n" );

		kickOffDMA();
	}
	else
	{
		Serial.printf( "Waiting...\n" );
	}

	++counter;
}
