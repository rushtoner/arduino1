/*
  Started with ArduAprs.
  Simple web functionality test.
  by David A. Rush, 2023 Jun 01
  Goal: control on-board LED via web.
*/

#include <SPI.h>
#include <SD.h>
#include <WiFiNINA.h>
#include <avr/dtostrf.h> // for dtostrf library
#include <Regexp.h>

#define SD_SPI_CHIPSELECT 4


// temporary buffer for short-term sprintf and such
#define TMP_BUF_LEN 512
char tmpBuf[TMP_BUF_LEN];

WiFiServer webServer(80);
// WiFiClient webClient;
int wiFiStatus = WL_IDLE_STATUS;

boolean goodSerial = false;
boolean goodAprs = false;
boolean goodAprsLog = false;  // Log to SD card if found
boolean goodWiFi = false;
boolean goodWebServer = false;

void setup() {
  Serial.print("\n\nWebFrontEnd: ");
  goodWiFi = setupWiFi();
  Serial.print("goodWiFi = "); Serial.println(goodWiFi);
  if (goodWiFi) {
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    goodWebServer = setupWebServer();
  }
  Serial.print("goodWebServer = "); Serial.println(goodWebServer);
  pinMode(LED_BUILTIN, OUTPUT);
  tests();
}


void tests() {
}








boolean setupWiFi() {
  char ssid[] = "NSA Surveillance Van 2";
  char pword[] = "ihate2000walnuts";
  boolean result = false;
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WL_NO_MODULE");
  } else {
    int tries = 1;
    while (wiFiStatus != WL_CONNECTED && tries > 0) {
      Serial.print("Attempting to connect to "); Serial.println(ssid);
      wiFiStatus = WiFi.begin(ssid, pword);
      if (wiFiStatus != WL_CONNECTED) {
        delay(5000);
        tries--;
      } else {
        result = true;
      }
    }
  }
  return result;
}


boolean setupWebServer() {
  boolean result = false;
  webServer.begin();
  result = true;
  return result;
}


void loop() {
  // The Main Loop
  //if (goodAprs) {
  //  loopAprs();
  //}
  loopLed();
  if (goodWebServer) {
    loopWebServer();
  }
}

#define LED_MODE_OFF 0
#define LED_MODE_ON 1
#define LED_MODE_BLINK 2
#define LED_MODE_BLINK_FAST 3

int ledMode = LED_MODE_BLINK;

void loopLed() {
  if (ledMode == LED_MODE_ON) {
    ledOn();
  } else if (ledMode == LED_MODE_OFF) {
    ledOff();
  } else if (ledMode == LED_MODE_BLINK) {
    // blink
    long now = millis();
    if (now % 1000 < 500) {
      ledOff();
    } else {
      ledOn();
    }
  } else if (ledMode == LED_MODE_BLINK_FAST) {
    long mod = millis() % 1000;
    if (mod < 250) {
      ledOff();
    } else if (mod < 500) {
      ledOn();
    } else if (mod < 750) {
      ledOff();
    } else {
      ledOn();
    }
  }
}

void ledOn() {
  digitalWrite(LED_BUILTIN, HIGH);
}
void ledOff() {
  digitalWrite(LED_BUILTIN, LOW);
}
#define PRINT_RAW true


#define REQ_BUF_LEN 512
char reqBuf[REQ_BUF_LEN];
int reqBufCount = 0;


void loopWebServer() {
  long start = millis();
  WiFiClient webClient = webServer.available();
  if (webClient) {
    Serial.println("Web connection...");
    boolean blankLine = true; // request ends with a blank line
    // reqBufCount = 0;
    // reqBuf[reqBufCount] = 0; // empty the buffer
    String currentLine = "";
    while (webClient.connected()) {
      if (webClient.available()) {
        char c = webClient.read();
        if (c != '\r') {
          // Serial.write(c);
          if (c == '\n' && blankLine) {
            sendWebResponse(webClient);
            break;
          } else {
            // reqBuf[reqBufCount++] = c;
            // reqBuf[reqBufCount] = 0;
            // Serial.println(reqBuf);
            currentLine += c;
            if (currentLine.endsWith("GET /on")) {
              ledMode = LED_MODE_ON;
            } else if (currentLine.endsWith("GET /off")) {
              ledMode = LED_MODE_OFF;
            } else if (currentLine.endsWith("GET /blink")) {
              ledMode = LED_MODE_BLINK;
            } else if (currentLine.endsWith("GET /blinkfast")) {
              ledMode = LED_MODE_BLINK_FAST;
            }
          }
          if (c == '\n') {
            blankLine = true;
            // Serial.println("Saw a slash n");
          } else {
            blankLine = false;
          }
        }
      }
    }
    delay(5); // give web browser time to receive?  Why?
    webClient.stop();
    Serial.print("Processed web hit in "); Serial.print(millis() - start); Serial.println(" ms");
  }
}


void sendWebResponse(WiFiClient webClient) {
  Serial.println("Sending response...");
  webClient.println("HTTP/1.1 200 OK");
  webClient.println("Content-type:text/html");
  webClient.println("Connection: close"); // connection will be closed after response
  webClient.println(); // necessary blank line
  webClient.println("<!DOCTYPE html>");
  webClient.println("<head>");
  webClient.println("<title>Web Front End</title>");
  webClient.println("</head>");
  webClient.println("<body>");

  webClient.println("<p><a href=\"/on\">LED On</a></p>");
  webClient.println("<p><a href=\"/off\">LED Off</a></p>");
  webClient.println("<p><a href=\"/blink\">LED Blink</a></p>");
  webClient.println("<p><a href=\"/blinkfast\">LED Blink Fast</a></p>");
  webClient.println();
  webClient.print("<p>Hello.  millis() = ");
  webClient.println(millis());
  webClient.println("</p>");
  
  webClient.println("</body>\n</html>");
}
