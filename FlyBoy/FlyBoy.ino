/*
  Based on ReadHMR2300, by David Rush, 2021 Mar 28
  Designed to run on a Arduino MKR WAN 1310, with on-board LoRa radio.
  Expects a HMR2300 magnetometer on Serial1, at 9600 bps.
  Expects a OLED screen on I2C.
  Expects an SD card reader on SPI with chipselect on pin 4.
*/
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <SD.h>
#include <Regexp.h> // Library by Nick Gammon for regular expressions

#define MINUTE (1000 * 60)
#define HOUR (MINUTE * 60)
#define CONTINUOUS_SECS (10 * HOUR)
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
/* added for display above */
#define DISPLAY_TEXT_SIZE 1
#define TEXT_COLUMNS 21
#define DATA_TEXT_ROWS 3
#define DISPLAY_UPDATE_INTERVAL_MS 250
// #define BUF_LEN 100

// assign the chip select line for the SD card on pin 4
#define SD_SPI_CHIPSELECT 4
#define DISPLAY_WIDTH 128 // OLED display width, in pixels
#define DISPLAY_HEIGHT 64 // OLED display height, in pixels
#define TEXT_SIZE 1
#define DISPLAY_COLS 21
#define DISPLAY_ROWS 8
#define LORA_FREQ 915E6
// ESC is sent to the HMR2300 in order to stop the unit from sending data continuously
#define ESC 0x1b

// HMR is on the 2nd UART accessible via pins D14 (TX) and D13 (RX) on the MKR 1310
#define HMR Serial1

// How big the Serial1 receive buffer is
#define RECEIVE_BUF_LEN 1000
#define SERIAL_INIT_TIMEOUT_MS 10000

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


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int n = 0;

// Loop code runs as a state machine
// state 0 = warming up
// state 1 = command sent
// state 2 = just do single-shot on request
int state = 0;
int lastState = -1;

char tmpBuf[TMP_BUF_LEN];
char receiveBuf[RECEIVE_BUF_LEN]; // for assembling HMR data from serial port
char printBuf[PRINT_BUF_LEN];     // for sprintf() then Serial.println() output

int receiveBufNextChar = 0;  // character position of next char to go in
char lastReceiveBuf[RECEIVE_BUF_LEN]; // copy here for printing "later"
char logFileName[LOG_FILE_NAME_LEN];
char logBuf[LOG_BUF_LEN];
int loggedPacketCount = 0;

#define X_BUF_LEN 32
char xbuf[X_BUF_LEN];
char ybuf[X_BUF_LEN];
char zbuf[X_BUF_LEN];
int x = 0; 
int minx = 0;  // observed -32768 (with a magnet)
int maxx = 0;  // observed 32608 (with a magnet)
int y = 0;
int miny = 0;
int maxy = 0;
int z = 0;
int minz = 0;
int maxz = 0;

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

// count how many HMR2300 samples were of the expected length, vs. not
int goodSampleLength = 0;
int badSampleLength = 0;

void setup() {
  goodSerial = setupSerial();
  goodDisplay = setupDisplay(5);
  goodHMR = setupHMR();
  goodSD = setupSD();
  goodLoRa = setupLoRa();
  setupLED();
  Serial.println("Serial1 (HMR) ready.");
  Serial.print("state = ");
  Serial.println(state);
  if (goodSD) {
    Serial.println("goodSD is true");
  } else {
    Serial.println("goodSD is false");
  }
}


boolean setupSerial() {
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
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    printlnStr("SSD1306 allocation failed");
  } else {
    result = true;
    // Show initial display buffer contents on the screen --
    // the library initializes this with an Adafruit splash screen.
    // Clear the buffer
    display.clearDisplay();
    display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(0, 0);     // Start at top-left corner
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    //                 123456789012345678901
    display.println(F("-- Fly Boy --"));
    display.println(F("Logs HMR2300 data"));
    display.println(F("  to SD card"));
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
  strcpy(xbuf, "x?");  // Put SOMETHING in the buffers so they're not initially empty
  strcpy(ybuf, "y?");
  strcpy(zbuf, "z?");
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
  // not set up yet
  return result;
}


