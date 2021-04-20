/*
  Based on GPSPassThru
  Expects KISS device on Serial1.
  by David A. Rush, 2021 Apr 16
  Should I rename this Trackuino???
*/

#include <SPI.h>
#include <SD.h>
#include <WiFiNINA.h>
#include <avr/dtostrf.h> // for dtostrf library
#include <Regexp.h>

#define SERIAL_INIT_TIMEOUT_MS 3000
#define APRS Serial1

// FEND = Frame End, FESC = Frame Escape, TFEND = Transposed Frame End, TFESC = Transposed Frame Escape
#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define SD_SPI_CHIPSELECT 4

byte lastByte = 0; // used to process escaped FEND or FESC

// temporary buffer for short-term sprintf and such
#define TMP_BUF_LEN 512
char tmpBuf[TMP_BUF_LEN];

// Packet buffer for assembling APRS radio packets
#define PKT_BUF_LEN 1500
byte pktBuf[PKT_BUF_LEN];
int pktBufCount = 0; // counter into the next available slot, 0 = empty.
#define LOG_FILE_NAME_LEN 13 // 8 + . + 3 + null = 13
char logFileName[LOG_FILE_NAME_LEN];
#define RAW_BUF_LEN 512
byte rawBuf[RAW_BUF_LEN];
int rawBufCount = 0;

// keep track of all the types seen
#define TYPE_IDS_SEEN_BUF_LEN 1024
byte typeIdsSeenBuf[TYPE_IDS_SEEN_BUF_LEN];
int typeIdsSeenBufCount = 0;

// Write to the log before the rawBuf is full, if it's been a while
#define APRS_LOG_WRITE_INTERVAL_MS 30000
long nextAprsLogWrite = APRS_LOG_WRITE_INTERVAL_MS * 2;

WiFiServer webServer(80);
// WiFiClient webClient;
int wiFiStatus = WL_IDLE_STATUS;

#define STATIONS_HEARD_LEN 5
#define STATIONS_HEARD_WIDTH 20 // how many chars wide to allow for a callsign-ssid WA4DAC-15 123456789
char stationsHeard[STATIONS_HEARD_LEN][STATIONS_HEARD_WIDTH];
int nextStationHeard = 0; // how many are in the circular queue?
int stationsHeardLen = 0; // tops out at STATIONS_HEARD_LEN
unsigned int totalStationsHeardCount = 0; // ever increasing (until it wraps around after 4.2 billion)

/* Packet info comma-separated strings
   Callsign-SSID max 8 chars
   counter, int plus comma, 10 + 1 = 11
   timestamp when heard (unsigned long + 1) = 10 + 1 = 11
   latitude ,1234.67N = 9
   longitude ,12345.67W = 10
   altitude, 123456
   raw packet, 30???
   adds up to 96.  Let's round it up to 100 (and include a null terminator)
*/

#define PACKETS_HEARD_QUEUE_MAX_LEN 100 // how many to remember
#define PACKETS_HEARD_MAX_WIDTH 100 // how much to remember about each one

char packetsHeard[PACKETS_HEARD_QUEUE_MAX_LEN][PACKETS_HEARD_MAX_WIDTH];
int nextPacketHeard = 0;  // Where does the next one go in the circular queue?
int packetsHeardQueueLen = 0; // how full is the circular queue?
int totalPacketsHeard = 0; // ever increasing count, serial number for each one heard


void addPacketHeard(const char *callsign, float latitude, float longitude, int altitude, const char* rawPkt) {
  // Point buf to the start of the current member of the queue, for convenience.  Has max length PACKETS_HEARD_MAX_WIDTH (minus one for null terminator)
  char *buf = packetsHeard[nextPacketHeard++];

  int len = snprintf(buf, PACKETS_HEARD_MAX_WIDTH, "%s, %d, %d, ", callsign, totalPacketsHeard++, millis());
  // Append latitude
  dtostrf(latitude, 9, 6, tmpBuf); // float to string
  strcat(buf, tmpBuf);
  strcat(buf, ", ");
  
  // append longitude
  dtostrf(longitude, 10, 6, tmpBuf);
  strcat(buf, tmpBuf);

  snprintf(tmpBuf, TMP_BUF_LEN, ", %d, ", altitude);
  if (false) {
    int tbl = strlen(tmpBuf); // tmp buf len
    int rpl = strlen(rawPkt); // raw pkt len
    len = tbl + rpl;
    if (len > PACKETS_HEARD_MAX_WIDTH -1) {
      len = PACKETS_HEARD_MAX_WIDTH - 1;
    }
    // Iterate over the rawPkt
    int n = 0;
    int m = 0;
    for(n = tbl; n < len; n++) {
      tmpBuf[n] = rawPkt[m++];
    }
    tmpBuf[n] = (char)0;
  }
  strcat(buf, tmpBuf);
  nextPacketHeard %= PACKETS_HEARD_QUEUE_MAX_LEN;
  if (packetsHeardQueueLen < PACKETS_HEARD_QUEUE_MAX_LEN) {
    packetsHeardQueueLen++;
  }
}


