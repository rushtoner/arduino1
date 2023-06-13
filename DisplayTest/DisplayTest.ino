/*
  Based on GPS location, reads from Serial1 (GPS?) and passes through to Serial (USB)
*/
#include <SPI.h>
#include <Wire.h>

// Adafruit GFX and SSD1303 for the 128x32 or 128x64 OLED displays
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
// #define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
/* added for display above */
#define DISPLAY_TEXT_SIZE 1
#define TEXT_COLUMNS 21
#define DISPLAY_UPDATE_INTERVAL_MS 100
#define TEXT_SIZE 1

#define DISPLAY_BUF_LEN 256

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

char displayBuf[DISPLAY_BUF_LEN];
long nextDisplay = 0;

void setup() {
  // Set up each subsystem
  setupDisplay(9); // displaySecs
  pinMode(A0, INPUT);
  Serial.begin(9600);
}


void loop() {
  if (millis() >= nextDisplay) {
    updateDisplay();
    nextDisplay = millis() + DISPLAY_UPDATE_INTERVAL_MS;
  }
  int val = analogRead(A0);
  Serial.println(val);
  delay(100);
}


void updateDisplay() {
    display.clearDisplay();
    display.setCursor(0,0);
    if (millis() % 1000 < 500) {
      //             123456789012345678901
      display.println("* ");
      display.println("Bothwell and\nUddingston\nMen's Shed");
    } else {
      display.println(" *");
      display.println("Bothwell and\nUddingston\nMen's Shed");
      display.println("012345678901234567890123456789");
    }
    display.println(displayBuf);
    display.display();
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
    display.println("BUMS");
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
