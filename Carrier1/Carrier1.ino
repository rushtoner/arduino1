/*
  First test sketch of MKR IoT Carrier board.
*/
#include <Arduino_MKRIoTCarrier.h>
MKRIoTCarrier carrier;

// #include <LoRa.h>
// #include <Wire.h>
// #include <Regexp.h> // Library by Nick Gammon for regular expressions

#define LORA_FREQ 915E6
// ESC is sent to the HMR2300 in order to stop the unit from sending data continuously
#define ESC 0x1b
#define SERIAL_INIT_TIMEOUT_MS 5000
#define LED_BLINK_INTERVAL_MS 250

// HMR is on the 2nd UART accessible via pins D14 (TX) and D13 (RX) on the MKR 1310
#define HMR Serial1

float temperature = 0;
float humidity = 0;
float pressure = 0;

boolean goodSerial = false;
boolean goodCarrier = false;

void setup() {
  goodSerial = setupSerial();
  goodCarrier = setupCarrier();
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


boolean setupCarrier() {
  boolean result = true;
  CARRIER_CASE = false;
  carrier.begin();
  return result;
}



void setupLED() {
  // set up a distinctive blink pattern
  pinMode(LED_BUILTIN, OUTPUT);
}


/* ******************************************************************************** */


void loop() {
  loopLED(); // blink the LED as proof-of-life
  loopCarrier();
}


void loopLED() {
  long ms = millis() % 2000;
  if (ms < LED_BLINK_INTERVAL_MS) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (ms < 2 * LED_BLINK_INTERVAL_MS) {
    digitalWrite(LED_BUILTIN, LOW);
  } else if (ms < 3 * LED_BLINK_INTERVAL_MS) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}

void loopCarrier() {
  temperature = carrier.Env.readTemperature(); //reads temperature
  humidity = carrier.Env.readHumidity(); //reads humidity
  pressure = carrier.Pressure.readPressure(); //reads pressure

  /*
  //reads IMU sensor  
  carrier.IMUmodule.readGyroscope(gyroscope_x, gyroscope_y, gyroscope_z); //read gyroscope (x, y z)
  carrier.IMUmodule.readAcceleration(accelerometer_x, accelerometer_y, accelerometer_z); //read accelerometer (x, y z)
  
  //reads ambient light
  if (carrier.Light.colorAvailable()) {
    carrier.Light.readColor(none, none, none, light);
  }

  //updates state of all buttons
  carrier.Buttons.update();

  //checks if button 1 has been pressed
  if (carrier.Button1.onTouchDown()) { 
      //code here
   }
  */

  /*
  carrier.leds.setPixelColor(pixel, g , r , b); //sets pixels. e.g. 3, 255, 0 , 255
  carrier.leds.show(); //displays pixels

  carrier.Relay1.open(); //opens relay 1
  carrier.Relay1.close(); //closes relay 1

  carrier.Buzzer.sound(500); //sets frequency of buzzer
  delay(1000); //sets duration for sounds
  carrier.Buzzer.noSound(); //stops buzzer
  */

  //controlling screen
  carrier.display.fillScreen(ST77XX_RED); //red background
  carrier.display.setTextColor(ST77XX_WHITE); //white text
  carrier.display.setTextSize(2); //medium sized text

  carrier.display.setCursor(20, 110); //sets position for printing (x and y)
  carrier.display.print("Temp: "); //prints text
  carrier.display.print(temperature); //prints a variable
  carrier.display.println(" C"); //prints text
  carrier.display.print("Humid: "); carrier.display.print(humidity); carrier.display.println(" %");
  carrier.display.print("Pressure: "); carrier.display.print(pressure); carrier.display.println(" ?");
  
}
