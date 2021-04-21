#define VERSION 1

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <SD.h>
#include <Regexp.h> // Library by Nick Gammon for regular expressions

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
#define LORA_LOG_FILE_NAME_LEN 20
#define SERIAL_BUF_LEN 1024
#define BLINK_INTERVAL 200
#define PRINTABLE_BUF_LEN 512
#define RAW_BUF_LEN 512
// DISPLAY_BUF_LEN is how many chars of display space is left for printing a received packet
#define DISPLAY_BUF_LEN (4 * 21)
#define PRINT_RULER false
#define MS_PER_MINUTE (1000 * 60)
#define MS_PER_HOUR (MS_PER_MINUTE * 60)
#define GPSRx Serial1
#define SERIAL_INIT_TIMEOUT_MS 5000 // wait up to 5 seconds for the GPSRx serial port to initialize
#define TIMESTAMP_BEACON_INTERVAL_MS 60000 // send a GPS timestamp beacon every 10 seconds
#define BUTTON_A_PIN 7

char title[] = "Ground Pounder v???";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

char elapsedBuf[ELAPSED_BUF_LEN];
long lastMillis = 0;

int canary = 42; // checking to see if this is getting clobbered by something else;
int receivedPacketCount = 0;
int loggedPacketCount = 0;
int lastRssi = 0;

// printableBuf is chars received over LoRa that are printable
char printableBuf[PRINTABLE_BUF_LEN];
int printableBufCount = 0;

// rawBuf is chars received over LoRa that are printable (or hexified into printable)
char rawBuf[RAW_BUF_LEN];
int rawBufCount = 0;

char tmpBuf[TMP_BUF_LEN];

char serialBuf[SERIAL_BUF_LEN];
int serialBufCount = 0;

char loraLogFileName[LORA_LOG_FILE_NAME_LEN];
char ruler[PRINTABLE_BUF_LEN];

#define GPS_RX_BUF_LEN 256 // 256 should be plenty, right?
char gpsRxBuf[GPS_RX_BUF_LEN];
int gpsRxBufCount = 0; // pointer for the next char into gpsRxBuf
int gpsFixState = 0; // 0 = No info (gps not there?), 1 = GPS told is no fix, 2 = 2-D fix, 3 = 3-D fix
int lastGpsFixState = gpsFixState;
char latBuf[TMP_BUF_LEN];
char lonBuf[TMP_BUF_LEN];

int gpsFixQuality = -1;
char gpsFixQualityBuf[22];
#define GPS_TIME_BUF_LEN 32
char gpsTimeBuf[GPS_TIME_BUF_LEN];
long nextTimestampBeaconMs = TIMESTAMP_BEACON_INTERVAL_MS;

// Support multiple plans for what is displayed on the screen.  Cycle thorugh them over time and/or via a button.
#define SCREEN_CHANGE_INTERVAL_MS 5000
#define SCREEN_DEFAULT 0
#define SCREEN_GPS 1
#define NUMBER_OF_SCREENS 2
int currentScreen = SCREEN_DEFAULT;
long nextScreenChangeMs = SCREEN_CHANGE_INTERVAL_MS;

// Set up a "good" flag for each subsystem, to keep track of which ones initialized okay.
boolean goodSerial  = false;
boolean goodSD      = false;
boolean goodDisplay = false;
boolean goodLoRa    = false;
boolean goodGPSRx   = false; // GPS receiver expected on Serial1


void setup() {
  // Set up each subsystem
  setupTitle();
  goodDisplay = setupDisplay();
  goodSerial  = setupSerial();
  goodLoRa    = setupLoRa();
  goodSD      = setupSD();
  goodGPSRx   = setupGPSRx();
  setupLED();
  setupRuler();
  nextScreenChangeMs = SCREEN_CHANGE_INTERVAL_MS;
  setupButton();
}


void setupTitle() {
  for(int j = 0; j < strlen(title); j++) {
    if (title[j] == '?') {
      sprintf(&title[j], "%d", VERSION);
      break;
    }
  }
}

void setupRuler() {
  if (PRINT_RULER) {
    int j = 0;
    for(j = 0; j < PRINTABLE_BUF_LEN - 2; j++) {
      ruler[j] = '0' + j % 10;
    }
    ruler[j++] = '<';
    ruler[j] = (char)0;
    printlnStr("Ruler:");
    printlnStr(ruler);
  }
}


boolean setupDisplay() {
  boolean good = false;
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
  } else {
    good = true;
    // Show initial display buffer contents on the screen --
    // the library initializes this with an Adafruit splash screen.
    // Clear the buffer
    display.clearDisplay();
    display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(0, 0);     // Start at top-left corner
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    //                 123456789012345678901
    display.print(F("   "));
    display.println(title);
    display.println(F("Logs LoRa packets"));
    display.println(F("  to SD card"));
    display.println(F("    by David A. Rush"));
    display.println(F("5 sec delay..."));
    display.display();
  }
  delay(5000);
  return good;
}


