/*
  Flight code intended for reading and logging data from HMR2300 as "baseline" magnetometer data.
  Designed to run on a Arduino MKR WAN 1310, with on-board LoRa radio.
  Expects a HMR2300 magnetometer on Serial1, at 9600 bps.
  Originally written for discrete 1306 OLED display and SD card writer.
  Modified to work with MKR IoT Carrier board, too.
  Expects a OLED screen on I2C.
  Expects an SD card reader on SPI with chipselect on pin 4.
*/

// Keep VERSION an integer
// #define VERSION 1
// Version 2 adds new formula for altitude from pressure
#define VERSION 2

// define USE_IOT_CARRIER if this code is to be deployed on a MKR 1310 that's riding on an IoT Carrier board.
// Do not define IOT_CARRIER if this code is to use discrete OLED display and SD card writer.

// #define USE_IOT_CARRIER

// Define exactly ONE, either USE_8 or USE_4, but not both
#define USE_8_LINE_OLED_DISPLAY
// #define USE_4_LINE_OLED_DISPLAY


#ifdef USE_IOT_CARRIER
  /* Arduino_MKRIoTCarrier.h also includes:
   *  Arduino.h, Wire.h, Arduino_PMIC.h, Arduino_APDS9960.h (ambient light sensor), 
   *  Arduino_LPS22HB.h (pressure sensor), Arduino_LSM6DS3.h (IMU),
   *  Arduino_HTS221.h (environmental sensor)
   *  Relays, Buzzer, Qtouch,
   *  SD.h (SD card), 
   *  Adafruit_GFX.h (core graphics), Adafruit_ST7735.h, Adafruit_ST7789.h
   *  SPI.h
   *  Adafruit_DotStar.h
   *  
   *  And it defines:
   *  DotStar DATAPIN 5, CLOCKPIN 4
   *  RELAY_1 14 // note that 13 and 14 are also the 1310's TX and RX lines for serial port, thus conflict
   *  RELAY_2 13
   *  BUZZER 7
   *  GROVE_AN1 A5
   *  GROVE_AN2 A6
   *  SD_CS 0
   *  INT 6 // every sensor interrupt pin, PULL-UP
   *  LED_CKI 4
   *  LED_SDI 5
   *  TFT_CS 2
   *  TFT_RST -1
   *  TFT_DC  1
   *  TFT_BACKLIGHT 3
   */
  #include <Arduino_MKRIoTCarrier.h>
  MKRIoTCarrier carrier;
  #define DISPLAY_WIDTH_PIXELS  240 // IoT carrier display width, in pixels
  #define DISPLAY_HEIGHT_PIXELS 240 // IoT carrier display height, in pixels
#else
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #include "Adafruit_MPRLS.h"
  #include <SPI.h>
  #include <Wire.h>
  #include <SD.h>
  #define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
  int carrier = 0; // will be ignored, but compiler needs something
  #ifdef USE_8_LINE_OLED_DISPLAY
    #define DISPLAY_WIDTH_PIXELS 128 // OLED display width, in pixels
    #define DISPLAY_HEIGHT_PIXELS 64 // OLED display height, in pixels
    #define OLED_DISPLAY_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32 - Actually, the 0x3C address seems to work for both my 32 and 64-line displays
    #define DISPLAY_ROWS 8
  #else
    #define DISPLAY_WIDTH_PIXELS 128 // OLED display width, in pixels
    #define DISPLAY_HEIGHT_PIXELS 32 // OLED display height, in pixels
    #define OLED_DISPLAY_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
    #define DISPLAY_ROWS 4
  #endif
  #define DISPLAY_COLS 21
#endif

#include <LoRa.h>
#include <Regexp.h> // Library by Nick Gammon for regular expressions
#include <avr/dtostrf.h> // for formatting floating point numbers

#define MS_PER_SECOND 1000
#define MS_PER_MINUTE (MS_PER_SECOND * 60)
#define MS_PER_HOUR (MS_PER_MINUTE * 60)
#define CONTINUOUS_SECS (1 * MS_PER_MINUTE)


/* added for display above */
#define OLED_DISPLAY_TEXT_SIZE 1
#define DISPLAY_UPDATE_INTERVAL_MS 200

// assign the chip select line for the SD card on pin 4
#define SD_SPI_CHIPSELECT 4

#define LORA_FREQ 915E6

// ESC is sent to the HMR2300 in order to stop the unit from sending data continuously
#define ESC 0x1b

// HMR is on the 2nd UART accessible via pins D14 (TX) and D13 (RX) on the MKR 1310
#define HMR Serial1

// How big the Serial1 receive buffer is
#define S1_RECEIVE_BUF_LEN 1000
#define SERIAL_INIT_TIMEOUT_MS 10000

#define LORA_RX_BUF_LEN 2000
char loRaRxBuf[LORA_RX_BUF_LEN];

