#include <Arduino.h>

#include <SoftwareSerial.h>
#include <SdFat.h>
#include <Wire.h>
#include <forcedClimate.h>
#include <DS1307.h>
#include <ChainableLED.h>
#include <EEPROM.h>
//#include "config.cpp"

// -- Pins --
// SoftSerial pins
#define RX 8
#define TX 9

// LED pins
#define LEDpin1 6
#define LEDpin2 7

// Light sensor pin
#define lightSensorPIN 2

// Button pins
#define greenButtonPIN 2
#define redButtonPIN 3

//SD card
#define chipSelect 4

// -- EEPROM Adresses --
// #define EEPROM_CompilerTimeApplied 1    // Set to true once __DATE__ has been written to the RTC once

// -- MISC --
#define buttonPressTime 5000000 // Time button has to be pressed for (in µs)

//TODO move global variables to EEPROM when possible

/*
===================================================
==================== LED Stuff ====================
===================================================
*/

ChainableLED leds(LEDpin1, LEDpin2, 1);

struct RGB {
    unsigned char R; //Red
    unsigned char G; //Green
    unsigned char B; //Blue
} Blue, Yellow, Orange, Red, Green, White;

void setUpColors(){
    Blue.R = 0;
    Blue.G = 0;
    Blue.B = 255;

    Yellow.R = 225;
    Yellow.G = 234;
    Yellow.B = 0;

    Orange.R = 255;
    Orange.G = 69;
    Orange.B = 0;

    Red.R = 255;
    Red.G = 0;
    Red.B = 0;

    Green.R = 0;
    Green.G = 255;
    Green.B = 0;

    White.R = 255;
    White.G = 255;
    White.B = 255;
}

void setLEDcolor(RGB RGBvalue){
    leds.setColorRGB(0, RGBvalue.R, RGBvalue.G, RGBvalue.B);
}

[[noreturn]] void blinkLED(RGB RGBvalue1, RGB RGBvalue2, int secondColorTimeMultiplier) {
    // Second color shines for secondColorTimeMultiplier longer than first one, overall frequency : 1 Hz

    unsigned short int color_1_time = 1000 / (secondColorTimeMultiplier + 1);
    unsigned short int color_2_time = ((1000 * secondColorTimeMultiplier) / (secondColorTimeMultiplier + 1));

    while(true) {
        // First color
        leds.setColorRGB(0, RGBvalue1.R, RGBvalue1.G, RGBvalue1.B);

        delay(color_1_time);

        // Second color
        leds.setColorRGB(0, RGBvalue2.R, RGBvalue2.G, RGBvalue2.B);

        delay(color_2_time);
    }
}

/*
===================================================
=================== System Stuff ==================
===================================================
*/

// -- Button Pressed Bools --
bool greenButtonPressed = false;
bool redButtonPressed = false;

String promptUserInput(const String& prompt) {
    Serial.println(prompt);

    while (Serial.available() == 0) {
        // TODO check if config mode timer has expired, return if yes
    }
    return Serial.readString();
}

// -- Measure timing variables --
//Interval between measures in ms (standard systemMode)
unsigned int LOG_INTERVALL = 2000;

// Time when economic / standard / maintenance systemMode will be executed next
unsigned int nextMeasure = 0;

// -- Enum containing all supported error states --
enum errorCase {RTC_error, GPS_error, Sensor_error, Data_error, SDfull_error, SDread_error};

// -- System error handling --
void criticalError(errorCase error) {
    // Block both interrupt functions
    redButtonPressed = true;
    greenButtonPressed = true;

    switch (error) {
        case RTC_error:
            blinkLED(Red, Blue, 1);

        case GPS_error:
            blinkLED(Red, Yellow, 1);

        case Sensor_error:
            blinkLED(Red, Green, 1);

        case Data_error:
            blinkLED(Red, Green, 2);

        case SDfull_error:
            blinkLED(Red, White, 1);

        case SDread_error:
            blinkLED(Red, White, 2);
    }
}

// -- Enum containing all possible system modes --
// DO NOT CHANGE THIS OUTSIDE THE 'switchMode()' FUNCTION OR STUFF WILL BREAK
enum systemMode {standard, economic, maintenance, config, noMode};

// -- System currentMode variable --
systemMode currentMode = standard;

// -- Next systemMode, changed by interrupts --
systemMode nextMode = noMode;

// -- Switch systemMode --
// Used to switch back from maintenance systemMode, contains either standard or economic
systemMode lastModeBeforeMaintenance;