void setupLED() {
  // set up a distinctive blink pattern
  pinMode(LED_BUILTIN, OUTPUT);
}


/* ******************************************************************************** */


void loop() {
  if (state != lastState) {
    Serial.print("new state = ");
    Serial.println(state);
    lastState = state;
  }
  loopLED(); // blink the LED as proof-of-life
  loopHMR(); // Check for HMR data from the HMR
  
  // Every so often update the OLED display
  if (millis() >= nextUpdateDisplay) {
    updateDisplay();
  }
}


void loopLED() {
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
  int data = HMR.read();
  long a, b, elapsed;
  int messagesPerSec;
  if (data > 0) {
    if (data == 13 || receiveBufNextChar >= RECEIVE_BUF_LEN - 1) {
      processReceiveBuf();
    } else {
      receiveBuf[receiveBufNextChar++] = (char)(data & 0x7F);
    }
  }
  switch(state) {
    case 0: // wait 3 seconds before starting
      if (millis() > 3000) {
        printlnInt("Setting polling rate to ", SAMPLES_PER_SECOND);
        sprintf(tmpBuf, "*99R=%d\r", SAMPLES_PER_SECOND);
        printlnStr(tmpBuf);
        // HMR.write("*99R=40\r");
        HMR.write(tmpBuf);
        state++;
        waitingForOk = true;
      }
      break;
    case 1:
      // waiting for OK
      if (!waitingForOk) {
        state++;
      }
      break;
    case 2:
      // HMR.write("*99P\r"); // P = Poll
      HMR.write("*99C\r"); // continuous until ESC
      timerStart = millis();
      state++;
      waitUntilMs = millis() + CONTINUOUS_SECS * 1000L; // wait a few seconds
      break;
    case 3: // wait for waitUntilMs has passed
      if (millis() >= waitUntilMs) {
        state++;
        timerStop = millis();
      }
      break;
    case 4:  // cancel continuous
      HMR.write(ESC);
      HMR.write("\n");
      Serial.write("Stopped.\n");
      state++;
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
      state++;
      break;
    case 6: // report then do nothing
      Serial.println("Done.");
      state++;
      break;
    case 7: // do nothing
      break;
  }
  if (state == 5) {
  }
}


void waitForResponse() {
  // Read chars from the serial port until we get a CR (or buffer gets full), then process it
  boolean done = false;
  while (!done) {
    int data = HMR.read();
    if (data > 0) {
      if (data == 13 || receiveBufNextChar >= RECEIVE_BUF_LEN - 1) {
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
  if (state == 5) {
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
    state++;
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


void updateDisplay() {
  if (goodDisplay) {
    // display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
    display.clearDisplay();
    display.setCursor(0,0);
    // make a flashing asterisks to show we're alive
    if (millis() % 1000 < 500) {
      display.println(F("      Fly Boy *****"));
    } else {
      display.println(F("***** Fly Boy      "));
    }

    if (goodSerial) {
      display.print(F("Ser: ok"));
    } else {
      display.print(F("Ser: FAIL"));
    }
    if (goodHMR) {
      display.println(F(" HMR: ok"));
    } else {
      display.println(F(" HMR: FAIL"));
    }
    // display.print("state = "); display.println(state);
    display.print("File: "); display.println(logFileName);
    display.print("sec = "); display.println(millis()/1000);
    display.print("until "); display.println(waitUntilMs / 1000);
    sprintf(printBuf, "x = %6d y = %6d\nz = %6d c = %d", x, y, z, loggedPacketCount);
    display.println(printBuf);
    display.display();
  }
  nextUpdateDisplay = millis() + DISPLAY_UPDATE_INTERVAL_MS;
  lastMillis = millis();
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