// In theory 34.3 samples/second is max for 280 bits at 9600 bps
// In practice, about 31 samples/sec is the best throughput I've seen (even when asking for 40), 
// and that's when writing to the SD card is disabled and serial monitor output is minimized.
// With SD writing every sample independently, about 25 is max throughput
// 20 samples per second seems solid, with other stuff going on (within reason)
// With LOG_TO_SD true, 30 samples/sec is a disaster
// 25 samp/sec looks sustainable with other stuff (LOG_TO_SD true) going on (within reason)
// Nah, 20 minutes test at 25 had 5 bad sample lengths, thruput of 24 msg/sec
#define SAMPLES_PER_SECOND 10
#define LOG_TO_SD true
#define LOG_FILE_NAME_LEN 20
#define LOG_BUF_LEN 256
#define TMP_BUF_LEN 256
#define PRINT_BUF_LEN 256
#define LED_BLINK_INTERVAL 200

#ifdef USE_IOT_CARRIER
  int display = 0;
#else
  // Adafruit_SSD1306 display(DISPLAY_WIDTH_PIXELS, DISPLAY_HEIGHT_PIXELS, &Wire, OLED_RESET);
  Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);
#endif

// Report HMR data every 10 seconds
#define LORA_TRANSMIT_INTERVAL_MS 10000
int n = 0;

// Loop code runs as a state machine
// state 0 = warming up
// state 1 = command sent
// state 2 = just do single-shot on request
int hmrState = 0;
int lastHmrState = -1;
long nextHmrStateTransition = -1;
long nextLoRaTransmitMillis = 0;

// Data read from MKR ENV sensor, but not using because it seems to conflict with some other board (SD Proto shield?)
float temperature = 0.0, humidity = 0.0, pressure = 0.0;
int loopCount = 0;

char tmpBuf[TMP_BUF_LEN];
char receiveBuf[S1_RECEIVE_BUF_LEN]; // for assembling HMR data from serial port
char printBuf[PRINT_BUF_LEN];     // for sprintf() then Serial.println() output

int receiveBufNextChar = 0;  // character position of next char to go in
char lastReceiveBuf[S1_RECEIVE_BUF_LEN]; // copy here for printing "later"
char logFileName[LOG_FILE_NAME_LEN];
char logBuf[LOG_BUF_LEN];
int loggedPacketCount = 0;
char relayStatus[32]; // only for MKR IoT Carrier

// #define X_BUF_LEN 32
// char xbuf[X_BUF_LEN];
// char ybuf[X_BUF_LEN];
// char zbuf[X_BUF_LEN];

// x, y, and z values (and min/max values) are parsed from the Serial1 (HMR) input buffer
int x = 0; 
int minx = 0;  // observed -32768 (with a magnet)
int maxx = 0;  // observed 32608 (with a magnet)
int y = 0;
int miny = 0;
int maxy = 0;
int z = 0;
int minz = 0;
int maxz = 0;

Adafruit_MPRLS presSensor;
// Altitude at standard pressure should be 0 (sea level)
// 1000 hPa = 110.8 m = 363.6 ft (per weather.gov online calculator)
// 900 hPa = 988.1 m = 3241.8 ft
// 500 hPa = 5572.1 m = 18,281.2 ft
// 100 hPa = 15790.5 m = 51806 ft
// 10 hPa = 25907.5 m = 84998.2 ft
// 1 hPa = 32435.3 m = 106414.9 ft

#define STANDARD_PRESSURE_HPA 1013.25
float hPa = STANDARD_PRESSURE_HPA;
float last_hPa = hPa;
int altitudeMeters = 0;
int last_altitudeMeters = 0;
#define PRES_SENSOR_READING_INTERVAL_MS 3000
long nextPresSensorReading = 0;

boolean waitingForOk = false;
long waitUntilMs = 0L;
int receiveCount = 0;

long timerStart = 0;
long timerStop = 0;
long lastMillis = 0;
long nextUpdateDisplay = 0; // every time the display is updated, update this to some time in the future

// Set up boolean indicators of whether each subsystem was successfully initialized
boolean goodSerial = false;
boolean goodDisplay = false;
boolean goodHMR = false;
boolean goodSD = false;
boolean goodLoRa = false;
boolean goodPresSensor = false;

// count how many HMR2300 samples were of the expected length, vs. not
int goodSampleLength = 0;
int badSampleLength = 0;

void setup() {
  goodSerial = setupSerial();
  goodDisplay = setupDisplay(5);
  goodHMR = setupHMR();
  goodSD = setupSD();
  goodLoRa = setupLoRa();
  if (goodLoRa) {
    snprintf(tmpBuf, TMP_BUF_LEN, "FlyBoy v%d is on the air!", VERSION);
    loRaTransmit(tmpBuf);
    nextLoRaTransmitMillis = millis() + LORA_TRANSMIT_INTERVAL_MS;
  }
  goodPresSensor = setupPresSensor();
  if (goodPresSensor) {
    Serial.println("MPRLS pressure sensor initialized successfully.");
    nextPresSensorReading = millis() + PRES_SENSOR_READING_INTERVAL_MS;
  } else {
    Serial.println("Failed to initialize the MPRLS pressure sensor.");
  }
  setupLED();
  strcpy(relayStatus, "?");
  if (goodHMR) {
    Serial.println("Serial1 (HMR) ready.");
  } else {
    Serial.println("Serial1 (HMR) not ready.");
  }
  Serial.print("hmrState = ");
  Serial.println(hmrState);
  if (goodSD) {
    Serial.println("goodSD is true");
  } else {
    Serial.println("goodSD is false");
  }
  if (selfTest()) {
    Serial.print("selfTest() has passed");
  }
}


