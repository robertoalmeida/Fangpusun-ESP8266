#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = "YouKeyBlynkProject";

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "SSID";
char pass[] = "PASSWORD";

#include <SoftwareSerial.h>
#include "config.h"

// Serial variables
#define rxPin D7
#define txPin D8                                    // TX Not used
SoftwareSerial victronSerial(rxPin, txPin);         // RX, TX Using Software Serial so we can use the hardware serial to check the ouput
// via the USB serial provided by the NodeMCU.
char receivedChars[buffsize];                       // an array to store the received data
char tempChars[buffsize];                           // an array to manipulate the received data
char recv_label[num_keywords][label_bytes]  = {0};  // {0} tells the compiler to initalize it with 0.
char recv_value[num_keywords][value_bytes]  = {0};  // That does not mean it is filled with 0's
char value[num_keywords][value_bytes]       = {0};  // The array that holds the verified data
static byte blockindex = 0;
bool new_data = false;
bool blockend = false;

BlynkTimer timer;

// This function sends Arduino's up time every second to Virtual Pin (5).
// In the app, Widget's reading frequency should be set to PUSH. This means
// that you define how often to send data to Blynk App.
void myTimerEvent()
{
  // You can send any value at any time.
  // Please don't send more that 10 values per second.
  Blynk.virtualWrite(V99, millis() / 30000);
}

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(19200);
  victronSerial.begin(19200);
  Blynk.begin(auth, ssid, pass, "blynk-cloud.com", 80);
}

void loop() {
  Blynk.run();
  // Receive information on Serial from MPPT
  RecvWithEndMarker();
  HandleNewData();

  // Just print the values every second,
  // Add your own code here to use the data.
  // Make sure to not used delay(X)s of bigger than 50ms,
  // so make use of the same principle used in PrintEverySecond()
  // or use some sort of Alarm/Timer Library
  PrintEverySecond();
}

// Serial Handling
// ---
// This block handles the serial reception of the data in a
// non blocking way. It checks the Serial line for characters and
// parses them in fields. If a block of data is send, which always ends
// with "Checksum" field, the whole block is checked and if deemed correct
// copied to the 'value' array.

void RecvWithEndMarker() {
  static byte ndx = 0;
  char endMarker = '\n';
  char rc;

  while (victronSerial.available() > 0 && new_data == false) {
    rc = victronSerial.read();
    if (rc != endMarker) {
      receivedChars[ndx] = rc;
      ndx++;
      if (ndx >= buffsize) {
        ndx = buffsize - 1;
      }
    }
    else {
      receivedChars[ndx] = '\0'; // terminate the string
      ndx = 0;
      new_data = true;
    }
    yield();
  }
}

void HandleNewData() {
  // We have gotten a field of data
  if (new_data == true) {
    //Copy it to the temp array because parseData will alter it.
    strcpy(tempChars, receivedChars);
    ParseData();
    new_data = false;
  }
}

void ParseData() {
  char * strtokIndx; // this is used by strtok() as an index
  strtokIndx = strtok(tempChars, "\t");     // get the first part - the label
  // The last field of a block is always the Checksum
  if (strcmp(strtokIndx, "Checksum") == 0) {
    blockend = true;
  }
  strcpy(recv_label[blockindex], strtokIndx); // copy it to label

  // Now get the value
  strtokIndx = strtok(NULL, "\r");    // This continues where the previous call left off until '/r'.
  if (strtokIndx != NULL) {           // We need to check here if we don't receive NULL.
    strcpy(recv_value[blockindex], strtokIndx);
  }
  blockindex++;

  if (blockend) {
    // We got a whole block into the received data.
    // Check if the data received is not corrupted.
    // Sum off all received bytes should be 0;
    byte checksum = 0;
    for (int x = 0; x < blockindex; x++) {
      // Loop over the labels and value gotten and add them.
      // Using a byte so the the % 256 is integrated.
      char *v = recv_value[x];
      char *l = recv_label[x];
      while (*v) {
        checksum += *v;
        v++;
      }
      while (*l) {
        checksum += *l;
        l++;
      }
      // Because we strip the new line(10), the carriage return(13) and
      // the horizontal tab(9) we add them here again.
      checksum += 32;
    }
    // Checksum should be 0, so if !0 we have correct data.
    if (!checksum) {
      // Since we are getting blocks that are part of a
      // keyword chain, but are not certain where it starts
      // we look for the corresponding label. This loop has a trick
      // that will start searching for the next label at the start of the last
      // hit, which should optimize it.
      int start = 0;
      for (int i = 0; i < blockindex; i++) {
        for (int j = start; (j - start) < num_keywords; j++) {
          if (strcmp(recv_label[i], keywords[j % num_keywords]) == 0) {
            // found the label, copy it to the value array
            strcpy(value[j], recv_value[i]);
            start = (j + 1) % num_keywords; // start searching the next one at this hit +1
            break;
          }
        }
      }
    }
    // Reset the block index, and make sure we clear blockend.
    blockindex = 0;
    blockend = false;
  }
}

void PrintEverySecond() {
  static unsigned long prev_millis;
  if (millis() - prev_millis > 30000) {
    PrintValues();
    prev_millis = millis();
  }
}

float DivideByThousand(float x) {
  float result;
  result = x / 1000;
  return result;
}

float DivideByHundred(float x) {
  float result;
  result = x / 100;
  return result;
}

void PrintValues() {
  String descriptioninfo[] = { "Product ID", "Firmware", "Serial Number", "Battery Voltage", "Battery Current", "Panel Voltage", "Panel Power", "State of Operation", "Errors code", "Load State", "Load Current", "Yield Total", "Yield Today", "Maximum power today", "Yield yesterday", "Maximum power yesterday", "Days of utilization", "Checksum"  };
  for (int i = 0; i < num_keywords; i++) {
    float nn; float kk;
    String varname = "V" + String(i);
    Serial.print(keywords[i]);
    Serial.print(" - ");
    Serial.print(descriptioninfo[i]);
    Serial.print(" - ");
    Serial.print(varname);
    Serial.print(": ");
    switch (i) {
      case 3:
        nn = atoi( value[i] ); kk = DivideByThousand(nn);
        Serial.println(kk);
        Blynk.virtualWrite(i, kk);
        break;
      case 4:
        nn = atoi( value[i] ); kk = DivideByThousand(nn);
        Serial.println(kk);
        Blynk.virtualWrite(i, kk);
        break;
      case 5:
        nn = atoi( value[i] ); kk = DivideByThousand(nn);
        Serial.println(kk);
        Blynk.virtualWrite(i, kk);
        break;
      case 11:
        nn = atoi( value[i] ); kk = DivideByHundred(nn);
        Serial.println(kk);
        Blynk.virtualWrite(i, kk);
        break;
      case 12:
        nn = atoi( value[i] ); kk = DivideByHundred(nn);
        Serial.println(kk);
        Blynk.virtualWrite(i, kk);
        break;
      case 15:
        nn = atoi( value[i] ); kk = DivideByHundred(nn);
        Serial.println(kk);
        Blynk.virtualWrite(i, kk);
        break;
      default:
        Serial.println(value[i]);
        Blynk.virtualWrite(i, value[i]);
        break;
    }
  }
}
