#include <SPI.h>
#include <WiFiNINA.h>
#include "secrets.h"
#include <ArduinoJson.h>
#include <TimeLib.h>
#include "SAMDTimerInterrupt.h"
#include "SAMD_ISR_Timer.h"



SAMDTimer ITimer(TIMER_TC3);
SAMD_ISR_Timer ISR_Timer;
#define HW_TIMER_INTERVAL_MS      1
#define TIMER_INTERVAL_1MS       1L



// Wifi Credentials
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int status = WL_IDLE_STATUS;     // the WiFi radio's status
int analogPin = A2;
int sensorValue = 0;
uint16_t readCount = 0;
uint16_t buffA[200];
uint16_t buffB[5];
uint16_t buffC[1800];
uint8_t lenA = 0;
uint8_t lenB = 0;
uint16_t lenC = 0;
uint32_t readSum = 0;
bool postDataFlag = false;
uint16_t readMax = 0;
uint32_t PrmsTotal = 0;
uint16_t PrmsAverage = 0;

// API Credentialss
char server[] = "21593698.pythonanywhere.com";
char dt_server[] = "worldtimeapi.org";

class APIStateTemplate{
  public:
    String datetime;
    uint16_t usage = 1000;
    uint16_t peak = 500;
    uint32_t last_post_time = 0;
    uint16_t posting_interval = 10*60; // Every 10 minutes
    uint32_t seconds_passed = 0;
};

WiFiClient client;
APIStateTemplate APIState;

void TimerHandler(void)
{
  ISR_Timer.run();
}

void readCurrent()
{
  if(lenA >= 200)
  {
    // Calculate Prms for the last 200ms and store in buffB
    buffB[lenB] = readMax;
    PrmsTotal += readMax;
    lenB ++;
    readMax = 0;
    lenA = 0;
  }
  else if(lenB >= 5)
  {
    // Calculate average Prms from buffB for the last second and store in buffC
    PrmsAverage = PrmsTotal / 5;
    Serial.print("PrmsAverage: ");
    Serial.print(PrmsAverage);
    Serial.print("    lenC: ");
    Serial.println(lenC);
    buffC[lenC] = PrmsAverage;
    PrmsTotal = 0;
    lenC ++;
    lenB = 0;
  }
  else if(lenC >= 1800)
  {
    // Calculate Usage and Peak every 30 min. Print to Serial and to Server
    postDataFlag = true;
    lenC = 0;
  }
  sensorValue = analogRead(analogPin);
  if(sensorValue > readMax) readMax = sensorValue;
  lenA ++;
}

void setup() {
  Serial.begin(9600);
  delay(2000);
//  while (!Serial);
  setupWiFi();
  updateSystemDateTime(); // Requires a Connection
//  postData(); // First Post

  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);

  // Interval in millisecs
  if (ITimer.attachInterruptInterval_MS(HW_TIMER_INTERVAL_MS, TimerHandler))
  {
    Serial.print(F("Starting ITimer OK, millis() = ")); Serial.println(millis());
  }
  else
  {
    Serial.println(F("Can't set ITimer. Select another freq. or timer"));
  }
  
  ISR_Timer.setInterval(TIMER_INTERVAL_1MS,  readCurrent);
}

void loop() {

//  APIState.seconds_passed = now() - APIState.last_post_time;
//  if(APIState.seconds_passed >= APIState.posting_interval)
//  {
//    postData();
//    APIState.last_post_time = now();
//  }

  if(postDataFlag)
  {
    // Calculate Usage and Peak every 30 min. Print to Serial and to Server.
    Serial.println("postDataFlag!");
    postDataFlag = false;
  }
}

String getCurrentDateTimeString()
{
  uint16_t year_temp = year();
  uint8_t month_temp = month();
  uint8_t day_temp = day();
  uint8_t hour_temp = hour();
  uint8_t minute_temp = minute();
  uint8_t second_temp = second();
  uint8_t  buf[] = "xxxx-xx-xx xx:xx:xx";
  buf[0] = ((year_temp/1000) % 10) + 48;
  buf[1] = ((year_temp/100) % 10) + 48;
  buf[2] = ((year_temp/10) % 10) + 48;
  buf[3] = (year_temp % 10) + 48;
  buf[5] = ((month_temp/10) % 10) + 48;
  buf[6] = (month_temp % 10) + 48;
  buf[8] = ((day_temp/10) % 10) + 48;
  buf[9] = (day_temp % 10) + 48;
  buf[11] = ((hour_temp/10) % 10) + 48;
  buf[12] = (hour_temp % 10) + 48;
  buf[14] = ((minute_temp/10) % 10) + 48;
  buf[15] = (minute_temp % 10) + 48;
  buf[17] = ((second_temp/10) % 10) + 48;
  buf[18] = (second_temp % 10) + 48;
  return (char*)buf;
}