boolean setupSerial() {
  // This sets up the serial monitor for output to your computer while writing and debugging the program
  boolean result = false;
  // initialize serial communications and wait for port to open:
  Serial.begin(19200); // USB and serial monitor
  long start = millis();
  long elapsed = 0L;
  while (!Serial && elapsed < SERIAL_INIT_TIMEOUT_MS) {
    // wait up to 10 seconds for serial port to connect. Needed for native USB port only.
    delay(50);
    elapsed = millis() - start;
  }
  if (Serial) {
    result = true;
  }
  return result;
}


boolean setupDisplay(int delaySecs) {
  boolean result = false;
  #ifdef USE_IOT_CARRIER
    CARRIER_CASE = false;
    carrier.begin();
    result = true;
  #else
    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_DISPLAY_ADDRESS)) {
      printlnStr("SSD1306 allocation failed");
    } else {
      result = true;
      // Show initial display buffer contents on the screen --
      // the library initializes this with an Adafruit splash screen.
      // Clear the buffer
      display.clearDisplay();
      display.setTextSize(OLED_DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
      display.setTextSize(0.5);      // Normal 1:1 pixel scale
      display.setTextColor(SSD1306_WHITE); // Draw white text
      display.setCursor(0, 0);     // Start at top-left corner
      display.cp437(true);         // Use full 256 char 'Code Page 437' font
      //                 123456789012345678901
      snprintf(tmpBuf, TMP_BUF_LEN, "FlyBoy v%d\nLogs HMR2300 data\nto SD card\n", VERSION);
      display.println(tmpBuf);
      display.println();
      display.print(delaySecs); display.println(" sec delay...");
      display.display();
    }
    delaySecs--;
    while (delaySecs >= 0) {
      delay(1000);
      display.print(" ");
      display.print(delaySecs);
      display.display();
      delaySecs--;
    }
  #endif
  return result;
}


boolean setupHMR() {
  // Set up HMR2300 on Serial1
  boolean result = false;
  HMR.begin(9600); // HMR2300, via DF0077 level converter, on Serial1 (the UART pins)
  long start = millis();
  long elapsed = 0L;
  while (!HMR && elapsed < SERIAL_INIT_TIMEOUT_MS) {
    // wait up to 10 seconds for serial port to connect. Needed for native USB port only.
    delay(50);
    elapsed = millis() - start;
  }
  if (HMR) {
    result = true;
  }
  // strcpy(xbuf, "x?");  // Put SOMETHING in the buffers so they're not initially empty
  // strcpy(ybuf, "y?");
  // strcpy(zbuf, "z?");
  nextHmrStateTransition = millis() + 3000; // 3 seconds after setup, starting sending things to the HMR
  printlnLong("nextHmrStateTransition = ", nextHmrStateTransition);
  return result;
}


boolean setupSD() {
  // Set up the SD card writer for logging
  boolean good = false;
  char msg[20] = "SD: ";
  if (SD.begin(SD_SPI_CHIPSELECT)) {
    good = true;
    strcat(msg, "good");
    getNextFileName(); // look at what filenames already exist, and create a new one at index + 1
    goodSD = true; // so logHeader will work
    logHeader(); // Log a first line to describe what the data is
  } else {
    strcat(msg, "FAIL");
  }
  return good;
}


boolean setupLoRa() {
  boolean result = false;
  strncpy(printBuf, "nothing yet", PRINT_BUF_LEN - 1);
  if (!LoRa.begin(915E6)) {
    printlnStr("Starting LoRa failed!");
    result = false;
  } else {
    result = true;
  }
  return result;
}


void setupLED() {
  // set up a distinctive blink pattern
  pinMode(LED_BUILTIN, OUTPUT);
  // Serial.print("LED_BUILTIN = "); Serial.println(LED_BUILTIN);
}


boolean setupPresSensor() {
  boolean result = false;
  int resetPin = -1; // not used
  int eocPin   = -1; // not used
  presSensor = Adafruit_MPRLS(resetPin, eocPin);
  if (presSensor.begin()) {
    result = true;
  }
  return result;
}


boolean selfTest() {
  // Run some self-tests and return false if something is terribly wrong.
  Serial.println("\n\********** selfTest() **********");
  boolean result = true;
  int errors = 0;
  errors += testHPaToMeters(0, STANDARD_PRESSURE_HPA);
  errors += testHPaToMeters(111, 1000.0); // 1000 hPa = 110.8 m = 363.6 ft (per weather.gov online calculator)
  // 900 hPa = 988.1 m = 3241.8 ft
  errors += testHPaToMeters(5572, 500.0); // 500 hPa = 5572.1 m = 18,281.2 ft
  errors += testHPaToMeters(15790, 100.0); // 100 hPa = 15790.5 m = 51806 ft
  errors += testHPaToMeters(25908, 10.0); // 10 hPa = 25907.5 m = 84998.2 ft
  errors += testHPaToMeters(32435, 1.0); // 1 hPa = 32435.3 m = 106414.9 ft
  Serial.println("***********************************\n");
  // delay(10000);
  return (errors == 0);
}


