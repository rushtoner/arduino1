/*
  Based on GPSPassThru, by David Rush, 2021 Mar 19
  First test of pulling data from HMR2300 on Arduino MKR 1310
*/
#define ESC 0x1b
#define HMR Serial1
#define RECEIVE_BUF_SIZE 100

int n = 0;
// state 0 = warming up
// state 1 = command sent
// state 2 = just do single-shot on request
int state = 0;
char buf[100];
char receiveBuf[RECEIVE_BUF_SIZE];
int receiveBufNextChar = 0;
boolean waitingForOk = false;
long waitUntilMs = 0L;
int receiveCount = 0;

void setup() {
  // initialize serial communications and wait for port to open:
  Serial.begin(115200); // USB and serial monitor
  while (!Serial) {
    ; // wait for serial port
  }
  Serial.println("\nSerial ready.");
  HMR.begin(9600); // HMR2300, via DF0077 level converter, on Serial1 (the UART pins)
  while (!HMR) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Serial1 (HMR) ready.");
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.print("state = ");
  Serial.println(state);
}


void loop() {
  loopLED();
  loopHMR();
}


void loopLED() {
  long ms = millis() % 1000;
  if (ms < 100) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (ms < 200) {
    digitalWrite(LED_BUILTIN, LOW);
  } else if (ms < 300) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }
}

void loopHMR() {
  int data = HMR.read();
  if (data > 0) {
    if (data == 13 || receiveBufNextChar >= RECEIVE_BUF_SIZE - 1) {
      processReceiveBuf();
    } else {
      receiveBuf[receiveBufNextChar++] = (char)(data & 0x7F);
    }
  }
  switch(state) {
    case 0: // wait 3 seconds before starting
      if (millis() > 3000) {
        int samplesPerSecond = 20;
        Serial.print("Setting polling rate to ");
        Serial.println(samplesPerSecond);
        sprintf(buf, "*99R=%d\r", samplesPerSecond);
        Serial.println(buf);
        // HMR.write("*99R=40\r");
        HMR.write(buf);
        state++;
        waitingForOk = true;
      }
      break;
    case 1:
      // waiting for OK
      if (!waitingForOk) {
        state++;
      }
      break;
    case 2:
      // poll for X/Y/Z
      // HMR.write("*99P\r");
      HMR.write("*99C\r"); // continuous until ESC
      state++;
      waitUntilMs = millis() + 100000L; // wait 10 seconds
      break;
    case 3: // wait for waitUntilMs has passed
      if (millis() >= waitUntilMs) {
        state++;
      }
      break;
    case 4:  // cancel continuous
      HMR.write(ESC);
      HMR.write("\n");
      Serial.write("Stopped.\n");
      state++;
      break;
    case 5: // do nothing
      break;
  }

      /*
  if (state == 0 && millis() > 3000) {
    Serial.println("Starting continuous...");
    HMR.write("*99R=010\r"); // request 10 Hz rate
    state++;
  }
    HMR.write("*99C\r");    // request continuous sending
    state++;
  }
  
  if (state == 1 && millis() > 5000) {
    Serial.write("\nStopping...\n");
    HMR.write(ESC);
    HMR.write("\n");
    Serial.write("\nStopped.\n");
    state++;
  }
  
  if (state == 2) {
    Serial.println("Polling...");
    HMR.write("*99P\r"); // P for Polled
    state++;
  }
  
  if (state == 3) {
    sprintf(buf, "state = %d\n", state);
    Serial.print(buf);
    state++;
  }
  */
}

void waitForResponse() {
  boolean done = false;
  while (!done) {
    int data = HMR.read();
    if (data > 0) {
      if (data == 13 || receiveBufNextChar >= RECEIVE_BUF_SIZE - 1) {
        processReceiveBuf();
        done = true;
      } else {
        receiveBuf[receiveBufNextChar++] = (char)(data & 0x7F);
      }
    }
  }
}

void processReceiveBuf() {
  receiveBuf[receiveBufNextChar++] = (char)0; // null terminate the string
  Serial.print("receiveBuf = ");
  Serial.println(receiveBuf);
  receiveCount++;
  Serial.print("receiveCount = ");
  Serial.println(receiveCount);
  receiveBufNextChar = 0;
  waitingForOk = false;
}
