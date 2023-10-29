#include <Arduino.h>

#include <SoftwareSerial.h>
#include <SdFat.h>
#include <Wire.h>
#include <forcedClimate.h>
#include <DS1307.h>
#include <ChainableLED.h>
#include <EEPROM.h>

// SoftSerial pins
#define RX 8
#define TX 9

// LED Pins
#define LEDpin1 6
#define LEDpin2 7

// Light sensor Stuff
#define lightSensorPIN 2

// Button pins
#define greenButtonPIN 2
#define redButtonPIN 3

#define buttonPressTime 5000 // Time button has to be pressed for (in ms)

// EEPROM Adresses
// #define EEPROM_CompilerTimeApplied 1    // Set to true once __DATE__ has been written to the RTC once

//TODO move global variables to EEPROM

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


// -- Measure timing variables --
unsigned int LOG_INTERVALL = 2000; //Interval between measures in ms (standard systemMode)

unsigned int nextMeasure = 0; // Time when economic/standard systemMode will be executed next

enum errorCase {RTC_error, GPS_error, Sensor_error, Data_error, SDfull_error, SDread_error};

// -- System error handling --
void criticalError(errorCase error) {
    noInterrupts();

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
enum systemMode {standard, economic, maintenance, config, noMode}; // DO NOT CHANGE THIS OUTSIDE THE 'switchMode()' FUNCTION OR STUFF WILL BREAK

// -- System currentMode variable --
systemMode currentMode = standard;

// -- Next systemMode, changed by interrupts --
systemMode nextMode = noMode;

// -- Switch systemMode --
// Used to switch back from maintenance systemMode, contains either standard or economic
systemMode lastModeBeforeMaintenance;

void switchMode(systemMode newMode){
    nextMode = noMode;

    currentMode = newMode;

    Serial.println(currentMode);

    nextMeasure = 0; // Sets the time threshold for the next measure back to 0

    switch (currentMode) {
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
    }
    return;
}

// -- Interrupts --
unsigned int switchModeTimer = 0;

bool greenButtonPressed = 0;
bool redButtonPressed = 0;

void greenButtonInterrupt() {
    Serial.println("GBI");

    if (!redButtonPressed) {
        bool read = !digitalRead(greenButtonPIN);

        if (read) {
            greenButtonPressed = true;
        }
        else {
            greenButtonPressed = false;
        }
    }

    if (greenButtonPressed) {
        if (currentMode == standard) {
            nextMode = economic;
            switchModeTimer = millis() + buttonPressTime;
        }
        if (currentMode == economic) {
            nextMode = standard;
            switchModeTimer = millis() + buttonPressTime;
        }
    }
    else {
        nextMode = noMode;
        switchModeTimer = 0;
    }

}

void redButtonInterrupt() {
    Serial.println("RBI");

    if (!greenButtonPressed) {
        bool read = !digitalRead(redButtonPIN);

        if (read) {
            redButtonPressed = true;
        }
        else {
            redButtonPressed = false;
        }
    }

    if (redButtonPressed) {
        if (currentMode == standard or currentMode == economic) {
            nextMode = maintenance;
            switchModeTimer = millis() + buttonPressTime;
        }
        if (currentMode == maintenance) {
            switchModeTimer = millis() + buttonPressTime;
            nextMode = lastModeBeforeMaintenance;
        }
    }
    else {
        nextMode = noMode;
        switchModeTimer = 0;
    }

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

struct RTC_time {
    unsigned short int year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char minute;
    unsigned char second;
    unsigned char day_of_week;

};


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

        // int day_of_week = (day += month < 3 ? year-- : year - 2, 23*month/9 + day + 4 + year/4- year/100 + year/400)%7;

        clock.fillByYMD(year,month,day); //Jan 19,2013
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
    time+=String(" ");
    return time;
}

/*
===================================================
================== SD Card Stuff ==================
===================================================
*/

#define chipSelect 4 // (SD card reader model)

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

void writeToSD(String dataToWrite){

    //TODO add String.reserve()

    String fileDate;
    fileDate.reserve(8);

    fileDate += clock.year;
    fileDate += "-";
    fileDate += clock.month;
    fileDate += "-";
    fileDate += clock.dayOfMonth;
    fileDate += "-";


    static unsigned short int revision = 1;

    String fileName;
    fileName.reserve(12);

    bool t = true;

    while(t){
        fileName = fileDate + String(revision) + ".txt";

        if (!currentFile.open(fileName.c_str(), O_RDWR | O_CREAT | O_AT_END)) {
            criticalError(SDread_error);
        }

        //If filesize < 4000 byte
        if ((currentFile.fileSize()+sizeof(dataToWrite))<4000) {
            Serial.println("");
            Serial.println("F : " + fileName);
            t = false;
        }

        //If filesize > 4000 byte
        else {
            Serial.println("F : " + fileName + " FULL");
            currentFile.close();
            revision++;
        }

    }

    Serial.println("FS : " + String(currentFile.fileSize()) + " B");

    //Serial.println("D : " + dataToWrite);
    Serial.println("");

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

SoftwareSerial SoftSerial(RX, TX); // Serial already used for serial communication GPS connected on UART port on Grove SD Card Shield

void configureGPS() {
    //Serial.println("Opening SoftwareSerial for GPS");
    SoftSerial.begin(9600); // Open SoftwareSerial for GPS
}

String readGPS() {
    String gpsData;

    bool t = false;
    if (SoftSerial.available()) // if data is coming from software serial port ==> data is coming from SoftSerial GPS
    {
        t=true;
        while(t){
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

String gpsData;

/*
===================================================
================== Standard Mode ==================
===================================================
*/

void standardMode() {
    // -- Set time for next measure --
    nextMeasure = millis() + LOG_INTERVALL;

    // -- Make a string for assembling the data to log --
    //TODO : Make 'dataString' a global variable
    String dataString;

    dataString.reserve(125);

    // -- RTC CLock reading --
    dataString+=getTime() + " ; ";


    // -- Luminosity captor reading --
    dataString += readLightSensor();

    dataString += " ; ";


    // -- GPS reading --
    dataString += readGPS();

    dataString += " ; ";

    //-- BME280 Readings --
    BMESensor.takeForcedMeasurement();

    //Temperature

    dataString += String(BMESensor.getTemperatureCelcius());

    dataString += " ; ";

    //Humidity

    dataString += String(BMESensor.getRelativeHumidity());

    dataString += " ; ";

    //Pressure

    dataString += String(BMESensor.getPressure());

    //Write data to SD
    writeToSD(dataString);
}

/*
===================================================
================== Economic Mode ==================
===================================================
*/

bool readGPSnextExec = true;

void economicMode() { // Identical to standard systemMode, except for doubled interval and GPS only being read 1/2 the time

    // Set time for next measure
    nextMeasure = millis() + (LOG_INTERVALL*2);

    // make a string for assembling the data to log:
    String dataString = "";

    // RTC CLock reading
    dataString+=getTime() + " ; ";


    // Luminosity captor reading
    dataString += readLightSensor();

    dataString += " ; ";


    // GPS reading - Only called every second execution
    if (readGPSnextExec) {
        dataString += readGPS();

        dataString += " ; ";
    }

    readGPSnextExec = !readGPSnextExec;

    // -- BME280 Readings --
    BMESensor.takeForcedMeasurement();

    // Temperature

    dataString += String(BMESensor.getTemperatureCelcius());

    dataString += " ; ";

    // Humidity

    dataString += String(BMESensor.getRelativeHumidity());

    dataString += " ; ";

    // Pressure

    dataString += String(BMESensor.getPressure());

    // -- Write data to SD
    writeToSD(dataString);
}

/*
===================================================
================= Maintenance Mode ================
===================================================
*/

void maintenanceMode() {
    // -- Make a string for assembling the data to log --
    String dataString = "";

    // -- RTC CLock reading --
    dataString+=getTime() + " ; ";


    // -- Luminosity captor reading --
    dataString += readLightSensor();

    dataString += " ; ";


    // -- GPS reading --
    dataString += readGPS();

    dataString += " ; ";

    // -- BME280 Readings --
    BMESensor.takeForcedMeasurement();

    // Temperature
    dataString += String(BMESensor.getTemperatureCelcius());

    dataString += " ; ";

    // Humidity
    dataString += String(BMESensor.getRelativeHumidity());

    dataString += " ; ";

    // Pressure
    dataString += String(BMESensor.getPressure());

    // -- Print to Serial --
    Serial.println("D : " + dataString);
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
    pinMode(greenButtonPIN, INPUT);
    pinMode(redButtonPIN, INPUT);

    // -- Open serial communications and wait for port to open --
    Serial.begin(9600);

    while (!Serial) {}

    // -- Check if RED button is pressed for 5 sec, go to config systemMode --
    if (!digitalRead(redButtonPIN)) {
        unsigned long counter = millis()+5000;
        bool g = true;
        while (g) {
            //Serial.println("Red is pressed :" + String(counter-millis()));
            if (digitalRead(redButtonPIN)) {
                //Serial.println("Red is not pressed");
                g = false;
            }
            else if (millis()>counter) {
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

    attachInterrupt(digitalPinToInterrupt(greenButtonPIN), greenButtonInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(redButtonPIN), redButtonInterrupt, CHANGE);
}

void loop() {
    if (nextMode == noMode) {
        if (millis() > nextMeasure) {
            switch (currentMode) {
                case standard:
                    standardMode();
                    break;
                case economic:
                    economicMode();
                    break;
                case maintenance:
                    maintenanceMode();
                    break;
                case config:
                    configMode();
                    break;
            }
        }
    }
    else if (millis() >= switchModeTimer) {
        switchMode(nextMode);
    }
    /*else {
        Serial.println("T : " + String(switchModeTimer - millis()) + " M : " + nextMode + " L : " + lastModeBeforeMaintenance);
    }*/
}