boolean goodSerial = false;
boolean goodAprs = false;
boolean goodAprsLog = false;  // Log to SD card if found
boolean goodWiFi = false;
boolean goodWebServer = false;

void setup() {
  Serial.print("\n\nArduAprs: ");
  goodSerial = setupSerial(9600);
  Serial.print("goodSerial = "); Serial.println(goodSerial);
  goodAprs = setupAprs(9600);
  if (goodAprs) {
    goodAprsLog = setupAprsLog();
  }
  Serial.print("goodAprs = "); Serial.println(goodAprs);
  goodWiFi = setupWiFi();
  Serial.print("goodWiFi = "); Serial.println(goodWiFi);
  if (goodWiFi) {
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    goodWebServer = setupWebServer();
  }
  Serial.print("goodWebServer = "); Serial.println(goodWebServer);
  if (goodAprsLog) {
      Serial.print("Logging to SD card: "); Serial.println(logFileName);
  } else {
    Serial.println("Not logging to SD card.");
  }
  tests();
}


void tests() {
  strcpy(tmpBuf, "3228.57N/08457.06W3/A=000415 07.9V 31C");
  float y = parseLatitude(tmpBuf);
  Serial.print("\ntests(): y = "); Serial.print(y); Serial.print(" from "); Serial.println(tmpBuf);
  strcpy(tmpBuf, "08457.06W");
  float x = parseLongitude(tmpBuf);
  Serial.print("tests(): x = "); Serial.print(x); Serial.print(" from "); Serial.println(tmpBuf);
  Serial.println();
}


boolean setupSerial(int bps) {
  // This sets up the serial monitor for output to your computer while writing and debugging the program
  boolean result = false;
  // initialize serial communications and wait for port to open:
  Serial.begin(bps); // USB and serial monitor
  long start = millis();
  long elapsed = 0L;
  while (!Serial && elapsed < SERIAL_INIT_TIMEOUT_MS) {
    // wait up to 10 seconds for serial port to connect. Needed for native USB port only.
    delay(50);
    elapsed = millis() - start;
  }
  if (Serial) {
    result = true;
    snprintf(tmpBuf, TMP_BUF_LEN, "\n\nSerial opened at %d bps", bps);
    Serial.println(tmpBuf);
  }
  return result;
}


boolean setupAprs(int bps) {
  // This sets up the serial monitor for output to your computer while writing and debugging the program
  boolean result = false;
  // initialize serial communications and wait for port to open:
  APRS.begin(bps); // USB and serial monitor
  long start = millis();
  long elapsed = 0L;
  while (!APRS && elapsed < SERIAL_INIT_TIMEOUT_MS) {
    // wait up to 10 seconds for serial port to connect. Needed for native USB port only.
    delay(50);
    elapsed = millis() - start;
  }
  if (APRS) {
    result = true;
    snprintf(tmpBuf, TMP_BUF_LEN, "APRS opened at %d bps", bps);
    Serial.println(tmpBuf);
  }
  return result;
}


boolean setupAprsLog() {
  // Set up the SD card writer for logging
  boolean good = false;
  if (SD.begin(SD_SPI_CHIPSELECT)) {
    good = true;
    getNextFileName(); // look at what filenames already exist, and create a new one at index + 1
  }
  return good;
}