void switchMode(systemMode newMode){
    // Reset nextMode, used to trigger 'switchMode()' in 'loop()'
    nextMode = noMode;

    // Sets the time threshold for the next measure back to 0
    nextMeasure = 0;

    switch (newMode) {
        // Standard
        case standard :
            setLEDcolor(Green);
            lastModeBeforeMaintenance = standard;
            break;

        // Economic
        case economic :
            setLEDcolor(Blue);
            lastModeBeforeMaintenance = economic;
            break;

        // Maintenance
        case maintenance:
            setLEDcolor(Orange);
            break;

        // Config
        case config:
            setLEDcolor(Yellow);
            break;

        case noMode:
            //noMode is not allowed as a system mode, returning to previous mode
            return;
    }
    currentMode = newMode;
}

// -- Interrupts --
// Contains the time in ms, when the system mode is changed,
// as long as the pressed button is not released before that time
unsigned int switchModeTimer = 0;


void greenButtonInterrupt() {
    noInterrupts();
    if (!redButtonPressed) {
        // The buttons are LOW active,
        // so 'not pressed' -> HIGH and 'pressed' -> LOW

        greenButtonPressed = !digitalRead(greenButtonPIN);
    }
    else {
        // Return if red button is already pressed
        interrupts();
        return;
    }

    //Print Green Button Interrupt
    Serial.println("GBI "+ String(greenButtonPressed));

    if (greenButtonPressed) {
        if (currentMode == standard) {
            nextMode = economic;
        }
        if (currentMode == economic) {
            nextMode = standard;
        }
        switchModeTimer = micros() + buttonPressTime;
    }
    else {
        nextMode = noMode;
        switchModeTimer = 0;
    }
    interrupts();
}

void redButtonInterrupt() {
    noInterrupts();
    if (!greenButtonPressed) {
        // The buttons are LOW active,
        // so 'not pressed' -> HIGH and 'pressed' -> LOW

        redButtonPressed = !digitalRead(redButtonPIN);
    }
    else {
        // Return if green button is already pressed
        interrupts();
        return;
    }

    //Print Red Button Interrupt
    Serial.println("RBI "+ String(redButtonPressed));

    if (redButtonPressed) {
        if (currentMode == standard or currentMode == economic) {
            nextMode = maintenance;
        }
        if (currentMode == maintenance) {
            nextMode = lastModeBeforeMaintenance;
        }
        switchModeTimer = micros() + buttonPressTime;
    }
    else {
        nextMode = noMode;
        switchModeTimer = 0;
    }
    interrupts();
}

/*
===================================================
================== BME 280 Stuff ==================
===================================================
*/

ForcedClimate BMESensor = ForcedClimate();

void configureBME() {
    Wire.begin();
    BMESensor.begin();
};

/*
===================================================
==================== RTC Stuff ====================
===================================================
*/

/*
struct RTC_time {
    unsigned short int year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char minute;
    unsigned char second;
    unsigned char day_of_week;

};
*/


//TODO optimize this
DS1307 clock;

/*
void configureRTC() {
    short int compilerDateAlreadySet;

    EEPROM.get(0, compilerDateAlreadySet);

    if (compilerDateAlreadySet == 255) {
        String compilationDate = __DATE__;
        String compilationTime = __TIME__;

        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

        char monthChar[4];

        unsigned short int year, month, day, hour, minute, second;

        sscanf(compilationDate.c_str(), "%3s %hd %hd", monthChar, &day, &year);

        sscanf(compilationTime.c_str(), "%hd:%hd:%hd", &hour, &minute, &second);


        month = 0;
        // Convert month String to int
        bool t = true;
        while (t) {
            if (strcmp(monthChar, months[month]) == 0) {
                t = false;
            }
            month++;
        }

        clock.fillByYMD(year,month,day); //Jan 19, 2013
        clock.fillByHMS(hour,minute,second); //15:28 30"
        // clock.fillDayOfWeek(day_of_week); //Saturday
        clock.setTime();//write time to the RTC chip

        // Serial.println("DOW : " + String(day_of_week));

        compilerDateAlreadySet = 100;
        EEPROM.put(0, compilerDateAlreadySet);
    }
}
 */

String getTime()
{
    String time="";
    clock.getTime();
    time+=String(clock.hour, DEC);
    time+=String(":");
    time+=String(clock.minute, DEC);
    time+=String(":");
    time+=String(clock.second, DEC);
    time+=String("-");
    time+=String(clock.month, DEC);
    time+=String("/");
    time+=String(clock.dayOfMonth, DEC);
    time+=String("/");
    time+=String(clock.year+2000, DEC);
    return time;
}

