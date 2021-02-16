/*
  Based on GPS location, reads from Serial1 (GPS?) and passes through to Serial (USB)
*/


void setup() {
  // initialize serial communications and wait for port to open:
  Serial.begin(9600);
  Serial1.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Serial connected.");

}

int n = 0;

void loop() {
  int data = Serial1.read();
  if (data > 0) {
    Serial.print((char)(data & 0x7F));
  }
}