int testHPaToMeters(int expected, float hPa) {
  // Return 1 on failure, 0 on success.
  int result = 0;
  float tolerancePct = 2.0;
  int found = hPaToMeters(hPa);
  snprintf(tmpBuf, TMP_BUF_LEN, "expected %5d, found %5d for hPa = ", expected, found);
  dtostrf(hPa, 6, 1, &tmpBuf[strlen(tmpBuf)]);  // float (double, really) to string
  float errorPct;
  if (expected == 0) {
    if (found == expected) {
      errorPct = 0.0; 
    } else {
      errorPct = found - expected;
    }
  } else {
    errorPct = 100.0 * (found - expected) / (expected);
  }
  // float maxDelta = expected * tolerancePct / 100.0;
  // int delta = abs(expected - found);
  if (errorPct > tolerancePct) {
    result = 1;
  }
  strcat(tmpBuf, ", error ");
  dtostrf(errorPct, 5, 2, &tmpBuf[strlen(tmpBuf)]);
  strcat(tmpBuf, " %");
  if (result) {
    strcat(tmpBuf, " *** FAIL");
  } else {
    strcat(tmpBuf, " *** Pass");
  }
  Serial.println(tmpBuf);
  return result;
}


/* ******************************************************************************** */


void loop() {
  if (hmrState != lastHmrState) {
    Serial.print("new hmrState = ");
    Serial.println(hmrState);
    lastHmrState = hmrState;
  }
  loopLED(); // blink the LED as proof-of-life
  loopHMR(); // Check for HMR data from the HMR
  loopLoRa();
  
  // Every so often update the OLED display
  if (millis() >= nextUpdateDisplay) {
    updateDisplay();
    nextUpdateDisplay = millis() + DISPLAY_UPDATE_INTERVAL_MS;
  }
  // loopRelays();
  loopPresSensor();
  loopCount++;
}


void loopLED() {
  #ifdef USE_IOT_CARRIER
    long ms = millis() % 1000;
    int p = ms / 200; // which LED, 0-4
    int a = 0; // off color
    int b = 32; // on color (255 is VERY bright)
    for(int j = 0; j < NUMPIXELS; j++) {
      if (j == p) {
        carrier.leds.setPixelColor(j, 0, b, 0);
      } else {
        carrier.leds.setPixelColor(j, a, a, a);
      }
    }
    carrier.leds.show();
  #else  
    long ms = millis() % 2000;
    if (ms < LED_BLINK_INTERVAL) {
      digitalWrite(LED_BUILTIN, HIGH);
    } else if (ms < 2 * LED_BLINK_INTERVAL) {
      digitalWrite(LED_BUILTIN, LOW);
    } else if (ms < 3 * LED_BLINK_INTERVAL) {
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      digitalWrite(LED_BUILTIN, LOW);
    }
  #endif
}

char* rising = "rising";
char* falling = "falling";
char* steady = "steady";

void loopLoRa() {
  if (goodLoRa) {
    // Receive first
      boolean result = false;
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      char* buf = loRaRead(packetSize);
      Serial.println(buf);
    }

    // Now transmit every once in a while, maybe
    if (millis() >= nextLoRaTransmitMillis) {
      char* dir = steady;
      float delta = hPa - last_hPa;
      if (delta < -0.1) {
        dir = rising; // pressure lowers as altitude rises
      } else if (delta > 0.1) {
        dir = falling;
      }
      Serial.print("hPa = "); Serial.print(hPa); Serial.print(", last_hPa = "); Serial.print(last_hPa); 
      Serial.print(", delta = "); Serial.print(delta); Serial.print(", dir = "); Serial.println(dir);
      snprintf(printBuf, PRINT_BUF_LEN, "FlyBoy v%d s/x/y/z/Pa/dir,%d,%d,%d,%d,%d,%s", VERSION, millis()/MS_PER_SECOND, x, y, z, (int)(hPa * 100.0), dir);
      loRaTransmit(printBuf);
      nextLoRaTransmitMillis = millis() + LORA_TRANSMIT_INTERVAL_MS;
    }
  }
}


char* loRaRead(int packetSize) {
  int n = 0; // pointer into the receive buffer
  // read packet
  int count = 0;
  while (LoRa.available()) {
    int d = LoRa.read();
    if (n < LORA_RX_BUF_LEN - 1) {
      loRaRxBuf[n++] = (char)d;
    }
    loRaRxBuf[n++] = (char)0; // null terminate it
  }
  return loRaRxBuf;
}


