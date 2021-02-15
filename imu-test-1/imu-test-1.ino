#include <MKRIMU.h>
#include <Math.h>
#include <avr/dtostrf.h>

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
  int d = 100;
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(d);
  digitalWrite(LED_BUILTIN, LOW);
  delay(d);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(d);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(d);
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }
  Serial.println("IMU Initialized.");
}


float mx, my, mz;
// mx = +123.567, my = +123.567, mz = + 123.567
// char buf[100];

//            000000000011111111112222222222333333333344444444445
//            012345678901234567890123456789012345678901234567890
char buf[] = "mx = +123.567890, my = +123.567890, mz = + 123.567890          ";


int d = 250;

void loop() {
  // put your main code here, to run repeatedly:
  IMU.readMagneticField(mx, my, mz);
  // sprintf(buf, "mx = %4d, my = %4d, mz = %4d", mx, my, mz);
  if (false) {
    Serial.print("mx =   "); Serial.print(mx);
    Serial.print(", my =   "); Serial.print(my);
    Serial.print(", mz =   "); Serial.print(mz);
    Serial.println();
  }
  if (false) {
    ftoa(buf+5, mx);
    buf[16] = ',';
    ftoa(buf+23, my);
    buf[34] = ',';
    ftoa(buf+41, mz);
    Serial.println(buf);
  }
  Serial.print("Mag: micro-tesla x, y, z, magnitude, ");
  Serial.println(output(mx, my, mz));
  delay(d);
}

//             000000000011111111112222222222333333333344444444445
//             012345678901234567890123456789012345678901234567890
char buf3[] = "+123.5678, +123.5678, +123.5678, +123.5678        ";
// The string buffer 'buf3' will be re-used repeatedly by the 'output()' function below.

char* output(float x, float y, float z) {
  // Build a nice, pretty output string with x, y, and z values consistently formatted
  dtostrf(x, 9, 4, buf3 + 0); // put the X value into the string buffer
  buf3[9] = ','; // replace the null-terminator with a comma
  dtostrf(y, 9, 4, buf3 + 11); // Do the Y value
  buf3[20] = ','; // replace null-terminator
  dtostrf(z, 9, 4, buf3 + 22); // Do the Z value
  buf3[31] = ',';
  dtostrf(magnitude(x,y,z), 9, 4, buf3 + 33);
  return buf3; // return the address of the buffer
}

float magnitude(float x, float y, float z) {
  return sqrt(x*x + y*y + z*z);
}
