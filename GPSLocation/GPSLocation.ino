/*
  GPS Location

  This sketch uses the GPS to determine the location of the board
  and prints it to the Serial monitor.

  Circuit:
   - MKR board
   - MKR GPS attached via I2C cable

  This example code is in the public domain.
*/

#include <Arduino_MKRGPS.h>

#define GPS_DEBUG_x true

void setup() {
  // initialize serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // If you are using the MKR GPS as shield, change the next line to pass
  // the GPS_MODE_SHIELD parameter to the GPS.begin(...)
  if (!GPS.begin(GPS_MODE_UART)) {
    Serial.println("Failed to initialize GPS!");
    while (1);
  } else {
    Serial.println("GPS initialized.");
  }
}

int n = 0;

void loop() {
  // check if there is new GPS data available
  if (GPS.available()) {
    // read GPS values
    float latitude   = GPS.latitude();
    float longitude  = GPS.longitude();
    float altitude   = GPS.altitude();
    float speed      = GPS.speed();
    int   satellites = GPS.satellites();

    // print GPS values
    Serial.print("Location: ");
    Serial.print(latitude, 7);
    Serial.print(", ");
    Serial.print(longitude, 7);

    Serial.print(", Altitude: ");
    Serial.print(altitude);
    Serial.print("m");

    Serial.print(", Ground speed: ");
    Serial.print(speed);
    Serial.print(" km/h");

    Serial.print(", Number of satellites: ");
    Serial.print(satellites);

    Serial.print(", n = "); Serial.println(n++);
    delay(1000);
    
  } else {
    Serial.println("No GPS data available.");
    delay(1000);
  }
}