void getNextFileName() {
  File root = SD.open("/");
  boolean found = false;
  boolean exhausted = false;
  int n = 0;
  int nextIndex = 0; // APRS0000.RAW
  do {
    File entry = root.openNextFile();
    if (entry) {
      // Serial.print("entry name = ");
      String name = entry.name();
      // Serial.println(name);
      entry.close();
      if (name.startsWith("APRS")) {
        String tmp = name.substring(4, name.indexOf(".RAW"));  // Tab-separated values
        int index = tmp.toInt();
        if (index >= nextIndex) {
          nextIndex = index + 1;
        }
      } else {
        //Serial.println("does not start with");
      }
    } else {
      // Serial.println("no more files");
      exhausted = true;
    }
    n++;
  } while (!found && !exhausted);
  root.close();
  sprintf(logFileName, "APRS%04d.RAW", nextIndex); // file name max size: 8.3 I think
}


void logAprsLog(const byte *data, int len) {
  if (goodAprsLog) {
    if (len > 0) {
      // open the file. note that only one file can be open at a time,
      // so you have to close this one before opening another.
      File logFile = SD.open(logFileName, FILE_WRITE);
      // if the file is available, write to it and close it:
      if (logFile) {
        logFile.write(data, len);
        logFile.close();
        rawBufCount = 0;
        snprintf(tmpBuf, TMP_BUF_LEN, "Wrote %d bytes to %s", len, logFileName);
        Serial.println(tmpBuf);
      }
    } else {
      Serial.print("Nothing to write to "); Serial.println(logFileName);
    }
    nextAprsLogWrite = millis() + APRS_LOG_WRITE_INTERVAL_MS;
  }
}


boolean setupWiFi() {
  char ssid[] = "Get off my LAN";
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
  if (goodAprs) {
    loopAprs();
  }
  if (goodWebServer) {
    loopWebServer();
  }
}


#define PRINT_RAW true

void loopAprs() {
  int data = Serial1.read();
  if (data >= 0) {
    // yay, we have some data to process
    byte b = (byte)(data & 0xFF);
    rawBuf[rawBufCount++] = b;
    if (rawBufCount >= RAW_BUF_LEN) {
      logAprsLog(rawBuf, rawBufCount);
      rawBufCount = 0; // restart the buffer
    }
    if (PRINT_RAW)
      printByte(b);
    if (lastByte == FESC) {
      if (b == TFEND) {
        pktBuf[pktBufCount++] = FEND;
      } else if (b == TFESC) {
        pktBuf[pktBufCount++] = FESC;
      } else {
        // this is an error, ignore it
      }
    } else {
      // last byte was not FEND
      if (data == FESC) {
        // process the following TFEND or TFESC the next time 'round
      } else {
        if (data == FEND) {
          // could be the start, could be the end
          if (pktBufCount == 0) {
            // do nothing, keep on truckin'
          } else {
            // Hazard a guess that it's probably the end of a packet
            if (PRINT_RAW)
              Serial.println();
            processPacket();
            pktBufCount = 0; // clear the buffer for the next packet
          }
        } else {
          // Just a regular byte of data, so stuff it into the buffer (if there's room)
          if (pktBufCount < PKT_BUF_LEN - 1) {
            pktBuf[pktBufCount++] = b;
          }
        }
      }
    }
    lastByte = b;
  } else {
    // no data to read from serial port
    if (millis() > nextAprsLogWrite) {
      logAprsLog(rawBuf, rawBufCount);
    }
  }
}


void printByte(byte b) {
  if (isPrintable(b) || b == 10 || b == 13) {
    Serial.print((char)b);
  } else {
    snprintf(tmpBuf, TMP_BUF_LEN, "[0x%02X]", b);
    Serial.print(tmpBuf);
  }
}


void processPacket() {
  int cmdCode = pktBuf[0] & 0x0F;
  // Serial.print("cmdCode = "); Serial.println(cmdCode);
  if (cmdCode == 0) {
    // KISS Command code 0x00 (in lower nybble) means data frame.  Upper nybble is port number.
    processDataFrame();
  } else {
    snprintf(tmpBuf, TMP_BUF_LEN, "Not a data frame: 0x%02X\n", cmdCode);
    Serial.print(tmpBuf); 
  }
  // ignore all other values
  Serial.println();
}

#define ADDR_BUF_LEN 7

