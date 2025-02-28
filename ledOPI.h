#include <wiringPi.h>


#define LATCH_PIN 7
#define DATA_PIN 8
#define CLOCK_PIN 5



void pinSetup(void)
{

    wiringPiSetup();

    pinMode(DATA_PIN, OUTPUT);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
}


void clearShiftReg(void)
{
    digitalWrite(DATA_PIN, 0);
    digitalWrite(LATCH_PIN, 0);

    for(int i = 0; i < 8; i++){
	digitalWrite(CLOCK_PIN, 0);
	digitalWrite(CLOCK_PIN, 1);
	
    }

    digitalWrite(LATCH_PIN, 1);
}



void showStatus(unsigned char showByte)
{
        digitalWrite(LATCH_PIN, 0);
	for(int bit = 0; bit < 8; bit++){
		digitalWrite(CLOCK_PIN, 0);
		digitalWrite(DATA_PIN, bitRead(showByte, bit));
		digitalWrite(CLOCK_PIN, 1);
	}
        digitalWrite(LATCH_PIN, 1);
}
