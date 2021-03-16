/**
 * This sketch is for testing reading of data from a MKR IMU board, displaying it on an OLED display,
 * transmitting it out as a LoRa data packet, and logging it to an SD card.
 * Built from example code for the various subsystems.
 * David A. Rush, 2021 Mar 15.
 */

#include <MKRIMU.h>
#include <Math.h>
#include <avr/dtostrf.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LoRa.h>
#include <SD.h>

#define DISPLAY_WIDTH 128 // OLED display width, in pixels
#define DISPLAY_HEIGHT 64 // OLED display height, in pixels
#define TEXT_SIZE 1
#define DISPLAY_COLS 21
#define DISPLAY_ROWS 8
#define LORA_FREQ 915E6

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define DISPLAY_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, OLED_RESET);

// Set some flags to detect if all the required subsystems are go for launch
boolean goodSerial = false;
boolean goodDisplay = false;
boolean goodLoRa = false;
boolean goodIMU = false;
boolean goodSD = false;

void setup() {
  delayWhileFlashing(10000); // 10-second start-up delay, just in case other code causes unit to sieze up.
  // put your setup code here, to run once:
  goodDisplay = setupDisplay();
  goodSerial  = setupSerial();
  goodLoRa    = setupLoRa();
  goodIMU     = setupIMU();
  // goodSD = setupSD();
  goodIMU = false; // don't run loop yet
}


boolean setupSerial() {
  boolean result = false;
  Serial.begin(9600);
  if (Serial) {
    result = true;
    Serial.println(F("Serial: good"));
    if (goodDisplay) {
      display.println(F("Serial: good"));
      display.display();
    }
  }
  return result;
}


boolean setupDisplay() {
  boolean result = display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS);
  if (result) {
    // Clear the buffer
    display.clearDisplay();
    display.setCursor(0, 0);     // Start at top-left corner
    display.setTextSize(TEXT_SIZE);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(0, 0);     // Start at top-left corner
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    display.println(F("Display: good"));
    display.display();
    if (goodSerial) {
      Serial.println(F("Display: good"));
    }
  } else {
    // print error to serial, if it's working.
    if (goodSerial) {
      Serial.println(F("SSD1306 allocation failed"));
    }
  }
  return result;
}


boolean setupLoRa() {
  boolean result = false;
  if (LoRa.begin(LORA_FREQ)) {
    result = true;
    if (goodDisplay) {
      display.println(F("LoRa: good"));
      display.display();
    }
    if (goodSerial) {
      Serial.println(F("LoRa: good"));
    }
  } else {
    if (goodDisplay) {
      display.println(F("LoRa: FAIL"));
      display.display();
    }
    if (goodSerial) {
      Serial.println(F("LoRa: FAIL"));
    }
  }
  return result;
}


boolean setupIMU() {
  boolean result = IMU.begin();
  String msg = "IMU: FAIL";
  if (result) {
    msg = "IMU: good";
  }
  if (goodSerial)
    Serial.println(msg);
  if (goodDisplay) {
    display.println(msg);
    display.display();
  }
  if (goodLoRa)
    loRaPrintln(msg);
  return result;
}


void ftoa(char *buf, float f) {
  if (f < 0.0) {
    buf[0] = '-';
  } else {
    buf[0] = '+';
  }
  f = abs(f);
  long whole = floor(f);
  long frac  = (f - whole) * 1000000;
  sprintf(buf + 1, "%3d.%06d", whole, frac);
}

void delayWhileFlashing(long delayMs) {
  pinMode(LED_BUILTIN, OUTPUT);
  const long intervalMs = 250;
  long endOfTime = millis() + delayMs;
  while (millis() < endOfTime) {
    digitalWrite(LED_BUILTIN, HIGH);  // on
    delay(intervalMs);
    digitalWrite(LED_BUILTIN, LOW);
    delay(intervalMs);
  }
}


float mx, my, mz;
// mx = +123.567, my = +123.567, mz = + 123.567
// char buf[100];

//            000000000011111111112222222222333333333344444444445
//            012345678901234567890123456789012345678901234567890
char buf[] = "mx = +123.567890, my = +123.567890, mz = + 123.567890          ";


int d = 250;

long lastIMU = 0; // keeps track of millis() timestamp when IMU was last run
long loopCounter = 0;

