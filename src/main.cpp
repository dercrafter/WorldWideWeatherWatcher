#include <Arduino.h>

#include <SoftwareSerial.h>
#include <SdFat.h>
#include <Wire.h>
#include <forcedClimate.h>
#include <DS1307.h>
#include <ChainableLED.h>
#include <EEPROM.h>

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

// SD card
#define chipSelect 4

// -- MISC --
#define buttonPressTime 5000000  // Time button has to be pressed for (in µs)
#define configTimeout 1800000    // Time no cmd has to be entered for, to exit config mode (in ms)

#define deviceID 69
#define programVersion 420

//TODO move global variables to EEPROM when possible

// -- EEPROM Adresses --
#define EEPROM_BOOL_programHasRunBefore 1     // Set to true if the program has been executed before, since having been written to the arduino's flash
#define EEPROM_configuration 2                // Contains the system configuration



/**
=================================================== \n
====================== LED Stuff ===================== \n
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

/**
=================================================== \n
===================== System Stuff ==================== \n
===================================================
*/

// Configuration struct
struct configuration {
    bool ACTIVATE_LUMINOSITY_SENSOR;                // Determines if luminosity sensor is active
    unsigned short int LUMINOSITY_LOW_THRESHOLD;    // Threshold value for luminosity sensor reading to be considered 'LOW'
    unsigned short int LUMINOSITY_HIGH_THRESHOLD;   // Threshold value for luminosity sensor reading to be considered 'HIGH'
    bool ACTIVATE_THERMOMETER;                      // Determines if thermometer is active
    char THERMOMETER_MIN_TEMPERATURE;               // Lowest thermometer value that is considered valid
    char THERMOMETER_MAX_TEMPERATURE;               // Highest thermometer value that is considered valid
    bool ACTIVATE_HYGROMETRY_SENSOR;                // Determines if hygrometry sensor is active
    char MIN_TEMPERATURE_FOR_HYGROMETRY;            // Lowest temperature at which the hygrometry sensor is still read
    char MAX_TEMPERATURE_FOR_HYGROMETRY;            // Highest temperature at which the hygrometry sensor is still read
    bool ACTIVATE_PRESSURE_SENSOR;                  // Determines if pressure sensor is active
    unsigned short int MIN_VALID_PRESSURE;          // Lowest pressure value that is considered a valid reading
    unsigned short int MAX_VALID_PRESSURE;          // Highest pressure value that is considered a valid reading
    unsigned char LOG_INTERVALL;                    // Intervall between readings (in minutes)
    unsigned char TIMEOUT;                          // Determiner after how much time of a sensor not responding, a Timeout is triggered
    unsigned short int FILE_MAX_SIZE;               // Maximum file size, when reached a new file is created
} currentSystemConfiguration;

void defaultConfig() {
    currentSystemConfiguration.ACTIVATE_LUMINOSITY_SENSOR = true;
    currentSystemConfiguration.LUMINOSITY_LOW_THRESHOLD = 255;
    currentSystemConfiguration.LUMINOSITY_HIGH_THRESHOLD = 768;
    currentSystemConfiguration.ACTIVATE_THERMOMETER = true;
    currentSystemConfiguration.THERMOMETER_MIN_TEMPERATURE = -10;
    currentSystemConfiguration.THERMOMETER_MAX_TEMPERATURE = 60;
    currentSystemConfiguration.ACTIVATE_HYGROMETRY_SENSOR = true;
    currentSystemConfiguration.MIN_TEMPERATURE_FOR_HYGROMETRY = 0;
    currentSystemConfiguration.MAX_TEMPERATURE_FOR_HYGROMETRY = 50;
    currentSystemConfiguration.ACTIVATE_PRESSURE_SENSOR = true;
    currentSystemConfiguration.MIN_VALID_PRESSURE = 850;
    currentSystemConfiguration.MAX_VALID_PRESSURE = 1080;
    currentSystemConfiguration.LOG_INTERVALL = 10;
    currentSystemConfiguration.TIMEOUT = 30;
    currentSystemConfiguration.FILE_MAX_SIZE = 4096;
}



