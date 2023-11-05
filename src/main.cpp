#include <Arduino.h>

#include <SoftwareSerial.h>
#include <SdFat.h>
#include <Wire.h>
#include <forcedClimate.h>
#include <DS1307.h>
#include <ChainableLED.h>
#include <EEPROM.h>

// -- Pins --
// GPS - SoftSerial pins
#define RX 8
#define TX 9

// LED pins
#define LEDpin1 6
#define LEDpin2 7

// Light sensor pin
#define lightSensorPIN 2 // Analog pin

// Button pins
#define greenButtonPIN 2
#define redButtonPIN 3

// SD card
#define chipSelect 4

// -- MISC --
#define buttonPressTime 5000  // Time button has to be pressed for (in ms)
#define configTimeout 1800000    // Time no command has to be entered for, to exit config mode (in ms)

#define deviceID 69
#define programVersion 420


// -- EEPROM Adresses --
#define EEPROM_BOOL_programHasRunBefore 1     // Set to true if the program has been executed before, since having been written to the arduino's flash
#define EEPROM_configuration 2                // Contains the system configuration

DS1307 clock;

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
};

enum colorValue {Blue, Yellow, Orange, Red, Green, White};

RGB getColor(colorValue color) {
    struct RGB output;

    switch (color) {
        case Blue:
            output.R = 0;
            output.G = 0;
            output.B = 255;
            break;

        case Yellow:
            output.R = 225;
            output.G = 234;
            output.B = 0;
            break;

        case Orange:
            output.R = 255;
            output.G = 69;
            output.B = 0;
            break;

        case Red:
            output.R = 255;
            output.G = 0;
            output.B = 0;
            break;

        case Green:
            output.R = 0;
            output.G = 255;
            output.B = 0;
            break;

        case White:
            output.R = 255;
            output.G = 255;
            output.B = 255;
            break;
    }
    return output;
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
        setLEDcolor(RGBvalue1);

        delay(color_1_time);

        // Second color
        setLEDcolor(RGBvalue2);

        delay(color_2_time);
    }
}

/**
=================================================== \n
================== System configuration ================= \n
===================================================
*/

// Configuration struct
struct configuration {
    bool ACTIVATE_LUMINOSITY_SENSOR;                // Determines if luminosity sensor is active
    unsigned short int LUMINOSITY_LOW_THRESHOLD;    // Threshold value for luminosity sensor reading to be considered 'LOW'
    unsigned short int LUMINOSITY_HIGH_THRESHOLD;   // Threshold value for luminosity sensor reading to be considered 'HIGH'
    bool ACTIVATE_THERMOMETER;                      // Determines if thermometer is active
    int THERMOMETER_MIN_TEMPERATURE;                // Lowest thermometer value that is considered valid
    int THERMOMETER_MAX_TEMPERATURE;                // Highest thermometer value that is considered valid
    bool ACTIVATE_HYGROMETRY_SENSOR;                // Determines if hygrometry sensor is active
    int MIN_TEMPERATURE_FOR_HYGROMETRY;             // Lowest temperature at which the hygrometry sensor is still read
    int MAX_TEMPERATURE_FOR_HYGROMETRY;             // Highest temperature at which the hygrometry sensor is still read
    bool ACTIVATE_PRESSURE_SENSOR;                  // Determines if pressure sensor is active
    unsigned int MIN_VALID_PRESSURE;                // Lowest pressure value that is considered a valid reading
    unsigned int MAX_VALID_PRESSURE;                // Highest pressure value that is considered a valid reading
    unsigned char LOG_INTERVALL;                    // Intervall between readings (in minutes)
    unsigned int TIMEOUT;                           // Determiner after how much time of a sensor not responding, a Timeout is triggered
    unsigned short int FILE_MAX_SIZE;               // Maximum file size, when reached a new file is created
} currentSystemConfiguration;

