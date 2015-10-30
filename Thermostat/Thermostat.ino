/*
 Name:		Thermostat.ino
 Created:	10/28/2015 7:56:58 PM
 Author:	Andy
*/

//TODO handle wrapping millis() long or it won't work after so many days powered on

// include the library code:
#include <LiquidCrystal.h>
#include "RunningAverage.h"

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

// set up a constant for the tilt switchPin
const int tempUpPin = 5;
const int tempDownPin = 4;
const int sensorPin = A0;
const int ledPin = 6;
//const long shortCycleDelay = 600000; //minimum 10 minutes between runs
//const long maxRunTime = 900000; //max 15 minutes per run
//const long minRunTime = 60000; //min 1 minute run time
const long shortCycleDelay = 60000; //minimum 10 minutes between runs
const long maxRunTime = 5000; //max 15 minutes per run
const long minRunTime = 2000; //min 1 minute run time
RunningAverage myRA(10);

// variable to hold the value of the switchPin
int tempUpSwitchState = 0;
int tempDownSwitchState = 0;
int tempUpPrevSwitchState = 0;
int tempDownPrevSwitchState = 0;

int tempTarget = 0;
unsigned long time;
unsigned long prevTime;
unsigned long forceShutoffTime;
unsigned long dontStopUntilTime;
unsigned long dontRunAgainUntilTime;
bool isRunning;


float currTemp() {
	int sensorVal = analogRead(sensorPin);
	float voltage = (sensorVal / 1024.0) * 4.4;
	return (voltage - .5) * 100 * 1.8 + 32;
}

void printTempsToLcd(float currTemp, float avgTemp) {
	lcd.setCursor(2, 0);
	lcd.print(currTemp, 1);

	lcd.setCursor(12, 0);
	lcd.print(avgTemp, 1);
}

void printTargetToLcd(int target) {
	lcd.setCursor(8, 1);
	lcd.print(target);
}

void serialLog(char* msg) {
	Serial.println(msg);
}


void setup() {
	Serial.begin(9600);

	// set up the number of columns and rows on the LCD 
	lcd.begin(16, 2);

	// set up the switch pin as an input
	pinMode(tempUpPin, INPUT);
	pinMode(tempDownPin, INPUT);
	pinMode(ledPin, OUTPUT);

	// Print a message to the LCD.
	lcd.setCursor(0, 0);
	lcd.print("C:     Avg: ");
	lcd.setCursor(0, 1);
	lcd.print("Target: ");

	tempTarget = 70;
	myRA.clear();

	lcd.setCursor(8, 1);
	lcd.print(tempTarget);

	forceShutoffTime = 4294967295;
	dontRunAgainUntilTime = 0;
	dontStopUntilTime = 0;
	isRunning = false;
}

void startFurnace() {
	serialLog("Starting");
	if (!isRunning) {
		serialLog("Not running!");
		if (millis() >= dontRunAgainUntilTime) {
			serialLog("millis > dontRunAgainTime");
			digitalWrite(ledPin, HIGH);
			//TODO turn on furnace
			isRunning = true;
			dontStopUntilTime = millis() + minRunTime;
			forceShutoffTime = millis() + maxRunTime;
		}
	}
}

void stopFurnace() {
	serialLog("Stopping");
	if (isRunning) {
		if (millis() > dontStopUntilTime) {
			serialLog("Running");
			digitalWrite(ledPin, LOW);
			//TODO stop furnace
			isRunning = false;
			dontRunAgainUntilTime = millis() + shortCycleDelay;
		}
	}
}

void send_float(float arg)
{
	// get access to the float as a byte-array:
	byte * data = (byte *)&arg;

	// write the data to the serial
	Serial.write(data, sizeof(arg));
}

void loop() {

	if (isRunning) {
		digitalWrite(ledPin, HIGH);
	}

	time = millis();

	if (time - prevTime > 1000) {
		
		float fCurrTemp = currTemp();
		myRA.addValue(fCurrTemp);
		float currAvg = myRA.getAverage();

		printTempsToLcd(fCurrTemp, currAvg);

		if (currAvg < tempTarget - 1.5) {
			if (millis() > forceShutoffTime) {
				//force stop if it's been on for too long (something might be wrong)
				stopFurnace();
			}
			startFurnace();
		}
		else {
			stopFurnace();
		}

		/*Serial.write("C:");
		Serial.print(fCurrTemp, 1);

		Serial.write(", Avg:");
		Serial.print(currAvg, 1);
		Serial.println();

		Serial.flush();*/

		prevTime = time;
	}


	// check the status of the switch
	tempUpSwitchState = digitalRead(tempUpPin);
	tempDownSwitchState = digitalRead(tempDownPin);

	// compare the switchState to its previous state
	if (tempUpSwitchState == 1 && tempUpPrevSwitchState == 0) {
		tempTarget += 1;
	}

	if (tempDownSwitchState == 1 && tempDownPrevSwitchState == 0) {
		tempTarget -= 1;
	}

	printTargetToLcd(tempTarget);

	tempUpPrevSwitchState = tempUpSwitchState;
	tempDownPrevSwitchState = tempDownSwitchState;
}
