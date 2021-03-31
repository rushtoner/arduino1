/*
  Based on GPS location, reads from Serial1 (GPS?) and passes through to Serial (USB)
*/
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <SD.h>

// Adafruit GFX and SSD1303 for the 128x32 or 128x64 OLED displays
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
/* added for display above */
#define DISPLAY_TEXT_SIZE 1
#define TEXT_COLUMNS 21
#define DATA_TEXT_ROWS 3
#define DISPLAY_UPDATE_INTERVAL_MS 250
#define ELAPSED_BUF_LEN 100
#define SD_SPI_CHIPSELECT 4
#define DISPLAY_WIDTH 128 // OLED display width, in pixels
#define DISPLAY_HEIGHT 64 // OLED display height, in pixels
#define TEXT_SIZE 1
#define DISPLAY_COLS 21
#define DISPLAY_ROWS 8
#define LORA_FREQ 915E6
#define TMP_BUF_LEN 512
#define RAW_BUF_LEN 512
#define LORA_LOG_FILE_NAME_LEN 20
#define SERIAL_BUF_LEN 1024
#define BLINK_INTERVAL 200
#define PRINTABLE_BUF_LEN 512
#define GPS_BUF_LEN 512

// DISPLAY_BUF_LEN is how many chars of display space is left for printing a received packet
#define DISPLAY_BUF_LEN (6 * 21)
#define PRINT_RULER false
#define MS_PER_MINUTE (1000 * 60)
#define MS_PER_HOUR (MS_PER_MINUTE * 60)
#define SERIAL_INIT_TIMEOUT_MS 10000

#define GPS Serial1

const char *title = "GPS Display";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int receivedPacketCount = 0;
int loggedPacketCount = 0;
int lastRssi = 0;
int byteCount = 0;

// printableBuf is chars received over LoRa that are printable
char printableBuf[PRINTABLE_BUF_LEN];
int printableBufCount = 0;

// rawBuf is chars received over LoRa that are printable (or hexified into printable)
char rawBuf[RAW_BUF_LEN];
int rawBufCount = 0;

char gpsBuf[GPS_BUF_LEN];
int gpsBufCount = 0;

char displayBuf[DISPLAY_BUF_LEN];

char tmpBuf[TMP_BUF_LEN];

char logFileName[LORA_LOG_FILE_NAME_LEN];
long nextDisplay = 0;

// Set up a "good" flag for each subsystem, to keep track of which ones initialized okay.
boolean goodSerial  = false;  // The USB port
boolean goodSerial1 = false;  // 2nd UART on pins 13 & 14
boolean goodSD      = false;
boolean goodDisplay = false;
boolean goodLoRa    = false;


void setup() {
  // Set up each subsystem
  goodDisplay = setupDisplay(6);

  goodSerial  = setupSerial(9600, SERIAL_INIT_TIMEOUT_MS);
  if (goodSerial) {
    Serial.println("Serial ready");
  }
  
  goodSerial1 = setupSerial1(9600, SERIAL_INIT_TIMEOUT_MS);
  if (goodSerial1) {
    Serial.println("GPS ready");
  }
  strcpy(displayBuf, "wait for it...");
  // goodSD = setupSD();
  setupLED();
}


boolean setupSerial(long bps, long initTimeoutMs) {
  boolean result = false;
  // initialize serial communications and wait for port to open:
  Serial.begin(bps); // USB and serial monitor
  long start = millis();
  long elapsed = 0L;
  while (!Serial && elapsed < initTimeoutMs) {
    // wait up to 10 seconds for serial port to connect. Needed for native USB port only.
    delay(50);
    elapsed = millis() - start;
  }
  if (Serial) {
    result = true;
  }
  return result;
}



boolean setupSerial1(long bps, long initTimeoutMs) {
  boolean result = false;
  // initialize serial communications and wait for port to open:
  Serial1.begin(bps); // USB and serial monitor
  long start = millis();
  long elapsed = 0L;
  while (!Serial1 && elapsed < initTimeoutMs) {
    // wait up to 10 seconds for serial port to connect. Needed for native USB port only.
    delay(50);
    elapsed = millis() - start;
  }
  if (Serial1) {
    result = true;
  }
  return result;
}