void defaultConfig() {
    currentSystemConfiguration.ACTIVATE_LUMINOSITY_SENSOR = true;
    currentSystemConfiguration.LUMINOSITY_LOW_THRESHOLD = 255;
    currentSystemConfiguration.LUMINOSITY_HIGH_THRESHOLD = 768; // default : 768
    currentSystemConfiguration.ACTIVATE_THERMOMETER = true;
    currentSystemConfiguration.THERMOMETER_MIN_TEMPERATURE = -10;
    currentSystemConfiguration.THERMOMETER_MAX_TEMPERATURE = 60;
    currentSystemConfiguration.ACTIVATE_HYGROMETRY_SENSOR = true;
    currentSystemConfiguration.MIN_TEMPERATURE_FOR_HYGROMETRY = 0;
    currentSystemConfiguration.MAX_TEMPERATURE_FOR_HYGROMETRY = 50;
    currentSystemConfiguration.ACTIVATE_PRESSURE_SENSOR = true;
    currentSystemConfiguration.MIN_VALID_PRESSURE = 850;
    currentSystemConfiguration.MAX_VALID_PRESSURE = 1080;
    currentSystemConfiguration.LOG_INTERVALL = 2;
    currentSystemConfiguration.TIMEOUT = 30000;
    currentSystemConfiguration.FILE_MAX_SIZE = 4096;
}

// Set to false if this is the first time program is being executed since having been flashed onto the arduino
// this is being used to determine whether the RTC needs to be set to the correct time and if there is already a
// config that needs to be loaded from EEPROM
bool programHasRunBefore = false;

// -- EEPROM Config --
// Wite currentSystemConfiguration to EEPROM
void writeConfigToEEPROM () {
    EEPROM.put(EEPROM_configuration, currentSystemConfiguration);
}

// Fetch configuration from EEPROM, write to currentSystemConfiguration
void getConfigFromEEPROM () {
    EEPROM.get(EEPROM_configuration, currentSystemConfiguration);
}
/**
=================================================== \n
===================== Error handling =================== \n
===================================================
*/

// -- Button Pressed bools --
bool greenButtonPressed = false;
bool redButtonPressed = false;

// -- noInterrupt bool --
bool noInterrupt = false;

// -- Enum containing all supported error states --
enum errorCase {RTC_error, GPS_error, Sensor_error, Data_error, SDfull_error, SDread_error};

// -- System error handling --
void criticalError(errorCase error) {
    // Block both interrupt functions as noInterrupt() would prevent millis() from working
    noInterrupt = true;

    switch (error) {
        case RTC_error:
            blinkLED(getColor(Red), getColor(Blue), 1);

        case GPS_error:
            blinkLED(getColor(Red), getColor(Yellow), 1);

        case Sensor_error:
            blinkLED(getColor(Red), getColor(Green), 1);

        case Data_error:
            blinkLED(getColor(Red), getColor(Green), 2);

        case SDfull_error:
            blinkLED(getColor(Red), getColor(White), 1);

        case SDread_error:
            blinkLED(getColor(Red), getColor(White), 2);
    }
}

/**
=================================================== \n
===================== systemModes =================== \n
===================================================
*/
// -- Enum containing all system modes --
// noMode is only used in nextMode
enum systemMode {standard, economic, maintenance, config, noMode};

// -- Toggle GPS --
// Determines if the GPS is read during the next execution of economic mode
bool readGPSnextExec;

// -- Measure timing --
// Time when economic / standard / maintenance systemMode will be executed next
unsigned long nextMeasureTimer = 0;

// -- Switch systemMode --
// Current systemMode variable
// DO NOT CHANGE THIS OUTSIDE THE 'switchMode()' FUNCTION OR STUFF WILL BREAK
systemMode currentMode = standard;

// Next systemMode, changed by interrupts --
systemMode nextMode = noMode;

// Used to switch back from maintenance systemMode, contains either standard or economic
systemMode lastModeBeforeMaintenance;

// Contains the time in ms, when the system mode is changed,
// used after interrupts and in config mode
unsigned long switchModeTimer = 0;

