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
#define BLINK_INTERVAL 200
#define DISPLAY_BUF_LEN (4 * 21)
#define PRINT_RULER false
#define MS_PER_MINUTE (1000 * 60)
#define MS_PER_HOUR (MS_PER_MINUTE * 60)
#define GPSRx Serial1
#define SERIAL_INIT_TIMEOUT_MS 5000 // wait up to 5 seconds for the GPSRx serial port to initialize
#define TIMESTAMP_BEACON_INTERVAL_MS 60000 // send a GPS timestamp beacon every 10 seconds

#define LORA_RX_BUF_LEN 512
char loRaRxBuf[LORA_RX_BUF_LEN];
int loRaRxBufEnd = 0; // pointer to next available position to stuff data into (the end).

char title[] = "LoRaToSerial";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int lastRssi = 0;
long lastDisplayUpdateMillis = (long)0;

char tmpBuf[TMP_BUF_LEN];


// Set up a "good" flag for each subsystem, to keep track of which ones initialized okay.
boolean goodSerial  = false;
boolean goodDisplay = false;
boolean goodLoRa    = false;


void setup() {
  // Set up each subsystem
  goodDisplay = setupDisplay();
  goodSerial  = setupSerial();
  goodLoRa    = setupLoRa();
  setupLED();
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
    display.println(title);
    display.println("Parrots what it hears over LoRa to Serial monitor");
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
    Serial.print(title); Serial.println(" listening...");
  }
  return good;
}


boolean setupLoRa() {
  boolean result = false;
  strcpy(loRaRxBuf, "nothing yet");
  if (!LoRa.begin(915E6)) {
    Serial.println("Starting LoRa failed!");
    strcpy(loRaRxBuf, "starting LoRa failed");
    result = false;
  } else {
    result = true;
  }
  return result;
}


void setupLED() {
  pinMode(LED_BUILTIN, OUTPUT);
}


void loop() {
  loopLED();  // do the distinctive blink pattern just so we know that loop() is still running well
  if (loopLoRa()) {
    // we got a new packet
    if (goodSerial) {
      Serial.println(loRaRxBuf);
    }
  }
  
  if (millis() > lastDisplayUpdateMillis + DISPLAY_UPDATE_INTERVAL_MS) {
    updateDisplay();
  }
}


boolean loopLoRa() {
  // try to parse packet, return true if got new data to display and log
  boolean result = false;
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    loRaRxBufEnd = 0; // reset to start of receive buffer
    lastRssi = LoRa.packetRssi();
    while (LoRa.available()) {
      int d = LoRa.read();
      if (isPrintable(d)) {
        if (loRaRxBufEnd < LORA_RX_BUF_LEN - 1) {
          loRaRxBuf[loRaRxBufEnd++] = (char)d;
          loRaRxBuf[loRaRxBufEnd] = (char)0; // keep it null-terminated
        }
      }

      /*
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
      */
    }
    result = true;
  }
  return result;
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
    display.print(title);
    if (millis() % 1000 < 500) {
      display.println(" ***");
    } else {
      display.println();
    }
    display.println(loRaRxBuf);
    display.display();
  }
  lastDisplayUpdateMillis = millis();
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




void loRaTransmit(String str) {
  if (goodLoRa) {
    LoRa.beginPacket();
    LoRa.print(str);
    LoRa.endPacket();
  } else {
    Serial.print("Cannot send to LoRa: "); Serial.print(str);
  }
}


/*
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
*/