void processDataFrame() {
  // Start processing a data frame at the given index into the pktBuf array
  printRawPktBuf();
  // First process the address field, which can be several callsign-ssid long
  // boolean doingAddress = true; // need to shift one bit right during the address parts
  boolean lastAddress = false;
  char addrBuf[ADDR_BUF_LEN];
  int addrNum = 0;
  char sourceAddr[9];
  sourceAddr[0] = (char)0;
  int n = 1; // slip the leading command code
  int addrBufCount = 0;
  while (!lastAddress) {
    int j = 0;
    addrBufCount = 0;
    for(j = 0; j < 6; j++) {
      if (n >= pktBufCount) {
        lastAddress = true;
      } else {
        if (pktBuf[n] & 0x01 == 1) {
          lastAddress = true;
        }
        char c = (char)(pktBuf[n] >> 1);
        if (c != ' ') {
          addrBuf[addrBufCount++] = c;
          if (false) {
            snprintf(tmpBuf, TMP_BUF_LEN, "addrBuf[%d] = %c", j, addrBuf[j]);
            Serial.println(tmpBuf);
          }
        }
      }
      n++;
    }
    addrBuf[addrBufCount] = 0; // null terminate it
    Serial.print("addrBuf = "); Serial.print(addrBuf);
    if (pktBuf[n] & 0x01) {
      lastAddress = true;
    }
    int ssid = (pktBuf[n++]>>1) & 0x0F;
    Serial.print(", ssid = "); Serial.print(ssid);
    Serial.print(", lastAddress = "); Serial.println(lastAddress);
    if (addrNum == 1) {
      // should be source address, which is all we care about
      strcpy(sourceAddr, addrBuf);
      if (ssid > 0) {
        snprintf(tmpBuf, TMP_BUF_LEN, "-%d", ssid);
        strcat(sourceAddr, tmpBuf);
      }
      addStationHeard(sourceAddr);
    }
    addrNum++;
  }
  Serial.print("sourceAddr: "); Serial.println(sourceAddr);
  // Addresses are done.  Next is the control field(s)
  // 3 formats of control field: I = Information frame, S = Supervisory frame, U = Unnumbered frame
  // control field is 1 or 2 octets.  Everything that I have observed seems to be 0x03 (followed by 0xF0 which I assume is the PID)
  byte cf0 = pktBuf[n++];
  if (cf0 & 0x01 == 0) {
    // bit 0 = 0, I frame
    Serial.println("I frame");
  } else if (cf0 & 0x03 == 0x01) {
    // Supervisory frame
    Serial.println("S frame");
  } else if (cf0 & 0x03 != 0x03) {
    snprintf(tmpBuf, TMP_BUF_LEN, "Unknown control field type: 0x%02X", cf0);
    Serial.println(tmpBuf);
  } else {
    // first two bits are 1, so unnumbered frame.  0x03 is what an APRS packet should be, UI
    Serial.println("Unnumbered frame");
    byte pid = pktBuf[n++];  // PID = Protocol Identifier, 0xF0 = No layer 3 protocol implemented
    snprintf(tmpBuf, TMP_BUF_LEN, "PID = 0x%02X", pid);
    Serial.println(tmpBuf);
    if (pid != 0xF0) {
      snprintf(tmpBuf, TMP_BUF_LEN, "PID = 0x%02X is not what I expect of an APRS packet.", pid);
      Serial.println(tmpBuf);
    } else {
      // So far still smells like an APRS packet.
      byte aprsDataTypeId = pktBuf[n++];
      noteTypeId(aprsDataTypeId);
      if (isPrintable(aprsDataTypeId)) {
        snprintf(tmpBuf, TMP_BUF_LEN, "aprsDataTypeId = 0x%02X = %c", aprsDataTypeId, aprsDataTypeId);
      } else {
        snprintf(tmpBuf, TMP_BUF_LEN, "aprsDataTypeId = 0x%02X = %c", aprsDataTypeId);        
      }
      Serial.println(tmpBuf);
      switch(aprsDataTypeId) {
        // case 0x1C: Serial.println("Current Mic-E Data"); break;
        // case 0x1D: Serial.println("Old Mic-E Data"); break;
        case '!':  Serial.println("Position without timestamp or Ultimeter 2000 WX Station"); processPosWoutTime(sourceAddr, pktBuf, n); break;
        // case '"':  Serial.println("Unused"); break;
        // case '#':  Serial.println("Peet Bros"); break;
        case '$':  Serial.println("Raw GPS data"); processRawGPS(sourceAddr, pktBuf, n); break;
        // case '%':  Serial.println("Agrelo DFJr/MicroFinder"); break;
        // case '&':  Serial.println("Reserved - Map Feature"); break;
        case '\'': Serial.println("Old Mic-E data or Current TM-D700"); processOldMicE(sourceAddr, pktBuf, n); break;
        // case '(':  Serial.println("Unused"); break;
        // case ')':  Serial.println("Item"); break;
        // case '*':  Serial.println("Peet Bros U-II Wx Station"); break;
        // case '+':  Serial.println("Reserved - Shelter data with time"); break;
        // case ',':  Serial.println("Invalid or test data"); break;
        // case '-':  Serial.println("Unused"); break;
        // case '.':  Serial.println("Reserved - Space Weather"); break;
        // case '/':  Serial.println("Position with timestamp (no APRS messaging"); break;
        // case ':':  Serial.println("Message"); break;
        case ';':  Serial.println("Object"); processObject(sourceAddr, pktBuf, n); break;
        // case '<':  Serial.println("Station capabilities"); break;
        case '=':  Serial.println("Position without timestamp (with APRS messaging)"); processPosWoutTime(sourceAddr, pktBuf, n); break;
        case '>':  Serial.println("Status"); processStatus(sourceAddr, pktBuf, n); break;
        // case '?':  Serial.println("Query"); break;
        case '@':  Serial.println("Position with timestamp (with APRS messaging)"); processPosWithTime(sourceAddr, pktBuf, n); break;
        // case 'T':  Serial.println("Telemetry data"); break;
        // case '[':  Serial.println("Maidenhead grid locator beacon (obsolete)"); break;
        // case '\\': Serial.println("Unused"); break;
        // case ']':  Serial.println("Unused"); break;
        // case '^':  Serial.println("Unused"); break;
        // case '_':  Serial.println("Weather report (without position)"); break;
        // case '`':  Serial.println("Current Mic-E Data (not used in TM-D700) - not sure if this is right"); break;
        // case '{':  Serial.println("User-defined APRS format"); break;
        // case '}':  Serial.println("Third-party traffic"); break;
        default:
          if (isPrintable((char)(aprsDataTypeId))) {
            snprintf(tmpBuf, TMP_BUF_LEN, "Unrecognized APRS data type ID 0x%02X = %c", aprsDataTypeId, (char)aprsDataTypeId);
          } else {
            snprintf(tmpBuf, TMP_BUF_LEN, "Unrecognized APRS data type ID 0x%02X", aprsDataTypeId);
          }
          Serial.println(tmpBuf);
      }
      for(; n < pktBufCount; n++) {
        byte data = pktBuf[n];
        if (data == 13 || data == 10 || isPrintable(data)) {
          Serial.print((char)data);
        } else if (data == FEND) {
          Serial.print("[FEND]\n");
        } else if (data == FESC) {
          Serial.print("[FESC]");
        } else if (data == TFEND) {
          Serial.print("[TFEND]");
        } else if (data == TFESC) {
          Serial.print("[TFESC]");
        } else {
          snprintf(tmpBuf, TMP_BUF_LEN, "[0x%02X]", data);
          Serial.print(tmpBuf);
        }
      }
    }
  }
  Serial.println();
}


