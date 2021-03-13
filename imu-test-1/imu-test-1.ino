#include <MKRIMU.h>
#include <Math.h>
#include <avr/dtostrf.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  flashLed(100);
  setupDisplay();

  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner
  if (!setupIMU()) {
    //               012345678901234567890
    display.println("IMU initialize failed");
    display.display();
    while(1); // halt
  } else {
    display.clearDisplay();
    display.println("IMU initialized.");
    display.display();
  }
}

void flashLed(int d) {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(d);
  digitalWrite(LED_BUILTIN, LOW);
  delay(d);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(d);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(d);
}

boolean setupIMU() {
  boolean result = IMU.begin();
  if (result) {
    Serial.println("IMU Initialized.");
  } else {
    Serial.println("Failed to initialize IMU!");
  }
  return result;
}

void setupDisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();

  if (false) {
    // Draw a single pixel in white
    // display.drawPixel(10, 10, SSD1306_WHITE);
  
    // Show the display buffer on the screen. You MUST call display() after
    // drawing commands to make them visible on screen!
    display.display();
    delay(2000);
  }

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.println(F("Cubesat"));
  display.println(F("Project"));
  display.println(F("Opelika High School in Opelika, Alabama"));
  //display.setCursor(0,0);
  //display.clearDisplay();
  //display.println(F("You and I in a little toy shop"));
  //display.println(F("buy a bag of balloons with the money we've got."));
  display.display();
  
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
    loopIMU();
    lastIMU = now;
    // Serial.print("loopCounter = ");
    // Serial.println(loopCounter);
  }
  // delay(d);
  loopCounter++;
}

long elapsedSince(long when) {
  return millis() - when;
}

void loopIMU() {
  display.clearDisplay();
  IMU.readMagneticField(mx, my, mz);
  Serial.print("Mag: x, y, z, m, ");
  float magni = magnitude(mx, my, mz);
  Serial.print(output(mx, my, mz, magni));
  char label[] = "Magnetometer";
  displayXYZM(0, label, mx, my, mz, magni);

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
