/*
 Name:		Thermostat.ino
 Created:	10/28/2015 7:56:58 PM
 Author:	Andy
*/

//TODO handle wrapping millis() long or it won't work after so many days powered on

// include the library code:
#include "Wire.h"
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "Adafruit_STMPE610.h"
#include "RunningAverage.h"
#include "Config.h"
#include <SoftwareSerial.h>

// For the Adafruit shield, these are the default.
#define TFT_DC 9
#define TFT_CS 10

// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 150
#define TS_MINY 130
#define TS_MAXX 3800
#define TS_MAXY 4000

#define STMPE_CS 8
Adafruit_STMPE610 ts = Adafruit_STMPE610(STMPE_CS);

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

#define TEMP_UP_X 268
#define TEMP_UP_Y 5
#define TEMP_DOWN_X 268
#define TEMP_DOWN_Y 125
#define TEMP_BUTTON_WIDTH 50
#define TEMP_BUTTON_HEIGHT 110
#define FAN_BUTTON_X 5
#define FAN_BUTTON_Y 185
#define FAN_BUTTON_WIDTH 120
#define FAN_BUTTON_HEIGHT 50

#define TOUCH_DELAY 120

int sw;
int sh;
unsigned long lastTouchMillis;

RunningAverage tempRunningAvg(RUNNING_AVG_LENGTH);
SoftwareSerial esp8266(WIFI_RX_PIN, WIFI_TX_PIN);

unsigned char tempTarget = 0;
bool forceFanOn = false;
bool wifiConnected = false;
bool readingCommand = false;
unsigned long time;
unsigned long prevTime;
unsigned long forceShutoffTime;
unsigned long dontStopUntilTime;
unsigned long dontRunAgainUntilTime;
unsigned long lastCheckWifiTime;
unsigned long tempLogTime;
unsigned long lastTempLogTime;
unsigned int ESP_SERIAL_IN_LEN = 20;
char* espSerialRecvBuffer = new char[ESP_SERIAL_IN_LEN];
int espSerialRecvBufferIdx = 0;
bool isRunning;
 
//int freeRam()
//{
//	extern int __heap_start, *__brkval;
//	int v;
//	return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
//}

//String waitMultiple(String target1, String target2, uint32_t timeout = 1000)
//{
//	String data;
//	char a;
//	unsigned long start = millis();
//	while (millis() - start < timeout) {
//		while (esp8266.available() > 0) {
//			a = esp8266.read();
//			data += a;
//		}
//		if (data.indexOf(target1) != -1) {
//			break;
//		}
//		else if (data.indexOf(target2) != -1) {
//			break;
//		}
//	}
//	return data;
//}

void checkWifi()
{
	char returnChar;
	//espReadAll(false); //clear any buffered chars and start fresh
	esp8266.println("print(wifi.sta.status())");
	delay(500);
	char* wifiIp;
	if (esp8266.available() > 0)
	{
		returnChar = esp8266.read();
		if (returnChar == '5')
		{
			
			wifiConnected = true;

			esp8266.find('>');

			wifiIp = new char[16];
			esp8266.println("print(wifi.sta.getip())");
			esp8266.find('1'); //the '1' in 192.168....
			delay(50);
			char returnChar2; int cntr = 1;
			
			wifiIp[0] = '1';
			while (esp8266.available() > 0 && cntr < 15)
			{
				returnChar2 = esp8266.read();
				if (returnChar2 == '\t') break;
				wifiIp[cntr] = returnChar2;
				cntr++;
				wifiIp[cntr] = '\0';
				delay(50);
			}
		}
		else
		{
			wifiIp = new char[0];
			//Serial.print("NOT CONNECTED: ");
			//Serial.println(returnChar);
			wifiConnected = false;
		}
	}

	printWifiStatus(wifiIp);
}

void setupWifi()
{
	//turn echo off
	esp8266.println("uart.setup(0,9600,8,0,1,0)");
	esp8266.find('>');

	//set station mode
	esp8266.println("wifi.setmode(wifi.STATION)");
	esp8266.find('>');

	//set ssid and password
	esp8266.print("wifi.sta.config(\"");
	esp8266.print(WIFI_SSID);
	esp8266.print("\", \"");
	esp8266.print(WIFI_PASSWORD);
	esp8266.println("\")");
	esp8266.find('>');
}