void addStationHeard(const char *callsign) {
  // strncpy(stationsHeard[nextStationHeard++], callsign, STATIONS_HEARD_WIDTH);
  snprintf(stationsHeard[nextStationHeard++], STATIONS_HEARD_WIDTH, "%-8s %9d", callsign, totalStationsHeardCount++);
  nextStationHeard %= STATIONS_HEARD_LEN;
  if (stationsHeardLen < STATIONS_HEARD_LEN) {
    stationsHeardLen++;
  }
}


void processPosWithTime(const char* sourceAddr, const byte* buf, int n) {
  int tmpBufCount = 0;
  int j = 0;
  for(j = n; j < pktBufCount + 1; j++) {
    tmpBuf[tmpBufCount++] = (char)(pktBuf[j]);
  }
  tmpBuf[tmpBufCount++] = (char)0;
  Serial.print(sourceAddr); Serial.print(": "); Serial.println(tmpBuf);
}

#define PACKET_INFO_LEN 50
char packetInfo[PACKET_INFO_LEN];

void getPacketInfo(const byte* buf, int n) {
  int count = 0;
  int j = 0;
  for(j = n; j < pktBufCount + 1; j++) {
    packetInfo[count++] = (char)(pktBuf[j]);
  }
  packetInfo[count++] = (char)0;
}


