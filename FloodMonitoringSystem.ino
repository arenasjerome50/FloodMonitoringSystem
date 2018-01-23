/***************************************************
  This is an example for our Adafruit FONA Cellular Module

  Designed specifically to work with the Adafruit FONA
  ----> http://www.adafruit.com/products/1946
  ----> http://www.adafruit.com/products/1963
  ----> http://www.adafruit.com/products/2468
  ----> http://www.adafruit.com/products/2542

  These cellular modules use TTL Serial to communicate, 2 pins are
  required to interface
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  BSD license, all text above must be included in any redistribution
 ****************************************************/

/*
THIS CODE IS STILL IN PROGRESS!

Open up the serial console on the Arduino at 115200 baud to interact with FONA


This code will receive an SMS, identify the sender's phone number, and automatically send a response

For use with FONA 800 & 808, not 3G
*/

#include "Adafruit_FONA.h"

#define FONA_RX 2
#define FONA_TX 3
#define FONA_RST 4
#define SENSOR_1 8
#define SENSOR_2 9
#define SENSOR_3 10

// this is a large buffer for replies
char replybuffer[255];

// We default to using software serial. If you want to use hardware serial
// (because softserial isnt supported) comment out the following three lines 
// and uncomment the HardwareSerial line
#include <SoftwareSerial.h>
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

// Hardware serial is also possible!
//  HardwareSerial *fonaSerial = &Serial1;

// Use this for FONA 800 and 808s
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);
// Use this one for FONA 3G
//Adafruit_FONA_3G fona = Adafruit_FONA_3G(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

// status level constants
enum WaterStatus { VERY_LOW, NORMAL, WARNING, CRITICAL, ERROR_SENSOR };
// previous status
WaterStatus status = VERY_LOW;
String status_msg;

// function for getting water status from the sensors
WaterStatus getWaterStatus() {
  bool sensor1 = digitalRead(SENSOR_1);
  bool sensor2 = digitalRead(SENSOR_2);
  bool sensor3 = digitalRead(SENSOR_3);

  if ( sensor1 && sensor2 && sensor3 ) {
    status_msg = "Water level status: Very Low";
    return VERY_LOW;
  } else if ( !sensor1 && sensor2 && sensor3 ) {
    status_msg = "Water level status: Normal";
    return NORMAL;
  } else if ( !sensor1 && !sensor2 && sensor3 ) {
    status_msg = "Water level status: Warning";
    return WARNING;
  } else if ( !sensor1 && !sensor2 && !sensor3 ) {
    status_msg = "Water level status: Critical";
    return CRITICAL;
  } else {
    status_msg = "Error in getting water level, check the device sensors";
    return ERROR_SENSOR;
  }
}

void setup() {
  // initialize sensors
  pinMode(SENSOR_1, INPUT_PULLUP);
  pinMode(SENSOR_2, INPUT_PULLUP);
  pinMode(SENSOR_3, INPUT_PULLUP);
  
  while (!Serial);

  Serial.begin(9600);
  Serial.println(F("FONA SMS caller ID test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));

  // make it slow so its easy to read!
  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    while(1);
  }
  Serial.println(F("FONA is OK"));

  // Print SIM card IMEI number.
  char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!
  uint8_t imeiLen = fona.getIMEI(imei);
  if (imeiLen > 0) {
    Serial.print("SIM card IMEI: "); Serial.println(imei);
  }

  fonaSerial->print("AT+CNMI=2,1\r\n");  //set up the FONA to send a +CMTI notification when an SMS is received

  Serial.println("FONA Ready");

  WaterStatus current_status = getWaterStatus();
  if (current_status != status) {
    status = current_status;
  }

  // Enable incoming call notification.
  if(fona.callerIdNotification(true, 5)) {
    Serial.println(F("Caller id notification enabled."));
  }
  else {
    Serial.println(F("Caller id notification disabled"));
  }
}


char fonaNotificationBuffer[64];          //for notifications from the FONA
char smsBuffer[250];

void loop() {
  WaterStatus current_status = getWaterStatus();

  // compare to the previous water level if there is changes
  if (current_status != status) {
    delay(1000);
    status = current_status;
    sendSmsToSubscribers();
  }
  
  char* bufPtr = fonaNotificationBuffer;    //handy buffer pointer
  char callerIDbuffer[32];  //we'll store the SMS sender number in here
  
  if (fona.available())      //any data available from the FONA?
  {
    int slot = 0;            //this will be the slot number of the SMS
    int charCount = 0;
    //Read the notification into fonaInBuffer
    do  {
      *bufPtr = fona.read();
      Serial.write(*bufPtr);
      delay(1);
    } while ((*bufPtr++ != '\n') && (fona.available()) && (++charCount < (sizeof(fonaNotificationBuffer)-1)));
    
    //Add a terminal NULL to the notification string
    *bufPtr = 0;

    //Scan the notification string for an SMS received notification.
    //  If it's an SMS message, we'll get the slot number in 'slot'
    if (1 == sscanf(fonaNotificationBuffer, "+CMTI: " FONA_PREF_SMS_STORAGE ",%d", &slot)) {
      Serial.print("slot: "); Serial.println(slot);
      
      // Retrieve SMS sender address/phone number.
      if (! fona.getSMSSender(slot, callerIDbuffer, 31)) {
        Serial.println("Didn't find SMS message in slot!");
      }
      Serial.print(F("FROM: ")); Serial.println(callerIDbuffer);

      // Retrieve SMS value.
      uint16_t smslen;
      if (fona.readSMS(slot, smsBuffer, 250, &smslen)) { // pass in buffer and max len!
        Serial.println(smsBuffer);
      }

      if (!strcmp(smsBuffer, "GET STATUS")) {  
        //Send back an automatic response
        sendSmsToSender(callerIDbuffer);
      }

      // delete the original msg after it is processed
      //   otherwise, we will fill up all the slots
      //   and then we won't be able to receive SMS anymore
      if (fona.deleteSMS(slot)) {
        Serial.println(F("OK!"));
      } else {
        Serial.print(F("Couldn't delete SMS in slot ")); Serial.println(slot);
        fona.print(F("AT+CMGD=?\r\n"));
      }
      
    }
  }

  if(fona.incomingCallNumber(callerIDbuffer)) {
    Serial.println(F("RING!"));
    Serial.print(F("Phone Number: "));
    Serial.println(callerIDbuffer);
    delay(1000);
    if (!fona.hangUp()) {
      Serial.println(F("Can't Hang Up"));
    } else {
      Serial.println(F("Hanged Up!"));
    }
    sendSmsToSender(callerIDbuffer);
  }
}

// list the subcribers
char numbers[2][13] = {
  "09463425286",
  "09077381912"
};

void sendSmsToSubscribers() {
  char message[141];
  status_msg.toCharArray(message, 141);

  Serial.println("Sending reponse...");
  for(int i = 0; i < 2; i++) {
    if (!fona.sendSMS(numbers[i], message)) {
      Serial.println(F("Failed"));
    } else {
      Serial.println(F("Sent!"));
    }
  }
}

void sendSmsToSender(char number[]) {
  char message[141];
  status_msg.toCharArray(message, 141);
        
  Serial.println("Sending reponse...");
  if (!fona.sendSMS(number, message)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("Sent!"));
  }
}

