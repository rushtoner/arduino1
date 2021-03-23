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
int lastState = -1;
char buf[100];
char receiveBuf[RECEIVE_BUF_SIZE];
int receiveBufNextChar = 0;
boolean waitingForOk = false;
long waitUntilMs = 0L;
int receiveCount = 0;
int samplesPerSecond = 20;
long timerStart = 0;
long timerStop = 0;

void setup() {
  // initialize serial communications and wait for port to open:
  Serial.begin(19200); // USB and serial monitor
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
  if (state != lastState) {
    Serial.print("new state = ");
    Serial.println(state);
    lastState = state;
  }
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
  long a, b, elapsed;
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
        // int samplesPerSecond = 20;
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
      timerStart = millis();
      state++;
      waitUntilMs = millis() + 50000L; // wait 10 seconds
      break;
    case 3: // wait for waitUntilMs has passed
      if (millis() >= waitUntilMs) {
        state++;
        timerStop = millis();
      }
      break;
    case 4:  // cancel continuous
      HMR.write(ESC);
      HMR.write("\n");
      Serial.write("Stopped.\n");
      state++;
      break;
    case 5: // send test
      /*
      if (true) {
        a = millis();
        Serial.println("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890");
        delay(10);
        b = millis();
        elapsed = (int)(b - a);
        Serial.print("send time ");
        Serial.print(elapsed);
        Serial.println(" ms");
      }
      state++;
      */
      break;
    case 6: // do nothing
      break;
  }
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
  Serial.print("receiveBuf = \"");
  Serial.print(receiveBuf);
  Serial.println("\"");
  receiveCount++;
  if (false) {
    Serial.print("receiveCount = ");
    Serial.println(receiveCount);
  }
  receiveBufNextChar = 0;
  waitingForOk = false;
  if (state == 5) {
    Serial.println("yes");
    int elapsed = (int)(timerStop - timerStart);
    int messagesPerSec = receiveCount * 1000 / elapsed;
    sprintf(buf, "received %d messages in %d ms or %d msg/sec, samplesPerSec = %d"
      , receiveCount, elapsed, messagesPerSec, samplesPerSecond);
    Serial.println(buf);
    delay(500);
    // sprintf(buf, "That's %d messages per second", messagesPerSec);
    // Serial.println(buf);
    //Serial.println("Done.");
    state++;
  }
}