void processPosWoutTime(const char* sourceAddr, const byte* buf, int n) {
  getPacketInfo(buf, n);
  // Should look kinda like this:
  // 012345678901234567890
  // 3228.57N/08457.06W3/A=000450 07.9V 31C #
  // Regex to the rescue?
  Serial.print("Matching: \""); Serial.print(packetInfo); Serial.println("\"");
  float y = NAN; // latitude
  float x = NAN; // longitude
  int   z = 0;   // altitude
  MatchState ms;
  ms.Target(packetInfo);
  char matchResult = ms.Match("(%d%d%d%d%.%d%d[NS])");
  if (matchResult == REGEXP_MATCHED) {
    ms.GetCapture(tmpBuf, 0);
    y = parseLatitude(tmpBuf);
  } else {
    Serial.print("processPosWoutTime(): No latitude match found in: "); Serial.println(packetInfo);
  }

  ms.Target(packetInfo);
  matchResult = ms.Match("(%d%d%d%d%d%.%d%d[EW])");
  if (matchResult == REGEXP_MATCHED) {
    ms.GetCapture(tmpBuf, 0);
    x = parseLongitude(tmpBuf);
  } else {
    Serial.print("processPosWoutTime(): No longitude match found in: "); Serial.println(packetInfo);
  }
  
  ms.Target(packetInfo);
  matchResult = ms.Match("A=(%d%d%d%d%d%d)");
  if (matchResult == REGEXP_MATCHED) {
    ms.GetCapture(tmpBuf, 0);
    z = parseAltitude(tmpBuf);
  } else {
    Serial.print("processPosWoutTime(): No altitude match found in: "); Serial.println(packetInfo);
  }
  // Serial.print(sourceAddr); Serial.print(": "); Serial.println(packetInfo);
  addPacketHeard(sourceAddr, y, x, z, packetInfo);
}


float parseLatitude(char *buf) {
  //            01234567
  // Expecting "3212.34N"
  float y = (buf[0] - '0') * 10 + buf[1] - '0';
  float mins = (buf[2] - '0') * 10 + (buf[3] - '0') + ((buf[5] - '0')/10.0) + ((buf[6] - '0')/100.0);
  // Serial.print("degrees = "); Serial.print(y); Serial.print(", minutes = "); Serial.println(mins);
  y += mins/60.0;
  if (buf[7] == 'S') {
    y *= -1.0;
  }
  Serial.print("Parsing latitude from: "); Serial.print(buf); Serial.print(" and got "); Serial.println(y);
  return y;
}



float parseLongitude(char *buf) {
  //            012345678
  // Expecting "08457.06W3/A=000395 07.9V 31C"
  float x = (buf[0] - '0') * 100 + (buf[1] - '0') * 10 + (buf[2] - '0');
  float mins = (buf[3] - '0') * 10 + (buf[4] - '0') + ((buf[6] - '0')/10.0) + ((buf[7] - '0')/100.0);
  // Serial.print("degrees = "); Serial.print(y); Serial.print(", minutes = "); Serial.println(mins);
  x += mins/60.0;
  if (buf[8] == 'W') {
    x *= -1.0;
  }
  Serial.print("Parsing longitude from: "); Serial.print(buf); Serial.print(" and got "); Serial.println(x);
  return x;
}


int parseAltitude(char *buf) {
  // Expecting 123456
  return (buf[0] - '0') * 100000 + (buf[1] - '0') * 10000 + (buf[2] - '0') * 1000 + (buf[3] - '0') * 100 + (buf[4] - '0') * 10 + (buf[5] - '0');
}


void processObject(const char* sourceAddr, const byte* buf, int n) {
  int tmpBufCount = 0;
  int j = 0;
  for(j = n; j < pktBufCount + 1; j++) {
    tmpBuf[tmpBufCount++] = (char)(pktBuf[j]);
  }
  tmpBuf[tmpBufCount++] = (char)0;
  Serial.print(sourceAddr); Serial.print(": "); Serial.println(tmpBuf);
}