boolean setupSerial() {
  boolean good = false;  // return value
  Serial.begin(9600);
  delay(1000); // sometimes it takes a bit of time for the Serial port to come alive
  if (Serial) {
    good = true;
    Serial.print(title); Serial.println(" starting...");
  }
  return good;
}


boolean setupLoRa() {
  boolean result = false;
  strcpy(printableBuf, "nothing yet");
  if (!LoRa.begin(915E6)) {
    printlnStr("Starting LoRa failed!");
    result = false;
  } else {
    result = true;
  }
  return result;
}


void setupLED() {
  pinMode(LED_BUILTIN, OUTPUT);
}


boolean setupGPSRx() {
  boolean result = false;
  GPSRx.begin(9600);
  long timeout = millis() + SERIAL_INIT_TIMEOUT_MS;
  while (!GPSRx && millis() < timeout) {
    delay(50);
  }
  if (GPSRx) {
    result = true;
  }
  strcpy(lonBuf, "Lat: ?");
  strcpy(latBuf, "Lon: ?");
  strcpy(gpsFixQualityBuf, "Qual: ?");
  return result;
}


void setupButton() {
  pinMode(BUTTON_A_PIN, INPUT);  
}


void loop() {
  loopLED();  // do the distinctive blink pattern
  if (loopLoRa()) {
    // we got a new packet
    logSD(rawBuf);  // rawBuf holds printable text, but unprintables replaced by [hex codes]
    if (goodSerial) {
      printRuler();
      // Serial.println(printableBuf);
      Serial.println(rawBuf);
    }
    updateDisplay();
    if (receivedPacketCount > 1000000) {
      printlnInt("Got crazy receivedPacketCount = ", receivedPacketCount);
      printlnStr2("tmpBuf = ", tmpBuf);
      printlnInt("rawBufCount = ", rawBufCount);
      printlnStr2("rawBuf = ", rawBuf);
      goodSerial = false; // stop serial logging
    }
  } else {
    // we didn't get a new packet
    if (millis() > lastMillis + DISPLAY_UPDATE_INTERVAL_MS) {
      updateDisplay();
    }
  }
  if (loopGPSRx()) {
    processGPSRx();
  }
  loopButton();
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
          printInt("rawBufCount = ", rawBufCount);
          printInt(", RAW_BUF_LEN = ", RAW_BUF_LEN);
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


void printCurrentState(int d, char c, int count) {
  Serial.print("start: d = ");
  Serial.print(d);
  Serial.print(", c = '");
  Serial.print(c);
  Serial.print("', count = ");
  Serial.print(count);
  Serial.print(", rawBufCount = ");
  Serial.print(rawBufCount);
  Serial.println();
  Serial.println(ruler);
  Serial.println(rawBuf);
}


void updateDisplay() {
  if (goodDisplay) {
    // display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
    /*
     *   123456789012345678901
     * 1 GroundPounder * 3
     * 2 GPS 3 LORA-123.LOG
     * 3 S-000 R0000 L0000
     * 4 Run time HHH:MM:SS.T
     * 5 FlyBoy s/x/y/z 3647,1
     * 6 446,4332,-6898
     * 7
     * 8
     */
    display.clearDisplay();
    display.setCursor(0,0);
    if (millis() % 1000 < 500) {
      display.print("* "); display.println(title);
    } else {
      display.print("  "); display.print(title); display.println(" *");
    }
    int screen = currentScreen % NUMBER_OF_SCREENS;
    if (screen == SCREEN_DEFAULT) {
      snprintf(tmpBuf, TMP_BUF_LEN, "GPS %d %s", gpsFixState, loraLogFileName);
      display.println(tmpBuf);
      // 123456789012345678901
      // Sig -999 Rec 1000
      snprintf(tmpBuf, TMP_BUF_LEN, "Sig %d Rec %d pkts", lastRssi, receivedPacketCount);
      display.println(tmpBuf);
      /*
      display.print("S");
      display.print(lastRssi);
      display.print(" R");
      display.print(receivedPacketCount);
      display.print(" L");
      display.println(loggedPacketCount);
      */
      if (false) {
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
      }
      display.println(elapsedMsg(millis()));
      if (printableBufCount <= DISPLAY_BUF_LEN) {
        display.print(printableBuf);
      } else {
        strncpy(tmpBuf, printableBuf, min(min(DISPLAY_BUF_LEN,PRINTABLE_BUF_LEN),TMP_BUF_LEN));
        display.print(tmpBuf);
      }
    } else if (screen == SCREEN_GPS) {
      if (goodLoRa) {
        display.println("LoRa: Good");
      } else {
        display.println("LoRa: BAD!!!");
      }
      snprintf(tmpBuf, TMP_BUF_LEN, "GPS Status\nFix state %d\n", gpsFixState);
      display.println(tmpBuf);
      display.println(gpsFixQualityBuf);
      display.println(lonBuf);
      display.println(latBuf);
      display.println(gpsTimeBuf);
    }
    display.display();
    if (millis() > nextScreenChangeMs) {
      currentScreen++;
      nextScreenChangeMs = millis() + SCREEN_CHANGE_INTERVAL_MS;
    }
  }
  lastMillis = millis();
}


char* elapsedMsg(long ms) {
  int hours = ms / MS_PER_HOUR;
  ms -= hours * MS_PER_HOUR;
  int minutes = ms / MS_PER_MINUTE;
  ms -= minutes * MS_PER_MINUTE;
  int seconds = ms / 1000;
  ms -= seconds * 1000;
  sprintf(elapsedBuf, "Run time %d:%02d:%02d.%1d", hours, minutes, seconds, ms/100);
  return elapsedBuf;
}

/*
void serPrint(char c) {
  if (goodSerial) {
    Serial.print(c);
  }
}

void serPrint(int n) {
  if (goodSerial) {
    Serial.print(n);
  }
}

void serPrintln(int n) {
  if (goodSerial) {
    Serial.println(n);
  }
}
void serPrint(char *msg) {
  if (goodSerial) {
    Serial.print(msg);
  }
}

void serPrintln(const char *msg) {
  if (goodSerial) {
    Serial.println(msg);
  }
}
*/

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
  printlnStr(msg);
  return good;
}