void logSD(const char *dataString) {
  if (LOG_TO_SD && goodSD) {
    // open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.
    File dataFile = SD.open(logFileName, FILE_WRITE);
    // if the file is available, write to it:
    if (dataFile) {
      dataFile.println(dataString);
      // dataFile.print("receivedPacketCount = ");
      // dataFile.println(receivedPacketCount);
      dataFile.close();
      loggedPacketCount++;
    } else {
      if (goodSerial) {
        // if the file isn't open, pop up an error:
        strcpy(tmpBuf, "error opening:\n  ");
        strcat(tmpBuf, logFileName);
        Serial.println(tmpBuf);
      }
    }
  }
}


void logHeader() {
  logSD("millis\traw\tx\ty\tz\tcount");
}


void logData(long ms, const char* rawData, int x, int y, int z, int count) {
  sprintf(tmpBuf,"%d\t\"%s\"\t%d\t%d\t%d\t%d", ms, rawData, x, y, z, count);
  logSD(tmpBuf); 
}


void loopHMR() {
  // Pull any new chars from the serial port, and if we see the end of a packet, process it.
  // HMR states:
  // 0 = Initial, nothing done yet
  // 1 = 
  int data = HMR.read();
  long a, b, elapsed;
  int messagesPerSec;
  if (data > 0) {
    if (data == 13 || receiveBufNextChar >= S1_RECEIVE_BUF_LEN - 1) {
      processReceiveBuf();
    } else {
      receiveBuf[receiveBufNextChar++] = (char)(data & 0x7F);
    }
  }
  switch(hmrState) {
    case 0: // wait 3 seconds before starting
      if (millis() > 3000) {
        printlnInt("Setting polling rate to ", SAMPLES_PER_SECOND);
        snprintf(printBuf, PRINT_BUF_LEN, "*99R=%d\r", SAMPLES_PER_SECOND);
        printlnStr(printBuf);
        HMR.write(printBuf);
        hmrState++;
        waitingForOk = true;
      }
      break;
    case 1:
      // waiting for OK
      if (!waitingForOk) {
        hmrState++;
      }
      break;
    case 2:
      // HMR.write("*99P\r"); // P = Poll
      HMR.write("*99C\r"); // continuous until ESC
      // timerStart = millis();
      hmrState++;
      // waitUntilMs = millis() + CONTINUOUS_SECS * 1000L; // wait a few seconds
      break;
    case 3:
      // stay in hmrState 3 forever
      break;
    /*
    case 3: // wait for waitUntilMs has passed
      if (millis() >= waitUntilMs) {
        hmrState++;
        timerStop = millis();
      }
      break;
    case 4:  // cancel continuous
      HMR.write(ESC);
      HMR.write("\n");
      Serial.write("Stopped.\n");
      hmrState++;
      break;
    case 5: // we've stopped the HMR, print results
      Serial.println("five");
      elapsed = (timerStop - timerStart);
      messagesPerSec = receiveCount * 1000 / elapsed;
      sprintf(printBuf, "received %d messages in %d ms or %d msg/sec, samplesPerSec = %d"
        , receiveCount, elapsed, messagesPerSec, SAMPLES_PER_SECOND);
      Serial.println(printBuf);
      sprintf(printBuf, "goodSampleLength = %d, badSampleLength = %d, total = %d, CONTINUOUS_SECS = %d"
        , goodSampleLength, badSampleLength, (goodSampleLength + badSampleLength), CONTINUOUS_SECS);
      Serial.println(printBuf);

      Serial.print("LOG_TO_SD = ");
      Serial.println(LOG_TO_SD);
      // delay(500);
      // sprintf(buf, "That's %d messages per second", messagesPerSec);
      // Serial.println(buf);
      //Serial.println("Done.");
      hmrState++;
      break;
    case 6: // report then do nothing
      Serial.println("Done.");
      hmrState++;
      break;
    case 7: // do nothing
      break;
      */
  }
}


void waitForResponse() {
  // Read chars from the serial port until we get a CR (or buffer gets full), then process it
  boolean done = false;
  while (!done) {
    int data = HMR.read();
    if (data > 0) {
      if (data == 13 || receiveBufNextChar >= S1_RECEIVE_BUF_LEN - 1) {
        processReceiveBuf();
        done = true;
      } else {
        receiveBuf[receiveBufNextChar++] = (char)(data & 0x7F);
      }
    }
  }
}