/*
===================================================
================== SD Card Stuff ==================
===================================================
*/

SdFat SD;
SdFile currentFile;

void configureSDCard() {
    // Stop execution of SD card fails

    //Serial.print("Initializing SD card...");

    if (!SD.begin(chipSelect, SPI_HALF_SPEED)){
        criticalError(SDread_error);
    }

    //Serial.println("SD Card initialized.");
}

// 8 characters are reserved for this String in 'Setup()'
String fileDate;

// 16 characters are reserved for this String in 'Setup()'
String fileName;

// Current LOG file revision number
unsigned short int revision = 1;

// Write a line to the current revision LOG file on the SD card
void writeToSD(const String& dataToWrite){
    //Reset fileDate
    fileDate = "";

    fileDate += clock.year;
    fileDate += "-";
    fileDate += clock.month;
    fileDate += "-";
    fileDate += clock.dayOfMonth;
    fileDate += "-";

    bool t = true;

    while(t){
        fileName = fileDate + String(revision) + ".txt";

        if (!currentFile.open(fileName.c_str(), O_RDWR | O_CREAT | O_AT_END)) {
            criticalError(SDread_error);
        }

        // If projected filesize < 4000 bytes
        if ((currentFile.fileSize() + sizeof(dataToWrite))<4000) {
            Serial.println("");
            Serial.println("F : " + fileName);
            t = false;
        }

        // If projected filesize > 4000 bytes
        else {
            Serial.println("F : " + fileName + " FULL");
            currentFile.close();
            revision++;
        }

    }

    Serial.println("S : " + String(currentFile.fileSize()) + " B");

    currentFile.println(dataToWrite);

    currentFile.close();
}

/*
===================================================
================ Light sensor Stuff ===============
===================================================
*/

String readLightSensor() {
    int data = analogRead(lightSensorPIN);
    return String(data);
}

/*
===================================================
==================== GPS Stuff ====================
===================================================
*/

// Open SoftSerial for GPS
SoftwareSerial SoftSerial(RX, TX);

void configureGPS() {
    //Serial.println("Opening SoftwareSerial for GPS");
    SoftSerial.begin(9600); // Open SoftwareSerial for GPS
}

// Contains the GPS data
// 75 characters are reserved for this String in 'Setup()'
String gpsData;

String readGPS() {
    bool t = false;

    if (SoftSerial.available()) // if data is coming from software serial port ==> data is coming from SoftSerial GPS
    {
        t=true;

        while(t) {
            gpsData = SoftSerial.readStringUntil('\n');

            //Serial.print(gpsData);

            gpsData.trim();

            //gpsData.replace("\r", "");

            if (gpsData.startsWith("$GPGGA",0)){
                t=false;
            }
        }
        return gpsData;
    }
    return "GPS error";
}

/*
===================================================
================== Standard Mode ==================
===================================================
*/

// -- Make a string for assembling the data to log --
// 125 characters are reserved for this String in 'Setup()'
String dataString;

// Separator placed between RTC, GPS and sensor data in 'dataString'
String valueSeparator = " ; ";

void standardMode() {
    // -- RTC Clock reading --
    dataString += getTime();

    dataString += valueSeparator;

    // -- GPS reading --
    dataString += readGPS();

    dataString += valueSeparator;

    // -- Luminosity captor reading --
    dataString += readLightSensor();

    dataString += valueSeparator;

    //-- BME280 Readings --
    BMESensor.takeForcedMeasurement();

    //Temperature
    dataString += String(BMESensor.getTemperatureCelcius());

    dataString += valueSeparator;

    //Humidity
    dataString += String(BMESensor.getRelativeHumidity());

    dataString += valueSeparator;

    //Pressure
    dataString += String(BMESensor.getPressure());

    // -- Write data to SD --
    writeToSD(dataString);

    // -- Print dataString to Serial
    Serial.println(dataString);

    Serial.println("");
}

/*
===================================================
================== Economic Mode ==================
===================================================
*/

// Determines if the GPS is read during the next execution of 'economicMode()'
bool readGPSnextExec = true;