// systemMode switching function
void switchMode(systemMode newMode){
    // Reset nextMode, used to trigger 'switchMode()' in 'loop()'
    nextMode = noMode;
    switchModeTimer = 0;

    // Sets the time threshold for the next measure back to 0
    nextMeasureTimer = 0;

    // Makes sure the GPS is read during the next reading
    readGPSnextExec = true;

    switch (newMode) {
        // Standard
        case standard :
            setLEDcolor(getColor(Green));
            lastModeBeforeMaintenance = standard;
            break;

        // Economic
        case economic :
            setLEDcolor(getColor(Blue));
            lastModeBeforeMaintenance = economic;
            break;

        // Maintenance
        case maintenance:
            setLEDcolor(getColor(Orange));
            break;

        // Config
        case config:
            switchModeTimer = millis() + configTimeout;
            setLEDcolor(getColor(Yellow));
            break;

        case noMode:
            //noMode is not allowed as a system mode, returning to previous mode
            return;
    }
    currentMode = newMode;
}

// -- Interrupts --
// Green button interrupt function
void greenButtonInterrupt() {
    if (noInterrupt) {
        return;
    }
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
    //Serial.println("GBI "+ String(greenButtonPressed));

    if (greenButtonPressed) {
        if (currentMode == standard) {
            nextMode = economic;
        }
        if (currentMode == economic) {
            nextMode = standard;
        }
        switchModeTimer = millis() + buttonPressTime;
    }
    else {
        nextMode = noMode;
        switchModeTimer = 0;
    }
    interrupts();
}

// Red button interrupt function
void redButtonInterrupt() {
    //Print Red Button Interrupt
    //Serial.println("RBI "+ String(redButtonPressed));

    if (noInterrupt) {
        return;
    }
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

    if (redButtonPressed) {
        if (currentMode == standard or currentMode == economic) {
            nextMode = maintenance;
        }
        if (currentMode == maintenance) {
            nextMode = lastModeBeforeMaintenance;
        }
        switchModeTimer = millis() + buttonPressTime;
    }
    else {
        nextMode = noMode;
        switchModeTimer = 0;
    }
    interrupts();
}

// -- Make a string for assembling the data to log --
// 73 characters are reserved for this String in 'Setup()'
String dataString;

// Separator placed between RTC, GPS and sensor data in 'dataString'
String valueSeparator = " ; ";

/**
=================================================== \n
==================== SD Card Stuff ==================== \n
===================================================
*/

SdFat32 SD;
SdFile currentFile;

// 16 characters are reserved for this String in 'Setup()'
String fileName;

// Current LOG file revision number
unsigned char revision = 1;

bool fileOpen = false;

// Selects a file to write to, renames the current LOG file if it is full and creates a new one
void selectFile () {
    if (!fileOpen) {
        if (!currentFile.open("000000_0.LOG", O_RDWR | O_CREAT | O_AT_END)) {
            criticalError(SDread_error);
        }
        fileOpen = true;
    }

    // If projected filesize < FILE_MAX_SIZE bytes
    if ((currentFile.fileSize() + 125 < currentSystemConfiguration.FILE_MAX_SIZE)) {
        return;
    }

        // If projected filesize > FILE_MAX_SIZE bytes
    else {
        currentFile.close();
        while(true) {
            fileName = clock.year;
            fileName += clock.month;
            fileName += clock.dayOfMonth;
            fileName += "_";
            fileName += revision;
            fileName += ".LOG";

            // Check if file with that revision number already exists
            if (SD.exists(fileName.c_str())){
                revision++;
            }
                // If it doesn't exist, rename the current revision 0 file to it
            else {
                if(!SD.rename("000000_0.LOG", fileName)) {
                    criticalError(SDread_error);
                }

                if (!currentFile.open("000000_0.LOG", O_RDWR | O_CREAT | O_AT_END)) {
                    criticalError(SDread_error);
                }
                return;
            }
        }
    }
}