void processReceiveBuf() {
  // The serial receive buffer is assumed to have a message that we should process
  receiveBuf[receiveBufNextChar++] = (char)0; // null terminate the string
  strcpy(lastReceiveBuf, receiveBuf); // make a copy that will persist even when reading in new data
  if (parseXYZ(receiveBuf)) {
    if (false) {
      sprintf(printBuf, "Good result from \"%s\", x = %d, y = %d, z = %d", receiveBuf, x, y, z);
      Serial.println(printBuf);
    }
    if (goodSD) {
      logData(millis(), receiveBuf, x, y, z, loggedPacketCount);
    }
    goodSampleLength ++;
  } else {
    badSampleLength++;
  }
  if (goodSampleLength % 100 == 0) {
    // printlnInt("goodSampleLength = ", goodSampleLength);
    sprintf(printBuf, "goodSampleLength = %d, badSampleLength = %d, total = %d, CONTINUOUS_SECS = %d"
      , goodSampleLength, badSampleLength, (goodSampleLength + badSampleLength), CONTINUOUS_SECS);
    Serial.println(printBuf);
    Serial.println(receiveBuf);
  }
  
  receiveCount++;
  receiveBufNextChar = 0;
  waitingForOk = false;
  if (hmrState == 5) {
    Serial.println("five");
    int elapsed = (int)(timerStop - timerStart);
    int messagesPerSec = receiveCount * 1000 / elapsed;
    sprintf(printBuf, "received %d messages in %d ms or %d msg/sec, samplesPerSec = %d"
      , receiveCount, elapsed, messagesPerSec, SAMPLES_PER_SECOND);
    Serial.println(printBuf);
    sprintf(printBuf, "goodSampleLength = %d, badSampleLength = %d", goodSampleLength, badSampleLength);
    Serial.println(printBuf);
    delay(500);
    // sprintf(buf, "That's %d messages per second", messagesPerSec);
    // Serial.println(buf);
    //Serial.println("Done.");
    hmrState++;
  }
}

    // Should be 27 chars long for a standard x, y, z triplet
    // parse the x, y, and z values from an HMR2300 message
    // Requires the Regexp (Regular Expressions) library by Nick Gammon
    // Typical output: "  2,139  - 2,899    5,944  "
    //             or: "-32,000  -32,000  -32,000  "
    //             or: "- 1,363  - 4,175    4,667  "
    //             or: "- 2,417  - 3,361  - 4,901  "
    //             or: "  4,630  -   548  - 4,937  "
    // About +/- 4900 is as high as I ever see just from earth/desk environment

boolean parseXYZ(char *inputBuf) {
  // return true if we got fresh, new, legit-looking data
  boolean returnValue = false;
  if (strlen(inputBuf) != 27) {
    if (strlen(inputBuf) != 0) {
      // ignore blank lines
      sprintf(printBuf, "inputBuf is not 27 char long: \"%s\"", strlen(inputBuf));
      logSD(printBuf);
      // Serial.println(printBuf);
    }
  } else {
    MatchState ms;
    ms.Target(inputBuf);
    char matchResult = ms.Match("(-?%s*[%d,]+)%s+(-?%s*[%d,]+)%s+(-?%s*[%d,]+)");
    char tmp[32];
    char tmp2[32];
    int values[3]; // x, y, and z
  
    if (matchResult != REGEXP_MATCHED) {
      sprintf(printBuf, "Regular expression not matched in \"%s\"", inputBuf);
      logSD(printBuf);
      Serial.println(printBuf);
    } else {
      if (ms.level != 3) {
        sprintf(printBuf, "Regular expression matched, but level != 3 in \"%s\"", inputBuf);
        logSD(printBuf);
        Serial.print(printBuf);
      } else {
        if (false) {
          sprintf(printBuf, "Good returnValue in \"%s\"", inputBuf);
          logSD(printBuf);
          Serial.println(printBuf);
        }
        for(int j = 0; j < ms.level; j++) {
          ms.GetCapture(tmp, j);
          justDigits(tmp2, tmp);
          values[j] = atoi(tmp2);
        }
        x = values[0];
        y = values[1];
        z = values[2];
        if (x < minx)
          minx = x;
        if (x > maxx)
          maxx = x;
        if (y < miny)
          miny = y;
        if (y > maxy)
          maxy = y;
        if (z < minz)
          minz = z;
        if (z > maxz)
          maxz = z;
        if (false) {
          sprintf(printBuf, "x=%d, y=%d, z=%d", x, y, z);
          Serial.println(printBuf);
        }
        returnValue = true; // good result
      }
    }
  }
  return returnValue;
}