void postData()
{
  Serial.println("");
  Serial.println("##### Posting Data to Flask ######################");
  Serial.println("On " + getCurrentDateTimeString());
  APIState.datetime = getCurrentDateTimeString();
  StaticJsonDocument<256> usage_doc;
  StaticJsonDocument<256> peak_doc;
  usage_doc["datetime"] = APIState.datetime;
  usage_doc["api_key"] = API_KEY;
  usage_doc["usage"] = APIState.usage;
  peak_doc["datetime"] = APIState.datetime;
  peak_doc["api_key"] = API_KEY;
  peak_doc["peak"] = APIState.peak;
//  Serial.println("JSON Objects to be sent via POST request:");
//  serializeJsonPretty(usage_doc, Serial);
//  Serial.println();
//  serializeJsonPretty(peak_doc, Serial);
//  Serial.println();
  postToEndpoint(client, "/postUsage", usage_doc);
  postToEndpoint(client, "/postPeak", peak_doc);
  usage_doc.clear();
  peak_doc.clear();
  Serial.println("##################################################");
  Serial.println("");
}

void postToEndpoint(WiFiClient client, String endpoint, const JsonDocument& doc)
{
  if (client.connect(server, 80)){
    client.print("POST ");
    client.print(endpoint);
    client.println(" HTTP/1.1");
    client.println("Host: 21593698.pythonanywhere.com");
    client.println("Connection: close");
    client.print("Content-Length: ");
    client.println(measureJson(doc));
    client.println("Content-Type: application/json");
    client.println(); //Terminate headers with a blank line
    serializeJson(doc, client); // Send JSON document in body
  } else{
    Serial.println("Unable to connect to PythonAnywhere Flask server!");
  }
  Serial.println("Response from Server:");
  String line = "";
  while (client.connected()) { 
   line = client.readStringUntil('\n'); 
   Serial.println(line); 
 } 
}

void setupWiFi()
{
  Serial.println("");
  Serial.println("##### WiFi Setup #################################");
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware!");
  }
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass); // Connect to WPA/WPA2 network:
    delay(5000);
  }
  Serial.print("Connected to: ");
  Serial.println(ssid);
  Serial.println("##################################################");
  Serial.println("");
}

void updateSystemDateTime()
{
  Serial.println("");
  Serial.println("##### Update Sys Time ############################");
  Serial.println("Starting connection to worldtimeapi server..."); 
  if (client.connect(dt_server, 80)) { 
    Serial.println("Connected to worldtimeapi server."); 
    // Make a HTTP request: 
    client.println("GET /api/timezone/Africa/Johannesburg HTTP/1.1");
    client.println("Host: worldtimeapi.org");
    client.println("Connection: close");
    client.println();
    int secondsWaited = 0;
    while (!client.available()) {
      delay(2000);
      secondsWaited += 2;
      Serial.println("Waiting for data from worldtimeapi server...");
      if (secondsWaited > 20) {
        Serial.println("Time Out! Took too long to respond");
        Serial.println("##################################################");
        Serial.println("");
        return;
      }
    }
    //  Check HTTP Status
    char httpStatus[32] = {0};
    client.readBytesUntil('\r', httpStatus, sizeof(httpStatus));
    if(strcmp(httpStatus + 9, "200 OK") != 0){
      Serial.print(F("Unexpected Response: "));
      Serial.println(httpStatus + 9);
      Serial.println("##################################################");
      Serial.println("");
      return;
    }
    //  Skip HTTP Headers
    char endOfHeaders[] = "\r\n\r\n";
    if(!client.find(endOfHeaders)){
      Serial.println(F("Invalid Response"));
      Serial.println("##################################################");
      Serial.println("");
      return;
    }
    //  Create filter for parsing Json
    StaticJsonDocument<16> filter;
    filter["unixtime"] = true;
    StaticJsonDocument<256> doc; // Document to store incoming json after being filtered (experimentally found that a size of at least 3 is required)
    DeserializationError error;
    error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      Serial.println("##################################################");
      Serial.println("");
      return;
    }
    Serial.println("WorldTimeApi UnixTime Response:");
    serializeJsonPretty(doc, Serial);
    Serial.println("");
    uint32_t unixtime = 0;
    unixtime = doc["unixtime"];
    unixtime += 120*60; // Plus two hours
    setTime(unixtime);
    APIState.last_post_time = unixtime;
    client.stop(); //  Disconnect
    filter.clear();
    doc.clear();
    Serial.println("DT Set: " + getCurrentDateTimeString());
  } else { 
    Serial.println("Unable to connect to WorldTimeApi server");
  } 
  Serial.println("##################################################");
  Serial.println("");
}