void writeTocurrentFile(const String& dataToWrite, bool newLine) {
    if(!(currentMode == standard || currentMode == economic)) {
        if (newLine) {
            Serial.println(dataToWrite);
        }
        else {
            Serial.print(dataToWrite);
        }

        return;
    }

    if (newLine) {
        currentFile.println(dataToWrite);
        Serial.println(dataToWrite);

        // Print file name
        Serial.print("R : ");
        Serial.println(revision);

        // Print file size
        Serial.print("S : ");
        Serial.print(currentFile.fileSize());
        Serial.println(" B");
        Serial.println("");

    }

    else {
        currentFile.print(dataToWrite);
        Serial.print(dataToWrite);
    }
}


/**
=================================================== \n
==================== BME 280 Stuff ==================== \n
===================================================
*/

ForcedClimate BMESensor = ForcedClimate();

bool inRange(const float& value, const int& min, const int& max) {
    if (value < min) {
        return false;
    }
    else if (value > max) {
        return false;
    }
    return true;
}


void readBMEdata(String& output) {
    //-- BME280 Readings --
    BMESensor.takeForcedMeasurement();

    //Temperature
    float temperature = BMESensor.getTemperatureCelcius();

    if (currentSystemConfiguration.ACTIVATE_THERMOMETER) {
        if (inRange(temperature, currentSystemConfiguration.THERMOMETER_MIN_TEMPERATURE, currentSystemConfiguration.THERMOMETER_MAX_TEMPERATURE)) {
            output = String(temperature);

            output += valueSeparator;

            writeTocurrentFile(output, false);
        }
    }

    //Humidity
    if (currentSystemConfiguration.ACTIVATE_HYGROMETRY_SENSOR) {
        if (inRange(temperature, currentSystemConfiguration.MIN_TEMPERATURE_FOR_HYGROMETRY, currentSystemConfiguration.MAX_TEMPERATURE_FOR_HYGROMETRY)) {
            output = String(BMESensor.getRelativeHumidity());

            output += valueSeparator;

            writeTocurrentFile(output, false);
        }
    }

    //Pressure
    float pressure = BMESensor.getPressure();

    if (currentSystemConfiguration.ACTIVATE_PRESSURE_SENSOR) {
        if (inRange(pressure, currentSystemConfiguration.MIN_VALID_PRESSURE, currentSystemConfiguration.MAX_VALID_PRESSURE)) {
            output = String(pressure);

            output += valueSeparator;

            writeTocurrentFile(output, true);
        }
    }
}


/**
=================================================== \n
====================== RTC Stuff ===================== \n
===================================================
*/


// -- Adds the time to a String --
void readTime(String& output)
{
    clock.getTime();
    output = clock.hour;
    output += ":";
    output += clock.minute;
    output += ":";
    output += clock.second;
    output += "-";
    output += clock.month;
    output += "/";
    output += clock.dayOfMonth;
    output += "/";
    output += (clock.year + 2000);
    output += valueSeparator;

    writeTocurrentFile(output, false);
}


/**
=================================================== \n
=================== Light sensor Stuff ================== \n
===================================================
*/


void readLightSensorData(String& output) {
    //Return if luminosity sensor is disabled
    if (!currentSystemConfiguration.ACTIVATE_LUMINOSITY_SENSOR) {
        return;
    }

    unsigned int data = analogRead(lightSensorPIN);
    if (data < currentSystemConfiguration.LUMINOSITY_LOW_THRESHOLD) {
        output = "LOW";
    }
    else if ((data < currentSystemConfiguration.LUMINOSITY_HIGH_THRESHOLD)) {
        output = "AVG";
    }
    else {
        output = "HIGH";
    }
    output += valueSeparator;

    writeTocurrentFile(output, false);
}


/**
=================================================== \n
====================== GPS Stuff ===================== \n
===================================================
*/

// Open SoftSerial for GPS
SoftwareSerial SoftSerial(RX, TX);

bool timeout_GPS = false;

