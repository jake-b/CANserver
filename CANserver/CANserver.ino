/*   CAN Server by Josh Wardell
 *   http://www.jwardell.com/canserver/
 *   To be used with microDisplay
 *
 *   Jun 15 2020
 *
 *   Board: Node32s
 *   (must press IO0 right button to start programming)
 */

#include "WiFi.h"   //esp32
#include "ESPAsyncWebServer.h" //https://github.com/me-no-dev/ESPAsyncWebServer
#include <esp32_can.h>  //RX GPIO16 TX GPIO 17 https://github.com/collin80/esp32_can
#include <ArduinoOTA.h>
#include "PandaUDP.h"

#define LED1 1    //shared with serial tx - try not to use
#define LED2 2    //onboard blue LED
#define CFG1 15   //jumper to toggle second CANserver
#define CFG2 4    //future

#define BITRATE 500000  //CAN bitrate, Tesla=500000

// access point network credentials - don't change these
const char* ssid = "CANserver";
const char* password = "JWcanServer2020";

int testcounter = 100;

static int BattVolts = 0;     //ID 132 byte 0+1 scale .01 V
static int BattAmps = 0;      //ID 132 byte 2+3 scale -.1 offset 0 A
static int BattPower = 0;         //V*A
static int RearTorque = 0;     //ID 1D8 startbit 24 signed13 scale 0.25 NM
static int FrontTorque = 0;     //ID 1D8 startbit 24 signed13 scale 0.25 NM
static int MinBattTemp = 0;   //ID 312 SB 44 u9 scale .25 offset -25 C
static int BattCoolantRate = 0;  //ID 241 SB 0 u9 scale .01 LPM
static int PTCoolantRate = 0;    //ID 241 SB 22 u9 scale .01 LPM
static int MaxRegen = 0;      //ID 252 Bytes 0+1 scale .01 kW
static int MaxDisChg = 0;     //ID 252 Bytes 2+3 scale .01 kW
static int VehSpeed = 0;     //ID 257 SB 12 u12 scale .08 offset -40 KPH
static int v12v261 = 0;          //ID 261 SB0 u12 scale 0.005444 V
static int BattCoolantTemp = 0;  //ID 321 SB0 u10 scale 0.125 offset -40 C
static int PTCoolantTemp = 0;    //ID 321 SB10 u10 scale 0.125 offset -40 C
static int BattRemainKWh = 0;    //ID 352 byte 1/2 scale .1 kWh
static int BattFullKWh = 0;      //ID 352 byte 0/1 scale .1 kWh
static int InvHStemp376 = 0;     //ID 376 Byte 1 scale .5 offset -20 C
static int BSR = 0;
static int BSL = 0;
static int brightness = 4;       //LED brightness
static int DisplayOn = 1;       //to turn off displays if center screen is off

int disp0mode;
int disp1mode;
int disp2mode;
String disp0str;    //commands to send to first display
String disp1str;    //commands to send to second display
String disp2str;    //commands to send to third display

String inString = "";    // string to hold input
int serialinput;    // string to hold input
unsigned long previouscycle = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create Panda UDP server
PandaUDP panda;

void setup() {

  //pinMode(LED1,OUTPUT); // LED1 shares TXpin with serial
  pinMode(LED2, OUTPUT);
  pinMode(CFG1, INPUT_PULLUP);
  pinMode(CFG2, INPUT_PULLUP);

  if (digitalRead(CFG1) == 0) { //If jumpered, server 2
    ssid = "CANserver2";
  }

  Serial.begin(57600);  //comment out serial to use LED1
  delay(200);
  Serial.println();

  CAN0.begin(BITRATE);

  // Setting the ESP as an access point
  Serial.print("Setting AP (Access Point)…");
  // Remove the password parameter, if you want the AP (Access Point) to be open
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // set up ArduinoOTA
  ArduinoOTA.setHostname(ssid); // Same as SSID
  ArduinoOTA.setPassword(password);

  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA: Start\n");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("ArduinoOTA: End\n");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    Serial.println("\n");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready");

  // set up servers for displays
  server.on("/disp0", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", disp0str);
  });
  server.on("/disp1", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", disp1str);
  });
  server.on("/disp2", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", disp2str);
  });

  //receive posts of display buttons, TODO do something with the buttons
  server.on("/post0", HTTP_POST, [](AsyncWebServerRequest * request) {}, NULL,
  [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.print("POST0: ");
    for (size_t i = 0; i < len; i++) {
      Serial.write(data[i]);
    }
    Serial.println();
    request->send(200);
  });
  server.on("/post1", HTTP_POST, [](AsyncWebServerRequest * request) {}, NULL,
  [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.print("POST1: ");
    for (size_t i = 0; i < len; i++) {
      Serial.write(data[i]);
    }
    Serial.println();
    request->send(200);
  });
  server.on("/post2", HTTP_POST, [](AsyncWebServerRequest * request) {}, NULL,
  [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.print("POST2: ");
    for (size_t i = 0; i < len; i++) {
      Serial.write(data[i]);
    }
    Serial.println();
    request->send(200);
  });
  // Start server
  server.begin();

  // Begin Panda UDP server
  panda.begin();