//void espReadAll(bool printToSerial)
//{
//	char c;
//	while (esp8266.available() > 0)
//	{
//		c = esp8266.read();
//		if(printToSerial)
//		{
//			Serial.write(c);
//		}
//	}
//	Serial.println();
//}

bool wifiSend(const char* httpCommand)
{
	esp8266.println("sk=net.createConnection(net.TCP, 0)");
	esp8266.find('>');
	//readAll(false);

	esp8266.println("sk:on(\"receive\", function(sck, c) print(c) end )");
	esp8266.find('>');
	//readAll(false);

	esp8266.print("sk:connect(");
	esp8266.print(WIFI_LOGGING_PORT);
	esp8266.print(", \"");
	esp8266.print(WIFI_LOGGING_IP);
	esp8266.println("\")");
	esp8266.find('>');
	//readAll(false);

	esp8266.print("sk:send(\"");
	esp8266.print(httpCommand);
	esp8266.println("\")");

	return true;
}

//TODO optimize these two
bool wifiLogAction(char* action, char* value)
{
	int payloadLen = strlen(action) + strlen(value) + strlen(WIFI_AUTHKEY) + 15;
	char* payload = new char[payloadLen];

	strcat(payload, "a=");
	strcat(payload, action);
	strcat(payload, "&v=");
	strcat(payload, value);
	strcat(payload, "&authKey=");
	strcat(payload, WIFI_AUTHKEY);

	/*String payload = "a=";
	payload += action;
	payload += "&v=";
	payload += value;
	payload += "&authKey=";
	payload += WIFI_AUTHKEY;*/

	String post = "POST /api/thermostat_log.php HTTP/1.0\\r\\nContent-Type: application/x-www-form-urlencoded\\r\\nContent-Length: ";
	post += strlen(payload);
	post += "\\r\\n\\r\\n";
	post += payload;

	wifiSend(post.c_str());
}

bool logTemperature()
{
	String payload = "t=";
	payload += tempRunningAvg.getAverage();
	payload += "&l=thermostat&authKey=";
	payload += WIFI_AUTHKEY;

	String post = "POST /api/temp_log.php HTTP/1.0\\r\\nContent-Type: application/x-www-form-urlencoded\\r\\nContent-Length: ";
	post += payload.length();
	post += "\\r\\n\\r\\n";
	post += payload;

	wifiSend(post.c_str());
}

int readVcc()
// Calculate current Vcc in mV from the 1.1V reference voltage
{
	long result;

	// Read 1.1V reference against AVcc
	ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
	delay(2); // Wait for Vref to settle
	ADCSRA |= _BV(ADSC); // Convert
	while (bit_is_set(ADCSRA, ADSC));
	result = ADCL;
	result |= ADCH << 8;
	result = 1126400L / result; // Back-calculate AVcc in mV

	return(result);
}

float currTemp() {
	int sensorVal = analogRead(sensorPin);
	float millivolts = (sensorVal / 1024.0) * readVcc();
	float celsius = millivolts / 10 - 50;
	float fahrenheit = celsius * 1.8 + 32;
	return fahrenheit - 5;
}

void printTargetToLcd() {
	int x = sw / 2 - 20;
	int y = sh / 2 - 25;
	tft.fillRect(x, y, 75, 45, ILI9341_BLACK);
	tft.setCursor(x, y); tft.setTextSize(6); tft.setTextColor(ILI9341_GREENYELLOW);
	tft.print(tempTarget);
}

void printAvgTempToLcd(float avg) {
	int x = 5;
	int y = sh/2 - 15;
	tft.fillRect(x, y, 110, 45, ILI9341_BLACK);
	tft.setCursor(x, y); tft.setTextSize(3); tft.setTextColor(ILI9341_DARKGREY);
	tft.print(avg, 1);
}

