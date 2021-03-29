/*
  Based on ReadHMR2300, by David Rush, 2021 Mar 28
  First test of pulling data from HMR2300 on Arduino MKR 1310
*/
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <SD.h>

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
#define CONTINUOUS_SECS (12 * 60 * 60)
#define LOG_FILE_NAME_LEN 20
#define LOG_BUF_LEN 256
#define TMP_BUF_LEN 256

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int n = 0;

// Loop code runs as a state machine
// state 0 = warming up
// state 1 = command sent
// state 2 = just do single-shot on request
int state = 0;
int lastState = -1;

char tmpBuf[TMP_BUF_LEN];

char receiveBuf[RECEIVE_BUF_LEN];
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
int samplesPerSecond = 20;
long timerStart = 0;
long timerStop = 0;
long lastMillis = 0;

// Set up boolean indicators of whether each subsystem was successfully initialized
boolean goodSerial = false;
boolean goodDisplay = false;
boolean goodHMR = false;
boolean goodSD = false;

void setup() {
  goodSerial = setupSerial();
  goodDisplay = setupDisplay(5);
  goodHMR = setupHMR();
  goodSD = setupSD();
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
    display.println(F("-- Flight Unit --"));
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
  strcpy(xbuf, "x?");
  strcpy(ybuf, "y?");
  strcpy(zbuf, "z?");
  return result;
}


boolean setupSD() {
  boolean good = false;
  char msg[20] = "SD: ";
  if (SD.begin(SD_SPI_CHIPSELECT)) {
    good = true;
    strcat(msg, "good");
    getNextFileName();
    logHeader();
  } else {
    strcat(msg, "FAIL");
  }
  printlnStr(msg);
  return good;
}


