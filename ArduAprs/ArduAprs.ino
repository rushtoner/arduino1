/*
  Based on GPSPassThru
  Expects KISS device on Serial1.
  by David A. Rush, 2021 Apr 16
*/

#define SERIAL_INIT_TIMEOUT_MS 3000
#define APRS Serial1

// FEND = Frame End, FESC = Frame Escape, TFEND = Transposed Frame End, TFESC = Transposed Frame Escape
#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD

byte lastByte = 0; // used to process escaped FEND or FESC

// temporary buffer for short-term sprintf and such
#define TMP_BUF_LEN 512
char tmpBuf[TMP_BUF_LEN];

// Packet buffer for assembling APRS radio packets
#define PKT_BUF_LEN 1500
byte pktBuf[PKT_BUF_LEN];
int pktBufCount = 0; // counter into the next available slot, 0 = empty.

boolean goodSerial = false;
boolean goodAprs = false;


void setup() {
  goodSerial = setupSerial(9600);
  goodAprs = setupAprs(9600);
  Serial.print("ArduAprs: ");
  Serial.print("goodSerial = "); Serial.print(goodSerial);
  Serial.print(", goodAprs = "); Serial.println(goodAprs);
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


void loop() {
  // The Main Loop
  if (goodAprs) {
    loopAprs();
  }
}


#define PRINT_RAW true

void loopAprs() {
  int data = Serial1.read();
  if (data >= 0) {
    // yay, we have some data to process
    byte b = (byte)(data & 0xFF);
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
}

#define ADDR_BUF_LEN 7

void processDataFrame() {
  // Start processing a data frame at the given index into the pktBuf array
  printRawPktBuf();
  // boolean doingAddress = true; // need to shift one bit right during the address parts
  boolean lastAddress = false;
  char addrBuf[ADDR_BUF_LEN];
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
  Serial.println();
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