void readGPS(String& output) {
    if (SoftSerial.available()) // Check if soft serial is open
    {
        unsigned long timer = millis() + currentSystemConfiguration.TIMEOUT;

        while(millis() < timer) {
            output = SoftSerial.readStringUntil('\n');

            output.trim();

            if (output.startsWith("$GPGGA",0)){
                timeout_GPS = false;
                output+=valueSeparator;
                writeTocurrentFile(output, false);
                return;
            }
        }
        if (timeout_GPS) {
            criticalError(GPS_error);
        }

        timeout_GPS = true;
        output = "N/A";
        output+=valueSeparator;
        writeTocurrentFile(output, false);
        return;
    }
    else {
        criticalError(GPS_error);
    }
}

/**
=================================================== \n
==================== Perform reading =================== \n
===================================================
*/

void performReading() {
    selectFile();

    // -- GPS reading --
    // Only called every second execution if in economic mode
    if (readGPSnextExec) {
        readGPS(dataString);
    }

    // Toggle whether the GPS is read next execution if in economic mode
    if (currentMode == economic) {
        readGPSnextExec = !readGPSnextExec;
    }

    // -- RTC Clock reading --
    readTime(dataString);

    // -- Luminosity captor reading --
    readLightSensorData(dataString);

    //-- BME280 Readings --
    readBMEdata(dataString);
}


/**
=================================================== \n
================== Configuration Mode ================== \n
===================================================
*/

// Set to true if the user has entered an incorrect value in config mode
bool valueError = false;

// Used to send error messages when input values are not allowed by the command
void configValueError(const String& command, const int& value) {
    Serial.print("Err " + command + " : ");
    Serial.println(value);
    valueError = true;
}