void logSD(const char *dataString) {
  if (goodSD) {
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
  logSD("millis,x,y,z,packetCount");
}

void logData(long ms, int x, int y, int z, int count) {
  sprintf(tmpBuf,"%d,%d,%d,%d,%d", ms, x, y, z, count);
  logSD(tmpBuf); 
}


void setupLED() {
  // set up a distinctive blink pattern
  pinMode(LED_BUILTIN, OUTPUT);
}


void loop() {
  if (state != lastState) {
    Serial.print("new state = ");
    Serial.println(state);
    lastState = state;
  }
  loopLED();
  loopHMR();
  if (millis() - lastMillis > 100) {
    updateDisplay();
  }
}


#define BLINK_INTERVAL 200

void loopLED() {
  long ms = millis() % 2000;
  if (ms < BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (ms < 2 * BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, LOW);
  } else if (ms < 3 * BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}


void loopHMR() {
  int data = HMR.read();
  long a, b, elapsed;
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
        // int samplesPerSecond = 20;
        Serial.print("Setting polling rate to ");
        Serial.println(samplesPerSecond);
        sprintf(tmpBuf, "*99R=%d\r", samplesPerSecond);
        Serial.println(tmpBuf);
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
    case 5: // send test
      /*
      if (true) {
        a = millis();
        Serial.println("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890");
        delay(10);
        b = millis();
        elapsed = (int)(b - a);
        Serial.print("send time ");
        Serial.print(elapsed);
        Serial.println(" ms");
      }
      state++;
      */
      break;
    case 6: // do nothing
      break;
  }
}


void waitForResponse() {
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
  receiveBuf[receiveBufNextChar++] = (char)0; // null terminate the string
  strcpy(lastReceiveBuf, receiveBuf); // make a copy that will persist even when reading in new data
  if (false) {
    Serial.println("             \"012345678901234567890123456789\"");
    Serial.print("receiveBuf = \"");
    Serial.print(receiveBuf);
    Serial.println("\"");
  }
  strncpy(xbuf, receiveBuf, 7);
  x = parseBuf(xbuf);
  strncpy(ybuf, receiveBuf + 9, 7);  
  y = parseBuf(ybuf);
  strncpy(zbuf, receiveBuf + 18, 7);  
  z = parseBuf(zbuf);
  logData(millis(), x, y, z, loggedPacketCount);

  /*
  if (x < minx)
    minx = x;
  if (x > maxx) 
    maxx = x;
    */
  if (true) {
    if (goodSerial) {
      Serial.print(millis());
      printInt(", ", x);
      //printInt(", minx = ", minx);
      //printlnInt(", maxx = ", maxx);
      printInt(", ", y);
      printInt(", ", z);
      printlnInt(", ", loggedPacketCount);
    }
  }
  

  receiveCount++;
  receiveBufNextChar = 0;
  waitingForOk = false;
  if (state == 5) {
    Serial.println("five");
    int elapsed = (int)(timerStop - timerStart);
    int messagesPerSec = receiveCount * 1000 / elapsed;
    sprintf(tmpBuf, "received %d messages in %d ms or %d msg/sec, samplesPerSec = %d"
      , receiveCount, elapsed, messagesPerSec, samplesPerSecond);
    Serial.println(tmpBuf);
    delay(500);
    // sprintf(buf, "That's %d messages per second", messagesPerSec);
    // Serial.println(buf);
    //Serial.println("Done.");
    state++;
  }
}



/**
 * Look at the SD card for files like LORA-1.LOG, LORA-2.LOG, etc, and choose the next numbered file name.
 */


void getNextFileName() {
  File root = SD.open("/");
  boolean found = false;
  boolean exhausted = false;
  int n = 0;
  int nextIndex = 0; // HMR-0000.LOG
  do {
    File entry = root.openNextFile();
    if (false) {
      Serial.print("n = ");
      Serial.print(n);
      Serial.print(", entry = ");
      Serial.println(entry);
    }
    if (entry) {
      // Serial.print("entry name = ");
      String name = entry.name();
      // Serial.println(name);
      entry.close();
      if (name.startsWith("HMR-")) {
        // Serial.println("starts with");
        String tmp = name.substring(4, name.indexOf(".LOG"));
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
  sprintf(logFileName, "HMR-%04d.LOG", nextIndex); // file name max size: 8.3 I think
  Serial.print("logFileName = ");
  Serial.println(logFileName);
  // Serial.println("done");
}


void updateDisplay() {
  if (goodDisplay) {
    // display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
    display.clearDisplay();
    display.setCursor(0,0);
    /*
    display.print(F("File: "));
    display.print(loraLogFileName);
    */
    // make a flashing asterisk to show we're alive
    if (millis() % 1000 < 500) {
      display.println(F("   Flight Unit   "));
    } else {
      display.println(F("** Flight Unit **"));
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
    // display.println(lastReceiveBuf);
    // display.print("xbuf = "); display.println(xbuf);
    display.print("m = "); display.println(millis());
    display.print("x = "); display.println(x);
    display.print("y = "); display.println(y);
    display.print("z = "); display.println(z);
    display.print("log: "); display.println(loggedPacketCount);
    /*
    display.print("RSSI: ");
    display.println(lastRssi);
    if (true) {
      // Tried using "%,d" in order to get results like "1,234" but it didn't work
      sprintf(tmpBuf, "Received: %d", receivedPacketCount);
      display.println(tmpBuf);
      // sprintf(tmpBuf, "Logged:   %d", loggedPacketCount);
      sprintf(tmpBuf, "Logged:   %d, c %d", loggedPacketCount, canary);
      display.println(tmpBuf);
    } else {
      display.print("Received: ");
      display.println(receivedPacketCount);
      display.print("Logged:   ");
      display.println(loggedPacketCount);
    }
    */
    /*
    display.println(elapsedMsg(millis()));
    if (printableBufCount <= DISPLAY_BUF_LEN) {
      display.print(printableBuf);
    } else {
      strncpy(tmpBuf, printableBuf, DISPLAY_BUF_LEN);
      display.print(tmpBuf);
    }
    */
    display.display();
  }
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

int parseBuf(const char* buf) {
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