void processRawGPS(const char* sourceAddr, const byte* buf, int n) {
  //             0123456789012345678901234567890
  // Expecting: "GPRMC,014256,A,3242.4527,N,08527.2847,W,000,237,200421,,*0A"

  /*
19:56:57.863 -> aprsDataTypeId = 0x24 = $
19:56:57.863 -> Raw GPS data
19:56:57.863 -> RawGPS matching: "GPRMC,015653,A,3242.4489,N,08527.2841,W,000,201,200421,,*0C"
19:56:57.863 -> processPosWoutTime(): No latitude match found in: GPRMC,015653,A,3242.4489,N,08527.2841,W,000,201,200421,,*0C
19:56:57.863 -> WB4BYQ-3: aprsDataTypeId = 0x24 = $
19:56:57.863 -> GPRMC,015653,A,3242.4489,N,08527.2841,W,000,201,200421,,*0C
19:56:57.863 -> 

   */
  getPacketInfo(buf, n);
  
  Serial.print("RawGPS matching: \""); Serial.print(packetInfo); Serial.println("\"");
  float y = NAN; // latitude
  float x = NAN; // longitude
  int   z = 0;   // altitude
  MatchState ms;
  ms.Target(packetInfo);
  char matchResult = ms.Match("GPRMC,%d+,A,(%d%d%d%d%.%d+),([NS]),(%d%d%d%d%d%.%d+),([EW])");
  if (matchResult == REGEXP_MATCHED) {
    ms.GetCapture(tmpBuf, 0); // longitude
    y = (tmpBuf[0] - '0') * 10 + (tmpBuf[1] - '0');
    float divisor = 0.1;
    for(int j = 3; j < strlen(tmpBuf); j++) {
      y += (tmpBuf[0] - '0') * divisor;
      divisor /= 10.0;
    }
    ms.GetCapture(tmpBuf, 1); // N or S?
    if (tmpBuf[0] == 'S') {
      y *= -1.0;
    }
    ms.GetCapture(tmpBuf, 2); // longitude
    x = (tmpBuf[0] - '0') * 100 + (tmpBuf[1] - '0') * 10 + (tmpBuf[2] - '0');
    divisor = 0.1;
    for(int j = 3; j < strlen(tmpBuf); j++) {
      x += (tmpBuf[0] - '0') * divisor;
      divisor /= 10.0;
    }
    ms.GetCapture(tmpBuf, 3); // E or W?
    if (tmpBuf[0] == 'W') {
      x *= -1.0;
    }
  } else {
    Serial.print("processRawGPS(): No match found in: "); Serial.println(packetInfo);
  }
  Serial.print(sourceAddr); Serial.print(": "); Serial.println(tmpBuf);
  addPacketHeard(sourceAddr, y, x, z, tmpBuf);
}


void processOldMicE(const char* sourceAddr, const byte* buf, int n) {
  int tmpBufCount = 0;
  int j = 0;
  for(j = n; j < pktBufCount + 1; j++) {
    tmpBuf[tmpBufCount++] = (char)(pktBuf[j]);
  }
  tmpBuf[tmpBufCount++] = (char)0;
  Serial.print(sourceAddr); Serial.print(": "); Serial.println(tmpBuf);
}



void processStatus(const char* sourceAddr, const byte* buf, int n) {
  int tmpBufCount = 0;
  int j = 0;
  for(j = n; j < pktBufCount + 1; j++) {
    tmpBuf[tmpBufCount++] = (char)(pktBuf[j]);
  }
  tmpBuf[tmpBufCount++] = (char)0;
  Serial.print(sourceAddr); Serial.print(": "); Serial.println(tmpBuf);
}


void printRawPktBuf() {
  snprintf(tmpBuf, TMP_BUF_LEN, "Data frame (%d): ", pktBufCount);
  Serial.print(tmpBuf);
  for(int n = 0; n < pktBufCount; n++) {
    byte data = pktBuf[n];
    if (data == 13 || data == 10 || isPrintable(data)) {
      Serial.print((char)data);
    } else {
      snprintf(tmpBuf, TMP_BUF_LEN, "[0x%02X]", data);
      Serial.print(tmpBuf);
    }
  }
  Serial.println();

  if (false) {
    boolean pastAddr = false;
    snprintf(tmpBuf, TMP_BUF_LEN, "Data frame (%d): ", pktBufCount);
    Serial.print(tmpBuf);
    for(int n = 0; n < pktBufCount; n++) {
      byte data = pktBuf[n];
      if (data == 0xF0) {
        pastAddr = true;
      }
      if (!pastAddr) {
        data>>=1;
      }
      if (data == 13 || data == 10 || isPrintable(data)) {
        Serial.print((char)data);
      } else {
        snprintf(tmpBuf, TMP_BUF_LEN, "[0x%02X]", data);
        Serial.print(tmpBuf);
      }
    }
    Serial.println();
  } 
}