// Array of functions containing one function per supported config command
void (*configFunctions[])(const String& command) = {
        // LUMIN
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (value != 1 and value != 0) {
                configValueError(command, value);
                return;
            }

            // Write changes to config
            currentSystemConfiguration.ACTIVATE_LUMINOSITY_SENSOR = (value == 1);
        },

        // LUMIN_LOW
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (0 <= value and value >= 1023) {
                // Write changes to config
                currentSystemConfiguration.LUMINOSITY_LOW_THRESHOLD = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // LUMIN_HIGH
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (0 <= value and value >= 1023) {
                // Write changes to config
                currentSystemConfiguration.LUMINOSITY_HIGH_THRESHOLD = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // TEMP_AIR
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (value != 1 and value != 0) {
                configValueError(command, value);
                return;
            }

            // Write changes to config
            currentSystemConfiguration.ACTIVATE_THERMOMETER = (value == 1);

        },

        // MIN_TEMP_AIR
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (-40 <= value and value >= 85) {
                // Write changes to config
                currentSystemConfiguration.THERMOMETER_MIN_TEMPERATURE = value;
            }
            else {
                configValueError(command, value);
            }

        },

        // MAX_TEMP_AIR
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (-40 <= value and value >= 85) {
                // Write changes to config
                currentSystemConfiguration.THERMOMETER_MAX_TEMPERATURE = value;
            }
            else {
                configValueError(command, value);
            }

        },

        // HYGR
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (value != 1 and value != 0) {
                configValueError(command, value);
                return;
            }
            // Write changes to config
            currentSystemConfiguration.ACTIVATE_HYGROMETRY_SENSOR = (value == 1);
        },

        // HYGR_MINT
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (-40 <= value and value >= 85) {
                // Write changes to config
                currentSystemConfiguration.MIN_TEMPERATURE_FOR_HYGROMETRY = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // HYGR_MAXT
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (-40 <= value and value >= 85) {
                // Write changes to config
                currentSystemConfiguration.MAX_TEMPERATURE_FOR_HYGROMETRY = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // PRESSURE
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (value != 1 and value != 0) {
                configValueError(command, value);
                return;
            }

            // Write changes to config
            currentSystemConfiguration.ACTIVATE_PRESSURE_SENSOR = (value == 1);

        },

        // PRESSURE_MIN
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (300 <= value and value >= 1100) {
                // Write changes to config
                currentSystemConfiguration.MIN_VALID_PRESSURE = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // PRESSURE_MAX
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (300 <= value and value >= 1100) {
                // Write changes to config
                currentSystemConfiguration.MAX_VALID_PRESSURE = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // LOG_INTERVALL
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (0 < value and value >= 255) {
                // Write changes to config
                currentSystemConfiguration.LOG_INTERVALL = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // FILE_MAX_SIZE
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (100 < value and value >= 65535) {
                currentSystemConfiguration.FILE_MAX_SIZE = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // RESET
        [](const String& command) -> void
        {
            defaultConfig();
        },

        // TIMEOUT
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (0 <= value and value >= 255) {
                currentSystemConfiguration.TIMEOUT = value;
            }
            else {
                configValueError(command, value);
            }
        },

        // CLOCK
        [](const String& command) -> void
        {
            String HHMMSS = Serial.readString();

            unsigned char hour, minute, second;

            if (sscanf(HHMMSS.c_str(), "%s:%s:%s", &hour, &minute, &second) == 3) {
                if (hour < 0 or hour > 23) {
                    configValueError("hr", hour);
                    return;
                }
                if (minute < 0 or minute > 59) {
                    configValueError("min", minute);
                    return;
                }
                if (second < 0 or second > 59) {
                    configValueError("sec", second);
                    return;
                }

                // Write values to RTC
                clock.fillByHMS(hour, minute, second);
                clock.setTime();
            }
            else {
                Serial.println("err");
            }
        },

        // DATE
        [](const String& command) -> void
        {
            String MMDDYY = Serial.readString();
            unsigned char month, day;
            int year;

            if (sscanf(MMDDYY.c_str(), "%s:%s:%d", &month, &day, &year) == 3) {
                if (month < 1 or month > 12) {
                    configValueError("mth", month);
                    return;
                }
                if (day < 1 or day > 31) {
                    configValueError("dy", day);
                    return;
                }
                if (year < 2000 or year > 2099) {
                    configValueError("yr", year);
                    return;
                }

                // Write values to RTC
                clock.fillByHMS(month, day, year);
                clock.setTime();
            }
            else {
                Serial.println("err");
            }
        },

        // DAY
        [](const String& command) -> void
        {
            // Parse value from Serial
            int value = Serial.parseInt();

            // Command Logic
            if (1 <= value and value >= 7) {
                // Write value to RTC
                clock.fillDayOfWeek(value);
            }
            else {
                configValueError(command, value);
            }
            return;
        },

        // VERSION
        [](const String& command) -> void
        {
            Serial.print(programVersion);
            Serial.print(", ID ");
            Serial.println(deviceID);
        }
};


// This mode is called by pressing the red button for 5s at the start of the programs execution
void configMode() {
    // Reset config mode timeout to 30 minutes
    switchModeTimer = millis() + configTimeout;

    // Array of supported commands
    const char* configCommands[] = {"LUMIN", "LUMIN_LOW", "LUMIN_HIGH", "TEMP_AIR", "MIN_TEMP_AIR",
                                    "MAX_TEMP_AIR", "HYGR", "HYGR_MINT", "HYGR_MAXT", "PRESSURE",
                                    "PRESSURE_MIN", "PRESSURE_MAX", "LOG_INTERVALL", "FILE_MAX_SIZE",
                                    "RESET", "TIMEOUT", "CLOCK", "DATE", "DAY", "VERSION"};

    // String to store the user's input
    String command = Serial.readStringUntil('=');

    // Remove any unwanted spaces or CR & LF symbols
    command.trim();

    // Make the String upper case
    command.toUpperCase();

    // -- Interpretation of the users input --
    int i = 0;
    bool loop = true;

    // Attempting to match the input to a supported configuration command
    while(loop) {
        if (i == 20) {
            // If command is unknown, return to loop()
            Serial.println("Unknown cmd");
            return;
        }

        // Find command in list of supported commands
        if (command == configCommands[i]) {
            // Call function corresponding to command
            configFunctions[i](command);

            loop = false;
        }
        else {
            i++;
        }
    }

    // -- Flush data remaining in Serial to prevent errors --
    Serial.readString();

    // Return if an invalid value was entered
    if (valueError) {
        valueError = false;
        return;
    }

    Serial.println(command + " executed");

    // -- Write config to EEPROM --
    // Unfortunately the entire config gets written again each time, which hits EEPROM pretty hard

    // Theoretically I could calculate the exact location of each element of my configuration struct in EEPROM and
    // only change that, but it's too complicated and memory intensive for this project

    // Only config commands 0 - 15 require writing to EEPROM
    if (i < 16) {
        writeConfigToEEPROM();
    }
}

/**
=================================================== \n
======================= Setup ======================= \n
===================================================
*/

void setup() {
    // -- Reserve space for Strings to avoid fragmentation --
    // Contains sensor data
    dataString.reserve(45);

    // String for file name
    fileName.reserve(13);

    // -- Configure LEDs --
    leds.init();

    // -- Configure buttons --
    pinMode(greenButtonPIN, INPUT_PULLUP);
    pinMode(redButtonPIN, INPUT_PULLUP);

    // -- Open serial communications and wait for port to open --
    Serial.begin(9600);

    // -- Check whether the program has run before --
    EEPROM.get(EEPROM_BOOL_programHasRunBefore, programHasRunBefore);

    // programHasRunBefore = false;

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

    // -- Check if RED button is pressed for 5 sec, go to config systemMode if yes --
    // Reminder : the button is 'LOW' active
    if (!digitalRead(redButtonPIN)) {
        unsigned long counter = millis() + buttonPressTime;
        bool g = true;
        while (g) {
            if (digitalRead(redButtonPIN)) {
                g = false;
            }
            else if (millis() > counter) {
                noInterrupt = true;
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
    // configureRTC(); removed to save ram

    // -- Configure BME --
    Wire.begin();
    BMESensor.begin();

    // -- Configure GPS --
    // Open SoftwareSerial for GPS
    SoftSerial.begin(9600);

    // Wait until SoftSerial is open
    while(!SoftSerial.available()) {
        ;
    }

    // -- Configure SD Card --
    if (!SD.begin(chipSelect, SPI_HALF_SPEED)){
        // Stop execution if SD card fails
        criticalError(SDread_error);
    }

    // -- Setup interrupts for buttons --
    // This is done last to prevent interrupts during 'setup()'
    attachInterrupt(digitalPinToInterrupt(greenButtonPIN), greenButtonInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(redButtonPIN), redButtonInterrupt, CHANGE);

    Serial.println("->");
}

void loop() {
    if (nextMode == noMode) {
        switch (currentMode) {
            case standard:
                if (millis() > nextMeasureTimer) {
                    // Set time for next measure
                    nextMeasureTimer = millis() + currentSystemConfiguration.LOG_INTERVALL * 1000;

                    // Perform sensor reading
                    performReading();
                }
                break;

            case economic:
                if (millis() > nextMeasureTimer) {
                    // Set time for next measure
                    nextMeasureTimer = millis() + (currentSystemConfiguration.LOG_INTERVALL * 2000);

                    // Perform sensor reading
                    performReading();
                }
                break;

            case maintenance:
                // Close the current file if it is still open
                if (fileOpen) {
                    currentFile.close();
                    fileOpen = false;
                }

                // Set time for next measure
                nextMeasureTimer = millis() + currentSystemConfiguration.LOG_INTERVALL * 1000;

                // Perform sensor reading
                performReading();
                break;

            case config:
                // Switch modes if 'switchModeTimer' is exceeded

                // Serial.println(switchModeTimer - millis());

                if (millis() > switchModeTimer) {
                    // Allow interrupts
                    noInterrupt = false;

                    // Switch mode
                    switchMode(standard);
                }
                // Go into config mode if there is something in Serial
                else if (Serial.available() > 0) {
                    configMode();
                }
                break;

            case noMode:
                // 'noMode' is not allowed as a system mode, switchMode() will not allow switching to it
                break;
        }

    }
    else if (millis() > switchModeTimer) {
        greenButtonPressed = false;
        redButtonPressed = false;
        switchMode(nextMode);
    }
}