//  CAN0.watchFor(0x100, 0xF00); //setup a special filter
  CAN0.watchFor(); //then let everything else through anyway
//  CAN0.setCallback(0, gotHundred); //callback on that first special filter
}

void loop() {
  ArduinoOTA.handle();

  if (Serial) {
    long currentMillis = millis();
    if (currentMillis - previouscycle >= 500) { //Every 500ms
      previouscycle = currentMillis;
      digitalWrite(LED2, !digitalRead(LED2)); //flash LED for awake
      //digitalWrite(LED1, !digitalRead(LED1)); //debug LED1
      Serial.print("BattAmps(a):"); //print data to serial for debug
      Serial.print(BattAmps);
      Serial.print(" BattVolts(b):");
      Serial.print(BattVolts);
      Serial.print(" BattPower(c):");
      Serial.print(BattPower);
      Serial.print(" RearTorque(d):");
      Serial.print(RearTorque);
      Serial.print(" MinBattTemp(e):");
      Serial.print(MinBattTemp);
      Serial.print(" MaxRegen(f):");
      Serial.print(MaxRegen);
      Serial.print(" MaxDisChg(g):");
      Serial.print(MaxDisChg);
      Serial.print(" VehSpeed(h):");
      Serial.print(VehSpeed);
      Serial.print(" BSR(i):");
      Serial.print(BSR);
      Serial.print(" BSL(j):");
      Serial.print(BSL);
      Serial.print(digitalRead(CFG1));
      Serial.print(digitalRead(CFG2));
      Serial.println();
    }
  }//if serial
  if (Serial.available() > 0) {
    int inChar = Serial.read();   //read sample data for debug
    switch (inChar) {
    case 'a':
      BattAmps = inString.toInt();
      inString = "";
      break;

    case 'b':
      BattVolts = inString.toInt();
      inString = "";
      break;

    case 'c':
      BattPower = inString.toInt();
      inString = "";
      break;

    case 'd':
      RearTorque = inString.toInt();
      inString = "";
      break;

    case 'e':
      MinBattTemp = inString.toInt();
      inString = "";
      break;

    case 'f':
      MaxRegen = inString.toInt();
      inString = "";
      break;

    case 'g':
      MaxDisChg = inString.toInt();
      inString = "";
      break;

    case 'h':
      VehSpeed = inString.toInt();
      inString = "";
      break;

    case 'i':
      BSR = inString.toInt();
      inString = "";
      break;

    case 'j':
      BSL = inString.toInt();
      inString = "";
      break;

    case '\n':
      serialinput = inString.toInt(); //save off input
      inString = "";
      break;

    default:
      inString += (char)inChar;
      break;
    }
  } //serial available

  if (DisplayOn == 1) {
    //set up display commands from data, see microDisplay command reference
    disp0mode = 0;
    /////disp0str = "-901vFDu0m11l"; //for display test
    disp0str = String(BattPower) + "vWK  Bu" + String(int(0.008 * BattPower)) + "b" + String(disp0mode) + "m" + "120r";
    disp1str = String(RearTorque) + "vMNu" + String(int(0.006 * RearTorque)) + "b" + "0m120r";
    disp2str = String(int(0.621371 * VehSpeed)) + "vHPMu" + String(int(VehSpeed / 20)) + "b0m  TEaST  d2x120r";

  } else if (DisplayOn == 0) {//turn all displays black if car screen is off
    disp0str = "1m t1000r"; //text mode black space refresh 1sec
    disp1str = "1m t1000r"; //text mode black space refresh 1sec
    disp2str = "1m t1000r"; //text mode black space refresh 1sec
  }


  CAN_FRAME message;
  if (CAN0.read(message)) {
    /*  Serial.print(message.id, HEX);  ///debug display RX message
        if (message.extended) Serial.print(" X ");
        else Serial.print(" S ");
        Serial.print(message.length, DEC);
        for (int i = 0; i < message.length; i++) {
          Serial.print(message.data.byte[i], HEX);
          Serial.print(" ");
        }
        Serial.println();
    */
    // Send to UDP server if a client is connected.
    panda.handleMessage(message);

    digitalWrite(LED2, !digitalRead(LED2)); //flash LED2 to show data Rx
    switch (message.id)
    {
    case 0x00C:
      if (message.length == 8) {
        DisplayOn = (message.data.byte[0] & 0x0020) >> 5;  //SG_ UI_displayOn : 5|1@1+ (1,0) [0|1] ""
      }
      break;

    case 0x132:
      if (message.length == 8) {
        int tempvolts;
        tempvolts = 0.01 * float((message.data.byte[1] * 256) + message.data.byte[0]);
        if ((tempvolts > 290) && (tempvolts < 420)) { //avoid some bad messages
          BattVolts = tempvolts;
          BattAmps = (((message.data.byte[3] & 0x003F) << 8) + (message.data.byte[2])); //signed 15, mask off sign
          if (BattAmps > 8190) {                 //15-bit signed conversion 16384/2
            BattAmps = 0 - (16384 - BattAmps);
          }
          BattAmps = -0.1 * float(BattAmps);             //-0.1 scale
          BattPower = BattVolts * BattAmps / 100;
        }
      }
      break;

    case 0x1D8:
      if (message.length == 8) {
        int temptorque;
        temptorque = (((message.data.byte[4] & 0x000F) << 8) + (message.data.byte[3]));  //signed13, mask off sign
        if (temptorque > 2046) {                 //13-bit signed conversion 4096/2
          temptorque = 0 - (4096 - temptorque);
        }
        temptorque = 2.5 * float(temptorque);
        if ((temptorque < 5000) && (temptorque > -2000)) {  //reduce errors
          RearTorque = temptorque;
        }
      }
      break;

    case 0x312:
      if (message.length == 8) {
        MinBattTemp = int((float((((message.data.byte[6] << 8) + message.data.byte[5]) & 0x1FF0) >> 4) * .25 - 25) * 1.8 + 32);

      }
      break;
    /*
          case 0x224:
            DCDCoutput224 = ((message.data.byte[3] << 8) + message.data.byte[2]) & 0x07FF;
            break;
     */
    case 0x241:
      BattCoolantRate = 0.1 * float(((message.data.byte[1] << 8) + message.data.byte[0]) & 0x01FF); //ID 241 SB 0 u9 scale .01 LPM
      PTCoolantRate = 0.1 * float((((message.data.byte[3] << 8) + message.data.byte[2]) & 0x07FC0) >> 6);    //ID 241 SB 22 u9 scale .01 LPM
      break;

    /*      case 0x261:
            v12v261 = (((message.data.byte[1] & 0x0F) << 12) | (message.data.byte[0] << 4)) >> 4;  //ID 261 SB0 u12 scale 0.005444 V
            break;
    */
    case 0x252:
      MaxRegen =  0.01 * float((message.data.byte[1] * 256) + message.data.byte[0]);
      MaxDisChg = 0.01 * float((message.data.byte[3] * 256) + message.data.byte[2]);
      break;

    case 0x257: //VehSpeed = 0;     //ID 257 SB 12 u12 scale .08 offset -40 KPH
      if (message.length == 8) {
        VehSpeed = 0.8 * float((((message.data.byte[2] << 8) + message.data.byte[1]) & 0xFFF0) >> 4) - 400;
      }
      break;


    case 0x399: //Blind spots
      if (message.length == 8) {
        BSR = (message.data.byte[0] & 0xC0) >> 6;
        BSL = (message.data.byte[0] & 0x30) >> 4;
        if (BSR > 2) {  // 3 is active but no warning
          BSR = 0;
        }
        if (BSL > 2) {
          BSL = 0;
        }
      }
      break;

    /*      ///////////
         case 0x292:
           SOCUI = (((message.data.byte[1] & 0x03) << 8) + message.data.byte[0]);
           SOCAVE = (((message.data.byte[4] << 8) + message.data.byte[3]) & 0xFFC0) >> 6;  //ID 292 SB30 u10 scale 0.1 %
           break;

         case 0x321:
           BattCoolantTemp = ((message.data.byte[1] & 0x03) << 8) + message.data.byte[0];  //ID 321 SB0 u10 scale 0.125 offset -40 C
           PTCoolantTemp = (((message.data.byte[2] << 8) + message.data.byte[1]) & 0x0FFC) >> 2;    //ID 321 SB10 u10 scale 0.125 offset -40 C
         break;

         case 0x352:
           BattFullKWh = ((message.data.byte[1] & 0x03) * 256) + message.data.byte[0];
           BattRemainKWh = ((message.data.byte[2] & 0x0F) * 64) + (message.data.byte[1] >> 2);
           //Serial.print(BattRemainKWh);
           //Serial.print(" ");
           break;

         case 0x315:
           InvHStemp376 = message.data.byte[1];
           break;

         /*     case 0x712:
                if (message.data.byte[0] = 0); //index 0
                {
                  CellTemp1 = message.data.byte[1];
                  CellTemp2 = message.data.byte[2];
                }
                break;
         */

    default:
      break;
    } //end can msg id case

  } //endif can rec msg

// yield(); ///needed?
}