/* Some example captured data (while playing with a magnet, regular values rarely reach +/- 5000)  
18:49:58.634 -> input buf = "  4,534      302  - 5,726  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    302" = "302" =    302, [2] = "- 5,726" = "-5726" =  -5726
18:49:58.634 -> x =   4534, -32662,  32727, y =    302, -32768,  32712, z =  -5726, -32540,  32265
18:49:58.743 -> input buf = "  4,534      305  - 5,729  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    305" = "305" =    305, [2] = "- 5,729" = "-5729" =  -5729
18:49:58.743 -> x =   4534, -32662,  32727, y =    305, -32768,  32712, z =  -5729, -32540,  32265
18:49:58.818 -> input buf = "  4,534      305  - 5,729  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    305" = "305" =    305, [2] = "- 5,729" = "-5729" =  -5729
18:49:58.818 -> x =   4534, -32662,  32727, y =    305, -32768,  32712, z =  -5729, -32540,  32265
18:49:58.927 -> input buf = "  4,534      303  - 5,728  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    303" = "303" =    303, [2] = "- 5,728" = "-5728" =  -5728
18:49:58.927 -> x =   4534, -32662,  32727, y =    303, -32768,  32712, z =  -5728, -32540,  32265
18:49:59.038 -> input buf = "  4,534      305  - 5,729  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    305" = "305" =    305, [2] = "- 5,729" = "-5729" =  -5729
18:49:59.038 -> x =   4534, -32662,  32727, y =    305, -32768,  32712, z =  -5729, -32540,  32265
18:49:59.147 -> input buf = "  4,534      305  - 5,729  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    305" = "305" =    305, [2] = "- 5,729" = "-5729" =  -5729
18:49:59.147 -> x =   4534, -32662,  32727, y =    305, -32768,  32712, z =  -5729, -32540,  32265
18:49:59.257 -> input buf = "  4,534      303  - 5,728  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    303" = "303" =    303, [2] = "- 5,728" = "-5728" =  -5728
18:49:59.257 -> x =   4534, -32662,  32727, y =    303, -32768,  32712, z =  -5728, -32540,  32265
18:49:59.332 -> input buf = "  4,534      305  - 5,729  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    305" = "305" =    305, [2] = "- 5,729" = "-5729" =  -5729
18:49:59.332 -> x =   4534, -32662,  32727, y =    305, -32768,  32712, z =  -5729, -32540,  32265
18:49:59.444 -> input buf = "  4,534      303  - 5,726  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    303" = "303" =    303, [2] = "- 5,726" = "-5726" =  -5726
18:49:59.444 -> x =   4534, -32662,  32727, y =    303, -32768,  32712, z =  -5726, -32540,  32265
18:49:59.557 -> input buf = "  4,534      303  - 5,728  ", Found 3: [0] = "  4,534" = "4534" =   4534, [1] = "    303" = "303" =    303, [2] = "- 5,728" = "-5728" =  -5728
18:49:59.557 -> x =   4534, -32662,  32727, y =    303, -32768,  32712, z =  -5728, -32540,  32265
 */


char* justDigits(char* outBuf, const char *inBuf) {
  int len = strlen(inBuf);
  int k = 0;
  char c;
  for(int j = 0; j < len; j++) {
    c = inBuf[j];
    if ( (c >= '0' && c <= '9') || c == '-') {
      outBuf[k++] = c;
    }
  }
  outBuf[k] = (char)0;
  return outBuf;
}


/**
 * Look at the SD card for files like LORA-1.LOG, LORA-2.LOG, etc, and choose the next numbered file name.
 */


void getNextFileName() {
  File root = SD.open("/");
  boolean found = false;
  boolean exhausted = false;
  int n = 0;
  int nextIndex = 0; // HMR-0000.TSV
  do {
    File entry = root.openNextFile();
    if (entry) {
      // Serial.print("entry name = ");
      String name = entry.name();
      // Serial.println(name);
      entry.close();
      if (name.startsWith("HMR-")) {
        // Serial.println("starts with");
        String tmp = name.substring(4, name.indexOf(".TSV"));  // Tab-separated values
        int index = tmp.toInt();
        if (index >= nextIndex) {
          nextIndex = index + 1;
        }
      } else {
        //Serial.println("does not start with");
      }
    } else {
      // Serial.println("no more files");
      exhausted = true;
    }
    n++;
  } while (!found && !exhausted);
  root.close();
  sprintf(logFileName, "HMR-%04d.TSV", nextIndex); // file name max size: 8.3 I think
}


#define HPA_PER_IN_HG 33.863886666667


void loopPresSensor() {
  long start = millis();
  if (start >= nextPresSensorReading) {
    last_hPa = hPa;
    last_altitudeMeters = altitudeMeters;
    hPa = presSensor.readPressure();
    // hPa = 500.0; // for testing
    if (isnan(hPa)) {
      altitudeMeters = -1;
    } else {
      Serial.print("Pressure hPa: "); Serial.print(hPa);
      altitudeMeters = hPaToMeters(hPa);
    }
    nextPresSensorReading = millis() + PRES_SENSOR_READING_INTERVAL_MS;
  }
}



int hPaToMeters(float hPa) {
  float temperatureF = 37.0;
  if (false) {
    // hPa = 1037.0; for comparing against online example
    double result = hPa * 100.0;
    Serial.print("result = "); Serial.println(result);
    result = result / 101325.0;
    Serial.print("result = "); Serial.println(result);
    result = log(result);
    Serial.print("result = "); Serial.println(result);
    result = result * 287.053;
    Serial.print("result = "); Serial.println(result);
    result = result * ((temperatureF + 459.67) * (5.0/9.0));
    Serial.print("result = "); Serial.println(result);
    result = result / 9.8;
    Serial.print("result = "); Serial.println(result);
  }
  
  // this one never worked well: return (int)((log((hPa * 100.0)/101325.0) * 287.053 * (temperatureF + 459.67 * 5.0 / 9.0))/(9.8));
  // Equation 8 from Luther's paper:
  float p = hPa * 100.0;
  return 44330.8 - 4946.54 * pow(p, 0.1902632);
}