void loop() {
  // put your main code here, to run repeatedly:
  long now = millis();
  if (elapsedSince(lastIMU) > 1000) {
    if (goodIMU) {
      loopIMU();
      lastIMU = now;
      // Serial.print("loopCounter = ");
      // Serial.println(loopCounter);
    }
  }
  // delay(d);
  loopCounter++;
}

long elapsedSince(long when) {
  return millis() - when;
}

void loopIMU() {
  if (goodIMU) {
    display.clearDisplay();
    IMU.readMagneticField(mx, my, mz);
    Serial.print("Mag: x, y, z, m, ");
    float magni = magnitude(mx, my, mz);
    Serial.print(output(mx, my, mz, magni));
    char label[] = "Magnetometer";
    displayXYZM(0, label, mx, my, mz, magni);
    transmit("#mag", mx, my, mz);
  
    if (IMU.accelerationAvailable()) {
      IMU.readAcceleration(mx, my, mz);
      Serial.print(", Acc: ");
      float magni = magnitude(mx, my, mz);
      Serial.print(output(mx, my, mz, magni));
      char label[] = "Accelerometer";
      displayXYZM(4, label, mx, my, mz, magni);
    }
    Serial.println();
  }
}

/*
char line0[] = "012345678901234567890";
char line1[] = "x +123.567 y +123.567";
char line2[] = "z +123.567 m +123.567";
char line3[] = "012345678901234567890";
*/

// 128x64 has eight lines of 21 columns (plus null terminator).
char lines[8][22] = {"012345678901234567890","012345678901234567890","012345678901234567890","012345678901234567890"
                    ,"012345678901234567890","012345678901234567890","012345678901234567890","012345678901234567890"};

void displayXYZM(int row, char* label, float x, float y, float z, float m) {
  // display.clearDisplay();
  display.setCursor(0,row * 8);
  display.println(label);
  strcpy(lines[row+1], "x +123.567 y +123.567");
  dtostrf(x, 8, 3, lines[row+1] + 2);
  lines[row+1][10] = ' ';
  dtostrf(y, 8, 3, lines[row+1] + 13);
  display.println(lines[row+1]);
  // Serial.println(lines[row+1]);
  strcpy(lines[row+2], "z +123.567 m +123.567");
  dtostrf(z, 8, 3, lines[row+2] + 2);
  lines[row+2][10] = ' ';
  dtostrf(m, 8, 3, lines[row+2] + 13);
  display.println(lines[row+2]);
  display.display();
}

//             000000000011111111112222222222333333333344444444445
//             012345678901234567890123456789012345678901234567890
char buf3[] = "+123.5678, +123.5678, +123.5678, +123.5678        ";
// The string buffer 'buf3' will be re-used repeatedly by the 'output()' function below.

char* output(float x, float y, float z, float m) {
  // Build a nice, pretty output string with x, y, and z values consistently formatted
  dtostrf(x, 9, 4, buf3 + 0); // put the X value into the string buffer
  buf3[9] = ','; // replace the null-terminator with a comma
  dtostrf(y, 9, 4, buf3 + 11); // Do the Y value
  buf3[20] = ','; // replace null-terminator
  dtostrf(z, 9, 4, buf3 + 22); // Do the Z value
  buf3[31] = ',';
  dtostrf(m, 9, 4, buf3 + 33);
  return buf3; // return the address of the buffer
}

float magnitude(float x, float y, float z) {
  return sqrt(x*x + y*y + z*z);
}

void error(char *msg) {
  Serial.println(msg);
  display.setCursor(0,0);
  display.println("Error");
  display.println(msg);
}

#define XMIT_BUF_SIZE (DISPLAY_COLS * DISPLAY_ROWS + 1)
char xmitBuf[XMIT_BUF_SIZE];

void loRaPrintln(String str) {
  LoRa.beginPacket();
  LoRa.print(str);
  LoRa.endPacket();  
}


void transmit(char *label, double x, double y, double z) {
  // Serial.print("Label: ");
  // Serial.println(label);
  strcpy(xmitBuf, label);
  strcpy(xmitBuf + strlen(xmitBuf), " x=");
  dtostrf(x, 9, 4, xmitBuf + strlen(xmitBuf));
  strcpy(xmitBuf + strlen(xmitBuf), ", y=");
  dtostrf(y, 9, 4, xmitBuf + strlen(xmitBuf));
  strcpy(xmitBuf + strlen(xmitBuf), ", z=");
  dtostrf(z, 9, 4, xmitBuf + strlen(xmitBuf));
  // Serial.println(xmitBuf);
}
