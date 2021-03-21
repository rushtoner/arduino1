/*
  Based on GPSPassThru, by David Rush, 2021 Mar 19
  First test of pulling data from HMR2300 on Arduino MKR 1310
*/
#define ESC 0x1b

void setup() {
  // initialize serial communications and wait for port to open:
  Serial.begin(9600); // USB and serial monitor
  Serial1.begin(9600); // HMR2300, via DF0077 level converter
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Serial connected.");
}

int n = 0;
// state 0 = warming up
// state 1 = command sent
int state = 0;

void loop() {
  int data = Serial1.read();
  if (data > 0) {
    Serial.print((char)(data & 0x7F));
    
  }
  if (state == 0 && millis() > 3000) {
    Serial.write("\nStarting...\n");
    Serial1.write("*99R=10\r");
    Serial.write("\n");
    Serial1.write("*99C\r");
    Serial.write("\n");
    state++;
  }
  if (state == 1 && millis() > 5000) {
    Serial.write("\nStopping...\n");
    Serial1.write(ESC);
    Serial1.write("\n");
    Serial.write("\nStopped.\n");
    state++;
  }
}
