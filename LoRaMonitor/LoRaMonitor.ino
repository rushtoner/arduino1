#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <SD.h>

/* added for display below */
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
#define INTERVAL_MS 250
#define BUF_SIZE 100
#define SD_SPI_CHIPSELECT 4
#define DISPLAY_WIDTH 128 // OLED display width, in pixels
#define DISPLAY_HEIGHT 64 // OLED display height, in pixels
#define TEXT_SIZE 1
#define DISPLAY_COLS 21
#define DISPLAY_ROWS 8
#define LORA_FREQ 915E6


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

char elapsedBuf[BUF_SIZE];
long lastMillis = 0;
int receivedPacketCount = 0;
int lastRssi = 0;
char lastReceivedBuf[TEXT_COLUMNS * DATA_TEXT_ROWS + 1];
int lastReceivedCount = 0;
boolean useSerial = false;
boolean goodSD = false;
int loggedCount = 0;
char loraLogFileName[50];

void setup() {
  Serial.begin(9600);
  // while (!Serial);
  delay(2000); // pause to allow uploading good code over bad code
  if (Serial) {
    useSerial = true;
  }

  serPrintln("LoRa Monitor");
  setupDisplay();
  
  if (!LoRa.begin(915E6)) {
    serPrintln("Starting LoRa failed!");
    while (1);
  }
  goodSD = setupSD();
  logSD("Starting.");
}

void setupDisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    // serPrintln(F("SSD1306 allocation failed"));
    serPrintln("SSD1306 allocation failed");
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(3000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();
  display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.println(F("Cubesat Project"));
  display.println(F("Opelika High School"));
  display.println(F("in Opelika, Alabama"));
  display.println("LoRa Monitor");
  //display.setCursor(0,0);
  //display.clearDisplay();
  //display.println(F("You and I in a little toy shop"));
  //display.println(F("buy a bag of balloons with the money we've got."));
  display.display();
  reportPacketCount(receivedPacketCount, lastRssi);
}


void loop() {
  // try to parse packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    //serPrint("Rx: ");

    // read packet
    while (LoRa.available()) {
      char c = (char)LoRa.read();
      serPrint(c);
      // lastReceivedBuf[lastReceivedCount++] = '^';
      if (isPrintable(c) && lastReceivedCount < TEXT_COLUMNS * DATA_TEXT_ROWS) {
        lastReceivedBuf[lastReceivedCount++] = c;
      }
      // Serial.print((int)c);
    }
    lastReceivedBuf[lastReceivedCount++] = (char)0;
    lastRssi = LoRa.packetRssi();

    // print RSSI of packet
    if (false) {
      Serial.print(" (RSSI ");
      // Serial.println(LoRa.packetRssi());
      Serial.print(lastRssi);
      Serial.print(")");
    }
    Serial.println();
    if (goodSD) {
      logSD(lastReceivedBuf);
    }
    if (false) {
      Serial.print("lastReceivedCount: ");
      Serial.println(lastReceivedCount);
      Serial.print("Buf: ");
      Serial.println(lastReceivedBuf);
    }
    reportPacketCount(++receivedPacketCount, lastRssi);
    lastReceivedCount = 0; // reset for next time
  }
  if (millis() > lastMillis + INTERVAL_MS) {
    reportPacketCount(receivedPacketCount, lastRssi);
    lastMillis = millis();
  }
}

void reportPacketCount(int n, int rssi) {
  display.setTextSize(DISPLAY_TEXT_SIZE);      // Normal 1:1 pixel scale
  display.clearDisplay();
  display.setCursor(0,0);
  if (DISPLAY_TEXT_SIZE == 1) {
    display.print(F("File: "));
    display.println(loraLogFileName);
  } else {
    display.println(loraLogFileName);  
  }
  display.print("n = ");
  display.println(n);
  display.print("rssi = ");
  display.println(rssi);
  if (false) {
    display.print("s = ");
    display.println(millis());
  }
  if (true) {
    display.print("Logged: ");
    display.println(loggedCount);
  }
  display.println(elapsedMsg(millis()));
  if (n > 0) {
    display.println(lastReceivedBuf);
  }
  display.display();
}


char* elapsedMsg(long ms) {
  int msec = ms % 1000;
  int seconds = (ms - msec)/1000;
  int minutes = seconds / 60;
  seconds -= minutes * 60;
  // seconds = seconds % 60;
  // minutes = ((ms / 1000) - seconds) / 60;
  //if (!elapsedBuf) {
  //  elapsedBuf = (char*)malloc(BUF_SIZE);
  //}
  sprintf(elapsedBuf, "el %02d:%02d.%03d", minutes, seconds, msec);
  return elapsedBuf;
}

void serPrint(char c) {
  if (useSerial) {
    Serial.print(c);
  }
}

void serPrint(int n) {
  if (useSerial) {
    Serial.print(n);
  }
}

void serPrintln(int n) {
  if (useSerial) {
    Serial.println(n);
  }
}
void serPrint(char *msg) {
  if (useSerial) {
    Serial.print(msg);
  }
}

void serPrintln(const char *msg) {
  if (useSerial) {
    Serial.println(msg);
  }
}

boolean setupSD() {
  boolean result = false;
  String msg = "SD: FAIL";
  result = SD.begin(SD_SPI_CHIPSELECT);
  if (result) {
    msg = "SD: good";
    getNextFileName();
  }
  Serial.println(msg);
  return result;
}

boolean logSD(const char *dataString) {
  boolean result = false;
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open(loraLogFileName, FILE_WRITE);
  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
    // Serial.println(dataString);
    result = true;
    loggedCount++;
  } else {
    // if the file isn't open, pop up an error:
    Serial.println("error opening lora.log");
  }
  return result;
}

/**
 * Look at the SD card for files like LORA-1.LOG, LORA-2.LOG, etc, and choose the next numbered file name.
 */
void getNextFileName() {
  // Serial.println("getNextFileName()");
  File root = SD.open("/");
  // Serial.print("root = ");
  // Serial.println(root);
  boolean found = false;
  boolean exhausted = false;
  int n = 0;
  int nextIndex = 0; // LORA-index.LOG
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
      if (name.startsWith("LORA-")) {
        // Serial.println("starts with");
        String tmp = name.substring(5, name.indexOf(".LOG"));
        // Serial.print("tmp = \"");
        // Serial.print(tmp);
        // Serial.println("\"");
        int index = tmp.toInt();
        if (index >= nextIndex) {
          nextIndex = index + 1;
          //Serial.print("new nextIndex = ");
          //Serial.println(nextIndex);
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
  sprintf(loraLogFileName, "LORA-%03d.LOG", nextIndex); // file name max size: 8.3 I think
  Serial.print("loraLogFileName = ");
  Serial.println(loraLogFileName);
  // Serial.println("done");
}