void logSD(const char *dataString) {
  if (goodSD) {
    // open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.
    File dataFile = SD.open(loraLogFileName, FILE_WRITE);
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
        strcpy(serialBuf, "error opening:\n  ");
        strcat(serialBuf, loraLogFileName);
        Serial.println(serialBuf);
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
  sprintf(loraLogFileName, "LORA-%03d.LOG", nextIndex); // file name max size: 8.3 I think
  printlnStr2("loraLogFileName = ", loraLogFileName);
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


#define END_OF_GPS_SENTENCE 10
#define CARRIAGE_RETURN 13


boolean loopGPSRx() {
  boolean result = false;
  // Check for any new data in the Serial1 port the GPS is attached to, and if it ends with \n then return true, indicating a new string is complete and ready
  int data = GPSRx.read();
  if (data > 0) {
    if (data == END_OF_GPS_SENTENCE) { // end of a sentence
      result = true;
    } else if (data == CARRIAGE_RETURN) {
      // just ignore this
    } else {
      // add to the buffer
      if (gpsRxBufCount < GPS_RX_BUF_LEN - 1) {
        gpsRxBuf[gpsRxBufCount++] = (char)data;
        gpsRxBuf[gpsRxBufCount] = (char)0; // keep it null terminated
      }
    }
  }
  return result;
}


int gpsRxBufCountMax = 0;

#define VERBOSE_GPS false

void processGPSRx() {
  if (VERBOSE_GPS) {
    Serial.print("GPS: ");
    Serial.println(gpsRxBuf);
  }
  if (gpsRxBufCount > gpsRxBufCountMax) {
    gpsRxBufCountMax = gpsRxBufCount;
    Serial.print("gpsRxBufCountMax = "); Serial.println(gpsRxBufCountMax);
  }
  String str(gpsRxBuf);
  if (str.startsWith("$GPGSA")) {
    processGPGSA(str);
  } else if (str.startsWith("$GPGGA")) {
    processGPGGA(str);
  } else if (str.startsWith("$GPRMC")) {
    processGPRMC(str);
  } else {
    // Serial.print("Ignoring: "); Serial.println(str);
  }
  gpsRxBufCount = 0; // empty it
}


long nextGPGSAReport = 0;

void processGPGSA(String str) {
  /*    
    0123456789
    $GPGSA,A,3,32,26,25,31,03,22,,,,,,,1.60,1.30,0.93*0E
    A = Auto 2D/3D
    3 = 3D fix available, or 2 = 2D fix available, or 1 = No fix
  */
  char fix = gpsRxBuf[9];
  // Serial.print("fix = "); Serial.println(fix);
  if (fix == '3') {
    // 3-D fix available
    // Serial.println("3-D fix");
    gpsFixState = 3;
  } else if (fix == '2') {
    // 2-D fix available
    // Serial.println("2-D fix");
    gpsFixState = 2;
  } else if (fix == '1') {
    // No fix available
    // Serial.println("No fix");
    gpsFixState = 1;
  }
  if (gpsFixState != lastGpsFixState) {
    Serial.print("New gpsFixState = "); Serial.println(gpsFixState);
    lastGpsFixState = gpsFixState;
  }
}


void processGPGGA(String str) {
  // Q = Fix Quality, 0 = invalid, 1 = fix, 2 = dgps fix
  //        time         latitude  longitude    Q #  HDOP Altit   HtGeo   DGPS DGPS
  // $GPGGA,004025.000,3228.5702,N,08457.0669,W,2,06,1.22,116.4,M,-29.7,M,0000,0000*56
  int bufSize = 32;
  char a[bufSize];
  char b[bufSize];
  char c[bufSize];
  MatchState ms;
  ms.Target(gpsRxBuf);              //  time      latitude
  char matchResult = ms.Match("$GPGGA,([0-9\.]+),([0-9\.]+),([NS]),([0-9]*\.[0-9]*),([EW]),(%d),");
  if (matchResult == REGEXP_MATCHED) {
    // Serial.println("Match");
    // Latitude
    int n = 0;
    ms.GetCapture(a, n++); // gps time of day, UTC
    int m = 0;
    gpsTimeBuf[m++] = a[0];
    gpsTimeBuf[m++] = a[1];
    gpsTimeBuf[m++] = ':';
    gpsTimeBuf[m++] = a[2];
    gpsTimeBuf[m++] = a[3];
    gpsTimeBuf[m++] = ':';
    gpsTimeBuf[m++] = a[4];
    gpsTimeBuf[m++] = a[5];
    gpsTimeBuf[m++] = ' ';
    gpsTimeBuf[m++] = 'U';
    gpsTimeBuf[m++] = 'T';
    gpsTimeBuf[m++] = 'C';
    gpsTimeBuf[m++] = (char)0;
    if (millis() >= nextTimestampBeaconMs) {
      if (gpsFixQuality >= 1) {
        sendTimestampBeacon(gpsTimeBuf);
      }
      nextTimestampBeaconMs = millis() + TIMESTAMP_BEACON_INTERVAL_MS;
    }
    ms.GetCapture(a, n++); // degrees & minutes
    ms.GetCapture(b, n++); // hemisphere
    snprintf(latBuf, TMP_BUF_LEN, "Lat:  %c%c %s %s", a[0], a[1], &a[2], b);
    // Serial.println(latBuf);
    // Longitude
    ms.GetCapture(a, n++);
    ms.GetCapture(b, n++);
    snprintf(lonBuf, TMP_BUF_LEN, "Lon: %c%c%c %s %s", a[0], a[1], a[2], &a[3], b);
    // Serial.println(lonBuf);
    ms.GetCapture(a, n++); // fix quality
    if (a[0] == '0') {
      gpsFixQuality = 0;
      strcpy(gpsFixQualityBuf, "Qual: 0 = Invalid");
    } else if (a[0] == '1') {
      gpsFixQuality = 1;
      strcpy(gpsFixQualityBuf, "Qual: 1 = GPS Fix");
    } else if (a[0] == '2') {
      gpsFixQuality = 2;
      strcpy(gpsFixQualityBuf, "Qual: 2 = DGPS Fix");
    } else {
      gpsFixQuality = -1;
      strcpy(gpsFixQualityBuf, "Qual: Unknown");
    }
  } else {
    // Serial.print("No match: "); Serial.println(gpsRxBuf);
  }
  // Serial.println(str);
}


void processGPRMC(String str) {
  // 
}


void sendTimestampBeacon(const char* buf) {
  snprintf(tmpBuf, TMP_BUF_LEN, "Ground Pounder timestamp beacon %s", buf);
  Serial.println(tmpBuf);
  loRaTransmit(tmpBuf);
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

void printInt(const char *str, int j) {
  if (goodSerial) {
    Serial.print(str);
    Serial.print(j);
  }
}


void printlnInt(const char *str, int j) {
  if (goodSerial) {
    Serial.print(str);
    Serial.println(j);
  }
}


void printlnStr(const char *a) {
  if (goodSerial) {
    Serial.println(a);
  }
}


void printlnStr2(const char *a, const char* b) {
  if (goodSerial) {
    Serial.print(a);
    Serial.println(b);
  }
}


void printBuf(const char* label, const char* buf, int bufCount, int bufSize) {
  if (goodSerial) {
    Serial.print(label);
    Serial.print("bufCount = ");
    Serial.print(bufCount);
    Serial.print(", bufSize = ");
    Serial.print(bufSize);
    Serial.println(", ruler and buf =");
    Serial.println(ruler);
    Serial.println(buf);
  }
}

void printRuler() {
  if (PRINT_RULER && goodSerial) {
    Serial.println(ruler);
  }
}


void loopButton() {
  // Check for button press
  if (false) {
    if (digitalRead(BUTTON_A_PIN) == LOW) {
      Serial.println("Button A is LOW");
    } else {
      Serial.println("Button A is HIGH");
    }
  }
}