void noteTypeId(byte id) {
  // see if this type ID has already been seen.  If not, add it
  boolean already = false;
  for(int j = 0; j < typeIdsSeenBufCount; j++ ) {
    if (id == typeIdsSeenBuf[j]) {
      already = true;
      break;
    }
  }
  if (already) {
    if (isPrintable((char)id)) {
      snprintf(tmpBuf, TMP_BUF_LEN, "Already saw type ID 0x%02X = %c", id, (char)id);
    } else {
      snprintf(tmpBuf, TMP_BUF_LEN, "Already saw type ID 0x%02X", id);
    }
  } else {
    if (typeIdsSeenBufCount < TYPE_IDS_SEEN_BUF_LEN) {
      typeIdsSeenBuf[typeIdsSeenBufCount++] = id;
    }
    if (isPrintable((char)id)) {
      snprintf(tmpBuf, TMP_BUF_LEN, "Already saw type ID 0x%02X = %c", id, (char)id);
    } else {
      snprintf(tmpBuf, TMP_BUF_LEN, "Already saw type ID 0x%02X", id);
    }
  }
  Serial.println(tmpBuf);
  strcpy(tmpBuf, "Seen so far: ");
  int tmpBufCount = strlen(tmpBuf);
  for(int j = 0; j < typeIdsSeenBufCount; j++) {
    if (isPrintable((char)(typeIdsSeenBuf[j]))) {
      tmpBuf[tmpBufCount++] = (char)typeIdsSeenBuf[j];
    } else {
      snprintf(&tmpBuf[tmpBufCount], TMP_BUF_LEN, "[0x%02X]", typeIdsSeenBuf[j]);
    }
  }
  tmpBuf[tmpBufCount] = (char)0;
  Serial.println(tmpBuf);
}


#define REQ_BUF_LEN 512
char reqBuf[REQ_BUF_LEN];
int reqBufCount = 0;


void loopWebServer() {
  long start = millis();
  WiFiClient webClient = webServer.available();
  if (webClient) {
    Serial.println("Web connection...");
    boolean blankLine = true; // request ends with a blank line
    while (webClient.connected()) {
      if (webClient.available()) {
        char c = webClient.read();
        if (c != '\r') {
          // Serial.write(c);
          if (c == '\n' && blankLine) {
            sendWebResponse(webClient);
            break;
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
  webClient.println("Content-type:text/plain");
  webClient.println("Connection: close"); // connection will be closed after response
  webClient.println();
  webClient.print("Hello.\nmillis() = ");
  webClient.println(millis());

  if (true) {
    webClient.println();
    webClient.println("          111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999");
    webClient.println("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
    webClient.println("Packets Heard, newest first:");
    // newest first
    int n = nextPacketHeard - 1;
    if (n < 0) {
      n = PACKETS_HEARD_QUEUE_MAX_LEN - 1; // wrap around
    }
    // n should be pointed at the oldest one now
    for(int j = 0; j < packetsHeardQueueLen; j++) {
      webClient.println(packetsHeard[(PACKETS_HEARD_QUEUE_MAX_LEN - j + n) % PACKETS_HEARD_QUEUE_MAX_LEN]);
    }
  }
  
  if (false) {
    webClient.println("\nStations Heard, oldest first:");
    // oldest first
    int n = nextStationHeard % stationsHeardLen;
    //if (n > stationsHeardLen) {
    //  n = 0; // wrap around
    //}
    // n should be pointed at the oldest one now
    for(int j = 0; j < stationsHeardLen; j++) {
      webClient.println(stationsHeard[(j + n) % STATIONS_HEARD_LEN]);
    }
  }
  
  if (true) {
    webClient.println("\nStations Heard, newest first:");
    // newest first
    int n = nextStationHeard - 1;
    if (n < 0) {
      n = STATIONS_HEARD_LEN - 1; // wrap around
    }
    // n should be pointed at the oldest one now
    for(int j = 0; j < stationsHeardLen; j++) {
      webClient.println(stationsHeard[(STATIONS_HEARD_LEN - j + n) % STATIONS_HEARD_LEN]);
    }
  }
  webClient.println();
}