void updateDisplay() {
  if (goodDisplay) {
    #ifdef USE_IOT_CARRIER
      carrier.display.fillScreen(ST77XX_RED); //red background
      carrier.display.setTextColor(ST77XX_WHITE); //white text
      carrier.display.setTextSize(2); //medium sized text
    
      carrier.display.setCursor(0, 0); //sets position for printing (x and y)
      carrier.display.print("Temp: "); //prints text
      carrier.display.print(temperature); //prints a variable
      carrier.display.println(" C"); //prints text
      carrier.display.print("Humid: "); carrier.display.print(humidity); carrier.display.println(" %");
      carrier.display.print("Pressure: "); carrier.display.print(pressure); carrier.display.println(" ?");
      carrier.display.print("loopCount: "); carrier.display.println(loopCount);
      carrier.display.print("relayStatus: "); carrier.display.println(relayStatus);
    #else
      // display.setTextSize(OLED_DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
      display.clearDisplay();
      display.setCursor(0,0);
      // make a flashing asterisks to show we're alive
      if (millis() % 1000 < 500) {
        snprintf(tmpBuf, TMP_BUF_LEN, "    Fly Boy v%d ***", VERSION);
      } else {
        snprintf(tmpBuf, TMP_BUF_LEN, "*** Fly Boy v%d", VERSION);
      }
      display.println(tmpBuf);

      /*
      if (goodSerial) {
        display.print(F("Ser: ok"));
      } else {
        display.print(F("Ser: FAIL"));
      }
      */
      if (goodHMR) {
        display.println(F(" HMR: ok"));
      } else {
        display.println(F(" HMR: FAIL"));
      }
      // display.print("hmrState = "); display.println(hmrState);
      display.print("File: "); display.println(logFileName);
      display.println(getRunTime(millis()));
      sprintf(printBuf, "x = %6d y = %6d\nz = %6d c = %d", x, y, z, loggedPacketCount);
      display.println(printBuf);
      display.print("Pres "); display.print(hPa); display.println(" hPa");
      display.print("Alt "); display.print(altitudeMeters); display.println(" m");
      display.display();
    #endif
  }
  lastMillis = millis();
}

// Define a buffer dedicated to the getRunTime() function
// Looks like "Run time
#define RUN_TIME_BUF_LEN 20
char runTimeBuf[RUN_TIME_BUF_LEN];

char* getRunTime(long ms) {
  int hours = ms / MS_PER_HOUR;
  ms -= hours * MS_PER_HOUR;
  int minutes = ms / MS_PER_MINUTE;
  ms -= minutes * MS_PER_MINUTE;
  int seconds = ms / 1000;
  ms -= seconds * 1000;
  //                   12345678901234567890
  //                   Run time 000:00:00.0
  snprintf(runTimeBuf, RUN_TIME_BUF_LEN, "Run time %d:%02d:%02d.%1d", hours, minutes, seconds, ms/100);
  return runTimeBuf;
}

void printlnStr(const char* a) {
  if (goodSerial) {
    Serial.println(a);
  }
}

void printlnStr2(const char* a, const char* b) {
  if (goodSerial) {
    Serial.print(a);
    Serial.println(b);
  }
}

void printInt(const char* a, int b) {
  if (goodSerial) {
    Serial.print(a);
    Serial.print(b);
  }
}

void printlnInt(const char* a, int b) {
  if (goodSerial) {
    Serial.print(a);
    Serial.println(b);
  }
}

void printlnLong(const char* a, long b) {
  if (goodSerial) {
    Serial.print(a);
    Serial.println(b);
  }
}

#define DIGITS_BUF_LEN 10
char digitsBuf[DIGITS_BUF_LEN];


int parseBuf(char* buf) {
  int result = 0;
  boolean done = false;
  int j = 0;
  int dlen = 0;
  digitsBuf[dlen++] = buf[0]; // get the sign, if any
  while (!done && j < DIGITS_BUF_LEN) {
    if (buf[0] == (char)0) {
      done = true;
    } else {
      if (isDigit(buf[j])) {
        digitsBuf[dlen++] = buf[j];
      }
    }
    j++;
  }
  digitsBuf[dlen] = (char)0;
  // Serial.print("digits: \""); Serial.print(digitsBuf); Serial.println("\"");
  result = atoi(digitsBuf);
  return result;
}


void loopRelays() {
  #ifdef USE_IOT_CARRIER
    long ms = millis() % 9000;
    if (ms < 3000) {
      strcpy(relayStatus, "both open");
      carrier.Relay1.open();
      carrier.Relay2.open();
    } else if (ms < 6000) {
      strcpy(relayStatus, "one open, one closed");
      carrier.Relay1.close();
      carrier.Relay2.open();
    } else {
      strcpy(relayStatus, "both closed");
      carrier.Relay1.close();
      carrier.Relay2.close();
    }
    delay(500);
  #endif
}


void loRaTransmit(String str) {
  if (goodLoRa) {
    LoRa.beginPacket();
    LoRa.print(str);
    LoRa.endPacket();
  } else {
    Serial.print("Cannot send to LoRa: "); Serial.print(str);
  }
}