void printFanButton() {
	uint16_t buttonColor;
	uint16_t textColor;
	char* buttonText;

	if (forceFanOn) {
		buttonColor = ILI9341_ORANGE;
		textColor = ILI9341_WHITE;
		buttonText = "Fan ON";
	}
	else {
		buttonColor = ILI9341_DARKGREEN;
		textColor = ILI9341_WHITE;
		buttonText = "Fan AUTO";
	}

	tft.fillRect(FAN_BUTTON_X, FAN_BUTTON_Y, FAN_BUTTON_WIDTH, FAN_BUTTON_HEIGHT, buttonColor);
	tft.setCursor(FAN_BUTTON_X + 10, FAN_BUTTON_Y + 20); tft.setTextSize(2); tft.setTextColor(textColor);
	tft.print(buttonText);
}

void printTemplate() {
	int x = sw / 2 - 20;
	int y = sh / 2 - 40;
	tft.setCursor(x, y); tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
	tft.print("Target:");

	x = 5;
	y = sh / 2 - 30;
	tft.setCursor(x, y); tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
	tft.print("Temperature:");
}

void printWifiStatus(char* ip)
{
	tft.fillRect(0, 0, 400, 20, ILI9341_BLACK);

	//wifi status
	tft.setCursor(0, 0);
	tft.setTextSize(1);

	if (wifiConnected)
	{
		tft.setTextColor(ILI9341_WHITE);
		tft.print("WIFI: CONNECTED");
		tft.setCursor(100, 0);
		tft.setTextColor(ILI9341_GREENYELLOW);
		tft.print("(");
		tft.print(ip);
		tft.print(")");
	}
	else
	{
		tft.setTextColor(ILI9341_RED);
		tft.println("WIFI: DISCONNECTED");
	}
}

void startFurnace() {
	if (tempRunningAvg.getCount() >= RUNNING_AVG_LENGTH //don't start until our running average is full (could be inaccurate otherwise)
		&& !isRunning //don't start if it's already running
		&& millis() >= dontRunAgainUntilTime) //don't start if it's before the safe delay
	{ 
		//turn on furnace
		digitalWrite(furnaceTriggerPin, LOW); //relay is triggered on low signal
		isRunning = true;
		dontStopUntilTime = millis() + minRunTime;
		forceShutoffTime = millis() + maxRunTime;
		wifiLogAction("Furnace", "On");
	}
}

void stopFurnace() {
	if (isRunning //only try to stop of it's running
		&& millis() > dontStopUntilTime) //make sure it's not trying to stop within the short cycle delay
	{
		//stop furnace
		digitalWrite(furnaceTriggerPin, HIGH);
		isRunning = false;
		dontRunAgainUntilTime = millis() + shortCycleDelay;
		wifiLogAction("Furnace", "Off");
	}
}

void checkInputs()
{
	// See if there's any  touch data for us
	if (!ts.bufferEmpty() && millis() - lastTouchMillis > TOUCH_DELAY)
	{
		// Retrieve a point  
		TS_Point p = ts.getPoint();

		// Scale using the calibration #'s
		// and rotate coordinate system
		p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());
		p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());
		int y = tft.height() - p.x;
		int x = p.y;

		if (x > TEMP_UP_X && x < TEMP_UP_X + TEMP_BUTTON_WIDTH && y > TEMP_UP_Y && y < TEMP_UP_Y + TEMP_BUTTON_HEIGHT)
		{
			tempTarget += 1;
			printTargetToLcd();
		}
		else if (x > TEMP_DOWN_X && x < TEMP_DOWN_X + TEMP_BUTTON_WIDTH && y > TEMP_DOWN_Y && y < TEMP_DOWN_Y + TEMP_BUTTON_HEIGHT)
		{
			tempTarget -= 1;
			printTargetToLcd();
		}
		else if (x > FAN_BUTTON_X && x < FAN_BUTTON_X + FAN_BUTTON_WIDTH && y > FAN_BUTTON_Y && y < FAN_BUTTON_Y + FAN_BUTTON_HEIGHT)
		{
			forceFanOn = !forceFanOn;
			printFanButton();
		}

		lastTouchMillis = millis();


	}

	//clear the buffer
	while (!ts.bufferEmpty()) ts.getPoint();
}