// Set to false if this is the first time program is being executed since having been flashed onto the arduino
// this is being used to determine whether the RTC needs to be set to the correct time and if there is already a
// config that needs to be loaded from EEPROM
bool programHasRunBefore = false;

// -- Button Pressed bools --
bool greenButtonPressed = false;
bool redButtonPressed = false;


// -- Measure timing variables --
//Interval between measures in ms (standard systemMode)
unsigned int LOG_INTERVALL = 2000;

// Time when economic / standard / maintenance systemMode will be executed next
unsigned int nextMeasureTimer = 0;

// -- Enum containing all supported error states --
enum errorCase {RTC_error, GPS_error, Sensor_error, Data_error, SDfull_error, SDread_error};

// -- System error handling --
void criticalError(errorCase error) {
    // Block both interrupt functions as noInterrupt() would prevent millis() from working
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

// Time in ms when the config mode will be exited, reset when a command is entered in config mode
unsigned int configTimeoutTimer;

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
    nextMeasureTimer = 0;

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
            configTimeoutTimer = millis() + configTimeoutTimer;
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

// Green button interrupt function
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

// Red button interrupt function
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

/**
=================================================== \n
==================== BME 280 Stuff ==================== \n
===================================================
*/

ForcedClimate BMESensor = ForcedClimate();

void configureBME() {
    Wire.begin();
    BMESensor.begin();
};

/**
=================================================== \n
====================== RTC Stuff ===================== \n
===================================================
*/

int weekdayStringToInt (const String& weekday, unsigned char& weekdayNumber) {
    const char* weekdays[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};

    for (unsigned char i = 1; i <= 7; i++) {
        if (weekday == weekdays[i-1]) {
            weekdayNumber = i;
            return true;
        }
    }
    return false;
}


int monthStringToInt (const String& month, unsigned char& monthNumber) {
    const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

    for (unsigned char i = 1; i <= 12; i++) {
        if (month == months[i - 1]) {
            monthNumber = i;
            return true;
        }
    }
    return false;
}


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

/**
=================================================== \n
==================== SD Card Stuff ==================== \n
===================================================
*/

SdFat SD;
SdFile currentFile;

void configureSDCard() {
    if (!SD.begin(chipSelect, SPI_HALF_SPEED)){
        // Stop execution if SD card fails
        criticalError(SDread_error);
    }
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
        if ((currentFile.fileSize() + sizeof(dataToWrite)) < currentSystemConfiguration.FILE_MAX_SIZE) {
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

/**
=================================================== \n
=================== Light sensor Stuff ================== \n
===================================================
*/

String readLightSensor() {
    int data = analogRead(lightSensorPIN);
    return String(data);
}

/**
=================================================== \n
====================== GPS Stuff ===================== \n
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

/**
=================================================== \n
==================== Standard Mode ==================== \n
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

/**
=================================================== \n
=================== Economic Mode =================== \n
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

/**
=================================================== \n
=================== Maintenance Mode ================== \n
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


/**
=================================================== \n
================== Configuration Mode ================== \n
===================================================
*/

String promptUserInput(const String& prompt) {
    Serial.println(prompt + " : ");

    while (Serial.available() == 0) {
        // TODO check if config mode timer has expired, return if yes
    }
    return Serial.readString();
}

void configValueError(const String& command, const int& value) {
    Serial.print("Unsupported value - " + command + " : " + String(value));
}



void writeConfigToEEPROM () {
    EEPROM.put(EEPROM_configuration, currentSystemConfiguration);
}

void getConfigFromEEPROM () {
    EEPROM.get(EEPROM_configuration, currentSystemConfiguration);
}

// TODO add config systemMode
void configMode() {
    // Reset config mode timeout to 30 minutes
    configTimeoutTimer = millis() + configTimeout;

    // String to store the user command
    String command = Serial.readStringUntil('=');

    // Remove any unwanted spaces or CR & LF symbols
    command.trim();

    // Make the string upper case
    command.toUpperCase();

    // Read value after the =
    int value = Serial.parseInt();

    // Flush data remaining in Serial to prevent errors
    Serial.readString();

    // -- Luminosity Sensor --
    // Bool
    if (command == "LUMIN") {
        if (value != 1 and value != 0) {
            configValueError(command, value);
            return;
        }
        currentSystemConfiguration.ACTIVATE_LUMINOSITY_SENSOR = (value == 1);
    }

    // Unsigned short int
    else if (command == "LUMIN_LOW") {
        if (0 <= value and value >= 1023) {
            currentSystemConfiguration.LUMINOSITY_LOW_THRESHOLD = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }

    // Unsigned short int
    else if (command == "LUMIN_HIGH") {
        if (0 <= value and value >= 1023) {
            currentSystemConfiguration.LUMINOSITY_HIGH_THRESHOLD = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }

    // -- Thermometer --
    // Bool
    else if (command == "TEMP_AIR") {
        if (value != 1 and value != 0) {
            configValueError(command, value);
            return;
        }
        currentSystemConfiguration.ACTIVATE_THERMOMETER = (value == 1);
    }

    // Char
    else if (command == "MIN_TEMP_AIR") {
        if (-40 <= value and value >= 85) {
            currentSystemConfiguration.THERMOMETER_MIN_TEMPERATURE = value;
        }
        else {
            configValueError(command, value);
            return;
        }

    }

    // Char
    else if (command == "MAX_TEMP_AIR") {
        if (-40 <= value and value >= 85) {
            currentSystemConfiguration.THERMOMETER_MAX_TEMPERATURE = value;
        }
        else {
            configValueError(command, value);
            return;
        }

    }

    // -- Hygrometry --
    // Bool
    else if (command == "HYGR") {
        if (value != 1 and value != 0) {
            configValueError(command, value);
            return;
        }
        currentSystemConfiguration.ACTIVATE_HYGROMETRY_SENSOR = (value == 1);
    }

    // Char
    else if (command == "HYGR_MINT") {
        if (-40 <= value and value >= 85) {
            currentSystemConfiguration.MIN_TEMPERATURE_FOR_HYGROMETRY = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }

    // Char
    else if (command == "HYGR_MAXT") {
        if (-40 <= value and value >= 85) {
            currentSystemConfiguration.MAX_TEMPERATURE_FOR_HYGROMETRY = value;
        }
        else {
            configValueError(command, value);
            return;
        }

    }

    // -- Pressure sensor
    // Bool
    if (command == "PRESSURE") {
        if (value != 1 and value != 0) {
            configValueError(command, value);
            return;
        }
        currentSystemConfiguration.ACTIVATE_PRESSURE_SENSOR = (value == 1);

    }

    // Unsigned short int
    else if (command == "PRESSURE_MIN") {
        if (300 <= value and value >= 1100) {
            currentSystemConfiguration.MIN_VALID_PRESSURE = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }

    // Unsigned short int
    else if (command == "PRESSURE_MAX") {
        if (300 <= value and value >= 1100) {
            currentSystemConfiguration.MAX_VALID_PRESSURE = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }

    // -- RTC --
    // String -> Unsigned chars
    else if (command == "CLOCK") {
        String HHMMSS = promptUserInput("HH:MM:SS");
        unsigned char hour, minute, second;

        if (sscanf(HHMMSS.c_str(), "%s:%s:%s", &hour, &minute, &second) == 3) {
            if (hour < 0 or hour > 23) {
                configValueError("Hour", hour);
                return;
            }
            if (minute < 0 or minute > 59) {
                configValueError("Minute", minute);
                return;
            }
            if (second < 0 or second > 59) {
                configValueError("Second", second);
                return;
            }

            clock.fillByHMS(hour, minute, second);
            clock.setTime();
        }
        else {
            Serial.println("Error, use HH:MM:SS");
        }
    }

    // String -> Unsigned char:Unsigned char:int
    else if (command == "DATE") {
        String MMDDYY = promptUserInput("MM:DD:YY");
        unsigned char month, day;
        int year;

        if (sscanf(MMDDYY.c_str(), "%s:%s:%d", &month, &day, &year) == 3) {
            if (month < 1 or month > 12) {
                configValueError("Month", month);
                return;
            }
            if (day < 1 or day > 31) {
                configValueError("Day", day);
                return;
            }
            if (year < 2000 or year > 2099) {
                configValueError("Year", year);
                return;
            }

            clock.fillByHMS(month, day, year);
            clock.setTime();
        }
        else {
            Serial.println("Error, use MM:DD:YY");
            return;
        }
    }

    // String -> Unsigned char
    else if (command == "DAY") {
        String day = promptUserInput("Day");

        unsigned char weekdayNumber;

        if(weekdayStringToInt(day, weekdayNumber)) {
            clock.fillDayOfWeek(weekdayNumber);
            clock.setTime();
        }
        else {
            Serial.println("Error, invalid weekday");
        }
        return;
    }

    // -- MISC --
    // Unsigned char
    else if (command == "LOG_INTERVALL") {
        if (0 <= value and value >= 255) {
            currentSystemConfiguration.LOG_INTERVALL = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }
    // Unsigned Int
    else if (command == "FILE_MAX_SIZE") {
        if (0 <= value and value >= 65535) {
            currentSystemConfiguration.FILE_MAX_SIZE = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }

    else if (command == "RESET") {
        defaultConfig();
    }

    else if (command == "VERSION") {
        Serial.println(programVersion);
        return;
    }

    // Unsigned char
    else if (command == "TIMEOUT") {
        if (0 <= value and value >= 255) {
            currentSystemConfiguration.TIMEOUT = value;
        }
        else {
            configValueError(command, value);
            return;
        }
    }

    // If command is unknown, return to loop()
    else {
        Serial.println("Unknown command");
        return;
    }

    writeConfigToEEPROM();
}

/**
=================================================== \n
======================= Setup ======================= \n
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

    EEPROM.get(EEPROM_BOOL_programHasRunBefore, programHasRunBefore);

    if (programHasRunBefore) {
        // If this is not the first time the program is running since the arduino was flashed
        getConfigFromEEPROM();
    }
    else {
        // If this is the first time the program is running since the arduino was flashed
        defaultConfig();
        writeConfigToEEPROM();
        programHasRunBefore = true;
        EEPROM.put(EEPROM_BOOL_programHasRunBefore, programHasRunBefore);
    }


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
        if (millis() > nextMeasureTimer) {
            switch (currentMode) {
                case standard:
                    // Set time for next measure
                    nextMeasureTimer = millis() + LOG_INTERVALL;

                    // Reset dataString
                    dataString = "";

                    // Execute Mode
                    standardMode();
                    break;

                case economic:
                    // Set time for next measure
                    nextMeasureTimer = millis() + (LOG_INTERVALL * 2);

                    // Reset dataString
                    dataString = "";

                    // Execute Mode
                    economicMode();
                    break;

                case maintenance:
                    // Set time for next measure
                    nextMeasureTimer = millis() + LOG_INTERVALL;

                    // Reset dataString
                    dataString = "";

                    // Execute Mode
                    maintenanceMode();
                    break;

                case config:
                    // Execute Mode
                    if (Serial.available() != 0) {
                        configMode();
                    }
                    else if (millis() > configTimeoutTimer) {
                        switchMode(standard);
                    }
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
}