// Power saving mode
// Identical to standard systemMode, except for doubled interval and GPS only being read 1/2 the time
void economicMode() {
    // -- RTC Clock reading --
    dataString += getTime();

    dataString += valueSeparator;

    // -- GPS reading --
    // Only called every second execution
    if (readGPSnextExec) {
        dataString += readGPS();

        dataString += valueSeparator;
    }

    readGPSnextExec = !readGPSnextExec;

    // -- Luminosity captor reading --
    dataString += readLightSensor();

    dataString += valueSeparator;

    // -- BME280 Readings --
    BMESensor.takeForcedMeasurement();

    // Temperature
    dataString += String(BMESensor.getTemperatureCelcius());

    dataString += valueSeparator;

    // Humidity
    dataString += String(BMESensor.getRelativeHumidity());

    dataString += valueSeparator;

    // Pressure
    dataString += String(BMESensor.getPressure());

    // -- Write data to SD --
    writeToSD(dataString);

    // -- Print dataString to Serial
    Serial.println(dataString);

    Serial.println("");
}

/*
===================================================
================= Maintenance Mode ================
===================================================
*/

void maintenanceMode() {
    // -- RTC Clock reading --
    dataString += getTime();

    dataString += valueSeparator;

    // -- GPS reading --
    dataString += readGPS();

    dataString += valueSeparator;

    // -- Luminosity captor reading --
    dataString += readLightSensor();

    dataString += valueSeparator;

    // -- BME280 Readings --
    BMESensor.takeForcedMeasurement();

    // Temperature
    dataString += String(BMESensor.getTemperatureCelcius());

    dataString += valueSeparator;

    // Humidity
    dataString += String(BMESensor.getRelativeHumidity());

    dataString += valueSeparator;

    // Pressure
    dataString += String(BMESensor.getPressure());

    // -- Print dataString to Serial
    Serial.println(dataString);

    Serial.println("");
}

/*
===================================================
================ Configuration Mode ===============
===================================================
*/

// TODO add config systemMode
void configMode() {
    Serial.println("Config Mode");
}

/*
===================================================
====================== Setup ======================
===================================================
*/

void setup() {
    // -- Configure LEDs --
    leds.init();

    setUpColors();

    // -- Configure buttons --
    pinMode(greenButtonPIN, INPUT_PULLUP);
    pinMode(redButtonPIN, INPUT_PULLUP);

    // -- Open serial communications and wait for port to open --
    Serial.begin(9600);

    while (!Serial) {}

    // -- Check if RED button is pressed for 5 sec, go to config systemMode --
    if (!digitalRead(redButtonPIN)) {
        unsigned long counter = micros()+buttonPressTime;
        bool g = true;
        while (g) {
            //Serial.println("Red is pressed :" + String(counter-millis()));
            if (digitalRead(redButtonPIN)) {
                //Serial.println("Red is not pressed");
                g = false;
            }
            else if (micros()>counter) {
                switchMode(config);
                g = false;
            }
        }
    }
    else {
        switchMode(standard);
    }

    // -- Configure RTC --
    // Initialize Clock
    clock.begin();

    // Set default time in the clock
    //configureRTC();

    // -- Configure BME --
    configureBME();

    // -- Configure GPS --
    configureGPS();

    while(!SoftSerial.available()) {
        ; // Wait until SoftSerial is open
    }

    // -- Configure SD Card --
    configureSDCard();

    // -- Setup interrupts for buttons --
    attachInterrupt(digitalPinToInterrupt(greenButtonPIN), greenButtonInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(redButtonPIN), redButtonInterrupt, CHANGE);

    // -- Reserve space for Strings to avoid fragmentation --
    // Contains sensor data
    dataString.reserve(125);

    // Contains GPS data
    gpsData.reserve(75);

    // Strings to assemble file name
    fileDate.reserve(8);
    fileName.reserve(16);
}

void loop() {
    if (nextMode == noMode) {
        if (millis() > nextMeasure) {
            switch (currentMode) {
                case standard:
                    // Set time for next measure
                    nextMeasure = millis() + LOG_INTERVALL;

                    // Reset dataString
                    dataString = "";

                    // Execute Mode
                    standardMode();
                    break;

                case economic:
                    // Set time for next measure
                    nextMeasure = millis() + (LOG_INTERVALL*2);

                    // Reset dataString
                    dataString = "";

                    // Execute Mode
                    economicMode();
                    break;

                case maintenance:
                    // Set time for next measure
                    nextMeasure = millis() + LOG_INTERVALL;

                    // Reset dataString
                    dataString = "";

                    // Execute Mode
                    maintenanceMode();
                    break;

                case config:
                    // Execute Mode
                    configMode();
                    break;

                case noMode:
                    // 'noMode' is not allowed as a system mode, switching to 'standard'
                    switchMode(standard);
                    break;
            }
        }
    }
    else if (micros() >= switchModeTimer) {
        greenButtonPressed = false;
        redButtonPressed = false;
        switchMode(nextMode);
    }
    //else {
    //    Serial.println(String(switchModeTimer - millis()));
    //}
}