void setup() {
	Serial.begin(115200);
	esp8266.begin(9600);

	// init the tft display
	tft.begin();
	tft.setRotation(1);
	sw = tft.width();
	sh = tft.height();
	lastTouchMillis = millis();
	lastCheckWifiTime = millis();
	lastTempLogTime = millis();

	//start touchscreen
	ts.begin();

	//black background
	tft.fillScreen(ILI9341_BLACK);

	//up/down buttons
	tft.fillRect(TEMP_UP_X, TEMP_UP_Y, TEMP_BUTTON_WIDTH, TEMP_BUTTON_HEIGHT, ILI9341_BLUE);
	tft.setCursor(sw - 35, 47); tft.setTextSize(3);
	tft.print("+");
	tft.fillRect(TEMP_DOWN_X, TEMP_DOWN_Y, TEMP_BUTTON_WIDTH, TEMP_BUTTON_HEIGHT, ILI9341_BLUE);
	tft.setCursor(sw - 35, sh - 75); tft.setTextSize(3);
	tft.print("-");

	// set up the inputs/outputs
	pinMode(furnaceTriggerPin, OUTPUT);
	digitalWrite(furnaceTriggerPin, HIGH);

	//initialize variables
	tempTarget = 65;
	tempRunningAvg.clear();
	forceShutoffTime = 4294967295; //default to max long
	dontRunAgainUntilTime = 0;
	dontStopUntilTime = 0;
	isRunning = false;

	printTemplate();
	printTargetToLcd();
	printFanButton();

	setupWifi();
	checkWifi();
}

void loop() {

	char charRead;
	while (esp8266.available() > 0)
	{
		charRead = esp8266.read();

		if (charRead == '|')
		{
			if (readingCommand)
			{
				//end of command
				if (strncmp(espSerialRecvBuffer, "|settemp:", 9) == 0)
				{
					//set temperature
					int numFirstChar = espSerialRecvBuffer[9] - '0';
					int numSecondChar = espSerialRecvBuffer[10] - '0';
					int newTemperature = 10 * numFirstChar + numSecondChar;
					tempTarget = newTemperature;
					printTargetToLcd();

					espSerialRecvBufferIdx = 0;
					readingCommand = false;
				}
			}
			else
			{
				espSerialRecvBufferIdx = 0;
				readingCommand = true;
			}
		}

		if (espSerialRecvBufferIdx < ESP_SERIAL_IN_LEN - 1)
		{
			espSerialRecvBuffer[espSerialRecvBufferIdx] = charRead;
			espSerialRecvBufferIdx++;
			espSerialRecvBuffer[espSerialRecvBufferIdx] = '\0';
		}
	}

	/*if (esp8266.available() > 0)
	{
		Serial.print("ESP OUTPUT/");
		Serial.print(millis());
		Serial.print("/");
		while (esp8266.available() > 0)
		{
			Serial.write(esp8266.read());
		}
		Serial.println("/END ESP OUTPUT");
	}*/

	time = millis();

	if (time - prevTime > TEMP_READ_DELAY) {

		float fCurrTemp = currTemp();
		tempRunningAvg.addValue(fCurrTemp);
		float currAvg = tempRunningAvg.getAverage();

		printAvgTempToLcd(currAvg);

		if (currAvg < tempTarget - 1) {
			if (millis() > forceShutoffTime) {
				//force stop if it's been on for too long (something might be wrong)
				//TODO alert via wifi that it's hit the force shutoff time
				stopFurnace();
			}
			startFurnace();
		}
		else if(currAvg >= tempTarget + 0.4) //we've reached the target!
		{
			stopFurnace();
		}
		
		//store previous time
		prevTime = time;
	}

	if (time - lastCheckWifiTime > WIFI_CHECK_PERIOD)
	{
		checkWifi();
		lastCheckWifiTime = time;
	}

	//Serial.println(time-lastTempLogTime);
	//Serial.println(TEMP_LOG_PERIOD);
	if (time - lastTempLogTime > TEMP_LOG_PERIOD)
	{
		logTemperature();
		lastTempLogTime = time;
	}

	checkInputs();
	
}