boolean setupDisplay(int delaySecs) {
  boolean result = false;
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
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
    display.print("-- ");
    display.print(title);
    display.println(" --");
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


boolean setupLoRa() {
  boolean result = false;
  strcpy(printableBuf, "nothing yet");
  if (!LoRa.begin(915E6)) {
    Serial.println("Starting LoRa failed!");
    result = false;
  } else {
    result = true;
  }
  return result;
}


void setupLED() {
  pinMode(LED_BUILTIN, OUTPUT);
}

int count = 0;

void loop() {
  loopLED();  // do the distinctive blink pattern
  loopGPS();
  if (millis() >= nextDisplay) {
    updateDisplay();
    nextDisplay = millis() + 200;
  }
}


void loopGPS() {
  int data = Serial1.read();
  if (data > 0) {
    char c = (char)(data & 0x7F);
    // Serial.print(c);
    if (c == 10) {
      // ignore line feeds
    } else if (c == 13) {
      String str = String(gpsBuf);
      if (str.startsWith("$GPGGA")) {
        strncpy(displayBuf, gpsBuf, DISPLAY_BUF_LEN);
        count++;
        //sprintf(tmpBuf, "cnt = %d", count);
        //strcat(displayBuf, tmpBuf);
      }
      Serial.print("gpsBuf = ");
      Serial.println(gpsBuf);
      gpsBufCount = 0;
    } else {
      gpsBuf[gpsBufCount++] = c;
      gpsBuf[gpsBufCount] = (char)0; // keep it null terminated
    }
    //Serial.print("gpsBuf = ");
    //Serial.println(gpsBuf);
  }
}


boolean loopLoRa() {
  // try to parse packet, return true if got new data to display and log
  boolean result = false;
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    printableBufCount = 0; // reset to start of buffers
    rawBufCount = 0;
    // read packet
    int count = 0;
    while (LoRa.available()) {
      int d = LoRa.read();
      // char c = (char)(d & 0x7F);
      char c = (char)d;

      // printCurrentState(d, c, count);

      if (true) {
        // Stuff it into the printable buffer (omits non-printables)
        if (isPrintable(c) && printableBufCount < PRINTABLE_BUF_LEN - 1) {
          printableBuf[printableBufCount++] = c;
        }
      }

      // printBuf("rawBuf before: ", rawBuf, rawBufCount, RAW_BUF_LEN);
      // Stuff it into the raw buffer (hexifies non-printables)
      if (isPrintable(c)) {
        if (false) {
          Serial.print("rawBufCount = "); Serial.print(rawBufCount);
          Serial.print(", RAW_BUF_LEN = "); Serial.print(RAW_BUF_LEN);
        }
        if (rawBufCount < RAW_BUF_LEN - 1) {
          // printlnInt(", appending ", d);
          rawBuf[rawBufCount++] = c;
          rawBuf[rawBufCount] = (char)0; // null terminate it
        } else {
          // printlnInt(", omitting printable ", d);
        }
      } else {
        if (rawBufCount < RAW_BUF_LEN - (6 + 1)) {
          // hexify it, which takes 6 chars "[0xFF]" plus 1 for the null terminator
          sprintf(tmpBuf, "[0x%02x]", (int)c);
          rawBuf[rawBufCount] = (char)0; // null-terminate it for strcat
          strcat(rawBuf, tmpBuf); // this takes care of the null terminator for us
          rawBufCount += strlen(tmpBuf);
        } else {
          // Serial.print("Omitting non-printable ");
          // Serial.println(d);
        }
      }
      // printBuf("rawBuf after:  ", rawBuf, rawBufCount, RAW_BUF_LEN);
      if (false) {
        Serial.print("rawBufCount = ");
        Serial.println(rawBufCount);
        Serial.print("rawBuf:");
        Serial.println(rawBuf);
      }
      count++;
      // printlnInt("-- end, count = ", count);
    }
    /*
    if (printableBufCount < PRINTABLE_BUF_LEN - 1) {
      printableBuf[printableBufCount++] = '*';
    }
    */
    printableBuf[printableBufCount++] = (char)0; // null-terminate it
    // rawBuf[rawBufCount++] = (char)0; // null-terminate it
    lastRssi = LoRa.packetRssi();
    receivedPacketCount++;
    result = true;
  }
  return result;
}


void updateDisplay() {
  if (goodDisplay) {
    // display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
    display.clearDisplay();
    display.setCursor(0,0);
    if (millis() % 1000 < 500) {
      //             123456789012345678901
      display.print("** ");
      display.println(title);
    } else {
      display.print("   ");
      display.print(title);
      display.println(" **");
    }
    display.print("count = "); display.println(count);
    display.println(displayBuf);
    display.display();
  }
}



boolean setupSD() {
  boolean good = false;
  char msg[20] = "SD: ";
  if (SD.begin(SD_SPI_CHIPSELECT)) {
    good = true;
    strcat(msg, "good");
    getNextFileName();
    logSD("Starting.");
  } else {
    strcat(msg, "FAIL");
  }
  Serial.println(msg);
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
      dataFile.print("receivedPacketCount = ");
      dataFile.println(receivedPacketCount);
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


/**
 * Look at the SD card for files like LORA-1.LOG, LORA-2.LOG, etc, and choose the next numbered file name.
 */
void getNextFileName() {
  File root = SD.open("/");
  boolean found = false;
  boolean exhausted = false;
  int n = 0;
  int nextIndex = 0; // LORA-index.LOG
  do {
    File entry = root.openNextFile();
    if (entry) {
      String name = entry.name();
      entry.close();
      if (name.startsWith("LORA-")) {
        String tmp = name.substring(5, name.indexOf(".LOG"));
        int index = tmp.toInt();
        if (index >= nextIndex) {
          nextIndex = index + 1;
        }
      }
    } else {
      exhausted = true;
    }
    n++;
  } while (!found && !exhausted);
  root.close();
  sprintf(logFileName, "LORA-%03d.LOG", nextIndex); // file name max size: 8.3 I think
  Serial.print("logFileName = "); Serial.println(logFileName);
}


void loopLED() {
  // Create a distinctive blink pattern, so it's easy to tell if the program loop is running
  long ms = millis() % 2000;
  if (ms < BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (ms < 2 * BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, LOW);
  } else if (ms < 3 * BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (ms < 4 * BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, LOW);
  } else if (ms < 5 * BLINK_INTERVAL) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}
