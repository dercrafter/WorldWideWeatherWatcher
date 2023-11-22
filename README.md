# WorldWideWeatherWatcher project


## Introduction
This project is part of the embedded systems block in my second year of computer science studies.

The aim of this project is to create a device for measuring the parameters influencing the formation of cyclones and other natural disasters around the world.

To achieve this, our device is equipped with various sensors to measure brightness, temperature, pressure and humidity and to determine the device's geographical position. What's more, it's equipped with an SD reader to record this information.

The microcontroller used for this project is an Arduino Uno R3 with 2kB of SRAM, 32 kB of flash memory and 1kB of EEPROM, which led to numerous difficulties during the design phase du to the small amount of SRAM and Flash memory available. 
I would recommend to anyone attempting to make use of our code to upgrade to an Arduino Uno R4 which has a much more comfortable 32kB of RAM and 256kB of flash memory.

## Parts list
#### Arduino Uno R3 - Microcontroller
The Arduino microcontroller is the brain of the device, with 1 kB of EEPROM, 2 kB of SRAM and 32 kB of FLASH memory.

#### Grove SD Card Shield v4.3 - SD Reader
Connected directly to the Arduino pins, the connectors on the SD Card Shield are 5V. 
The SD reader occupies pin D4 of the Arduino.

#### Grove Base Shield v2.1
Connected directly to the Arduino pins, set to 3.3V.

#### Grove RTC v1.2 - Clock
Connected to the SD Card Shield's I2C port.

#### Grove GPS v1.2 - GPS
Connected to port D8 of the Grove Base Shield, and therefore uses pins D8 and D9 of the Arduino.
SoftwareSerial is connected to these pins to segregate the data sent by the GPS from the user-accessible serial monitor.

#### Grove Chainable RGB LED v2.0 - RGB LED
Connected to port D6 of the Grove Base Shield, it uses pins D6 and D7 of the Arduino.

#### Grove Dual Button v1.2 - Buttons
Connected to port D2 of the Grove Base Shield and therefore uses pins D2 and D3 of the Arduino.
The buttons are LOW active (their default state is HIGH, they switch to LOW when pressed) and require a PULLUP resistor to avoid having a floating line which can incorrectly send the signal to the Arduino that a button has been pressed.

## Used libraries
#### SoftwareSerial
This library is required to create a software serial for the GPS.

#### SdFat
This library enables the Arduino to record the device's measurements to the SD card.
I chose this library because it has more features and is faster than the official Arduino SD library.
The file system used is FAT32, since FAT16 is limited to 2 GB partitions, whereas the provided SD card had a capacity of 8 GB.
Furthermore, FAT32 is more modern and therefore offers greater compatibility with a large number of devices, making it easier to extract data from the device.

#### Wire
This library is needed to communicate with our I2C-connected sensors.

#### forcedClimate
This library enables the Arduino to read measurement data from the BME280 sensor.
It has the advantage of being power and memory efficient.

#### DS1307
This library lets the Arduino interact with our RTC clock.

#### ChainableLED
This library lets the Arduino change the color of our RGB LED.

#### EEPROM
This library allows the Arduino to write to and read from the Arduino's EEPROM.
