#include "secrets.h"
#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include "SAMDTimerInterrupt.h"
#include "SAMD_ISR_Timer.h"
#include <SD.h>
#include <BlynkSimpleWiFiNINA.h>

SAMDTimer ITimer(TIMER_TC3);
SAMD_ISR_Timer ISR_Timer;
#define HW_TIMER_INTERVAL_MS      1
#define TIMER_INTERVAL_1MS       1L
const int chipSelect = 10;
File sentDataFile;
File stdoutFile;
File backlogFile;
BlynkTimer timer;

// Wifi Credentials
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
char auth[] = BLYNK_AUTH_TOKEN;
int status = WL_IDLE_STATUS;     // the WiFi radio's status
int analogPin = A7;
uint16_t sensorValue = 0;
uint16_t readCount = 0;
uint16_t buffA[200];
uint16_t buffB[5];
uint16_t buffC[2000];
uint8_t lenA = 0;
uint8_t lenB = 0;
uint16_t lenC = 0;
uint32_t readSum = 0;
bool postDataFlag = false;
uint16_t readMax = 0;
uint16_t Prms = 0;
uint32_t PrmsTotal = 0;
uint16_t PrmsAverage = 0;
uint8_t lastLoopMin = 0;
uint8_t thisLoopMin = 0;
int millivolts = 0;
String SDString = "";
bool retry_post = false;
bool retry_updateTime = false;
uint32_t num_failures = 0;
bool posted = false;

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

void StandardOutput(String message)
{
  Serial.print(message);
  stdoutFile = SD.open("stdout.txt", FILE_WRITE);
  if (stdoutFile) {
    stdoutFile.print(message);
    stdoutFile.close();
  } else Serial.println("error opening stdout.txt");
}

void readCurrent()
{
  if(lenA >= 200)
  {
    // Calculate Prms for the last 200ms and store in buffB
    millivolts = readMax*(3300/1023.0) - 1660 - 100;
    if(millivolts < 0) millivolts = 0;
    Prms = (millivolts/33.0) * 2/sqrt(2) * 230; // Prms
    PrmsTotal += Prms;
    lenB ++;
    readMax = 0;
    lenA = 0;
  }
  else if(lenB >= 5)
  {
    // Calculate average Prms from buffB for the last second and store in buffC
    PrmsAverage = PrmsTotal / 5.0;
    buffC[lenC] = PrmsAverage;
    PrmsTotal = 0;
    lenC ++;
    lenB = 0;
  }
  sensorValue = analogRead(analogPin);
  if(sensorValue > readMax) readMax = sensorValue;
  lenA ++;
}

void myTimerEvent()
{
  // You can send any value at any time.
  // Please don't send more that 10 values per second.
  Blynk.virtualWrite(V2, millis() / 1000);
  Blynk.virtualWrite(V5, PrmsAverage);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Blynk.begin(auth, ssid, pass);
  timer.setInterval(1000L, myTimerEvent);
  Blynk.virtualWrite(V6, 0);

  Serial.print("\n######## INIT SD CARD ############################\n");
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    StandardOutput("Card failed, or not present\n");
    while (1);
  }
  stdoutFile = SD.open("stdout.txt", FILE_WRITE);
  if (stdoutFile) {
    stdoutFile.print("\n######## INIT SD CARD ############################ - New Power Cycle\n");
    stdoutFile.close();
  } else Serial.println("error opening stdout.txt"); 
  StandardOutput("card initialized.\n");
  if(!SD.exists("log.txt")) // Add heading line
  {
    SDString = "dt,usage(kWh),peak(W)";
    sentDataFile = SD.open("log.txt", FILE_WRITE);
    if (sentDataFile) {
      sentDataFile.println(SDString);
      sentDataFile.close();
    } else StandardOutput("error opening log.txt"); 
  }
  StandardOutput("##################################################\n\n");

  setupWiFi();
  updateSystemDateTime(); // Requires a Connection

  StandardOutput("\n######## START HARDWARE TIMER ####################\n");
  // Interval in millisecs
  if (ITimer.attachInterruptInterval_MS(HW_TIMER_INTERVAL_MS, TimerHandler))
  {
    StandardOutput("Starting ITimer OK\n");
  }
  else
  {
    StandardOutput("Can't set ITimer. Select another freq. or timer\n");
    while(1);
  }
  StandardOutput("##################################################\n\n");
  
  ISR_Timer.setInterval(TIMER_INTERVAL_1MS,  readCurrent);
  lastLoopMin = minute();
}

void loop() {
  thisLoopMin = minute();
  if(thisLoopMin % 30 == 0 && !posted)
  {
    preparePostData();
    postData(true);
    postBacklog();
    updateSystemDateTime();
    posted = true;
  } else if (thisLoopMin % 30 != 0 && posted) posted = false;
  maintainWiFi();
  Blynk.run();
  timer.run();
//  lastLoopMin = thisLoopMin;
}

void postBacklog()
{
  if(SD.exists("backlog.txt"))
  {
    backlogFile = SD.open("backlog.txt", FILE_READ);
    if (backlogFile)
    {
      String line = "";
      uint8_t backlogCount = 0;
      int res;
      int successfulWrites = 0;
      char c;
      StandardOutput("\n################## BACKLOG #######################\n");
      uint8_t ln_num = 1;
      while(backlogFile.available())
      {
        line = backlogFile.readStringUntil('\n');
        if (ln_num > 1)
        {
          APIState.usage = line.substring(17, 22).toInt();
          APIState.peak = line.substring(23).toInt();
          APIState.datetime = line.substring(0, 16);
          StandardOutput("Line " + String(ln_num) + ": ");
          StandardOutput("DT: " + String(APIState.datetime) + " || ");
          StandardOutput("Usage: " + String(APIState.usage) + " || ");
          StandardOutput("Peak: " + String(APIState.peak) + "\n");
          res = postData(false);
          if (res == 1) successfulWrites ++;
          backlogCount ++;
        }
        ln_num ++;
      }
      backlogFile.close();
      if (backlogCount == successfulWrites && SD.exists("backlog.txt")) {
        SD.remove("backlog.txt");
        StandardOutput("backlog.txt deleted\n");
      }
    }
    StandardOutput("##################################################\n\n");
  }
}

void preparePostData()
{
  // Calculate Usage and Peak every 30 min. Print to Serial and to Server.
  APIState.peak = 0;
  APIState.usage = 0;
  APIState.datetime = getCurrentDateTimeString();
  float usage_accum = 0;
  for(int i = 0; i < lenC; i++)
  {
    if(buffC[i] > APIState.peak) APIState.peak = buffC[i]; // W
    usage_accum += buffC[i]*(1.0/3600.0); // Wh
  }
  lenC = 0;
  APIState.usage = usage_accum; // Wh
}

String getCurrentDateTimeString()
{
  uint16_t year_temp = year();
  uint8_t month_temp = month();
  uint8_t day_temp = day();
  uint8_t hour_temp = hour();
  uint8_t minute_temp = minute();
  uint8_t  buf[] = "xxxx-xx-xx xx:xx";
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
  return (char*)buf;
}

int postData(bool first)
{
  if (first) StandardOutput("\n##### Posting Data to Flask ######################\n");
  StandardOutput("For " + APIState.datetime + "\n");
  StaticJsonDocument<256> data_doc;
  data_doc["datetime"] = APIState.datetime;
  data_doc["api_key"] = API_KEY;
  data_doc["usage"] = APIState.usage;
  data_doc["peak"] = APIState.peak;
//  Serial.println("JSON Objects to be sent via POST request:");
//  serializeJsonPretty(usage_doc, Serial);
//  Serial.println();
//  serializeJsonPretty(peak_doc, Serial);
//  Serial.println();
  StandardOutput("Sending Data to /postData\n");
  int res = postToEndpoint(client, "/postData", data_doc);
  data_doc.clear();
  if(res == 0){
    if (first) {
      if(!SD.exists("backlog.txt")) // Add heading line
      {
        SDString = "dt,usage(kWh),peak(W)";
        backlogFile = SD.open("backlog.txt", FILE_WRITE);
        if (backlogFile) {
          backlogFile.println(SDString);
          backlogFile.close();
        } else StandardOutput("error opening backlog.txt");
      }
//      SDString = APIState.datetime + ',' + String(APIState.usage) + ',' + String(APIState.peak);
      SDString = APIState.datetime + ',' + "xxxxx" + ',' + "xxxxx";
      SDString[17] = ((APIState.usage/10000) % 10) + 48;
      SDString[18] = ((APIState.usage/1000) % 10) + 48;
      SDString[19] = ((APIState.usage/100) % 10) + 48;
      SDString[20] = ((APIState.usage/10) % 10) + 48;
      SDString[21] = ((APIState.usage/1) % 10) + 48;
      SDString[23] = ((APIState.peak/10000) % 10) + 48;
      SDString[24] = ((APIState.peak/1000) % 10) + 48;
      SDString[25] = ((APIState.peak/100) % 10) + 48;
      SDString[26] = ((APIState.peak/10) % 10) + 48;
      SDString[27] = ((APIState.peak/1) % 10) + 48;
      backlogFile = SD.open("backlog.txt", FILE_WRITE); 
      // if the file is available, write to it:
      if (backlogFile) {
        backlogFile.println(SDString);
        backlogFile.close();
      } else StandardOutput("error opening backlog.txt\n");
    }
    StandardOutput("##################################################\n\n");
    return 0;
  }
  else{
    StandardOutput("Successful write to server.\n");
//    SDString = APIState.datetime + ',' + String(APIState.usage) + ',' + String(APIState.peak);
    SDString = APIState.datetime + ',' + "xxxxx" + ',' + "xxxxx";
    SDString[17] = ((APIState.usage/10000) % 10) + 48;
    SDString[18] = ((APIState.usage/1000) % 10) + 48;
    SDString[19] = ((APIState.usage/100) % 10) + 48;
    SDString[20] = ((APIState.usage/10) % 10) + 48;
    SDString[21] = ((APIState.usage/1) % 10) + 48;
    SDString[23] = ((APIState.peak/10000) % 10) + 48;
    SDString[24] = ((APIState.peak/1000) % 10) + 48;
    SDString[25] = ((APIState.peak/100) % 10) + 48;
    SDString[26] = ((APIState.peak/10) % 10) + 48;
    SDString[27] = ((APIState.peak/1) % 10) + 48;
    sentDataFile = SD.open("log.txt", FILE_WRITE); 
    // if the file is available, write to it:
    if (sentDataFile) {
      sentDataFile.println(SDString);
      sentDataFile.close();
    } else StandardOutput("error opening log.txt\n");
  }
  if (first) StandardOutput("##################################################\n\n");
  return 1;
}

int postToEndpoint(WiFiClient client, String endpoint, const JsonDocument& doc)
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
      StandardOutput("Unable to connect to PythonAnywhere Flask server!\n");
      return 0;
  }
  StandardOutput("Response from Server:\n");
  String line = "";
  while (client.connected()) { 
   line = client.readStringUntil('\n'); 
   StandardOutput(line + "\n"); 
  }
  return 1;
}

void setupWiFi()
{
  StandardOutput("\n##### WiFi Setup #################################\n");
  if (WiFi.status() == WL_NO_MODULE) {
    StandardOutput("Communication with WiFi module failed!\n");
    while (true);
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    StandardOutput("Please upgrade the firmware!\n");
  }
  
  while (WiFi.status() != WL_CONNECTED) {
    StandardOutput("Attempting to connect to WPA SSID: " + String(ssid) + "\n");
    WiFi.begin(ssid, pass); // Connect to WPA/WPA2 network:
    delay(5000);
  }
  StandardOutput("Connected to: " + String(ssid) + "\n");
  Blynk.virtualWrite(V6, 1);
  StandardOutput("##################################################\n\n");
}

void maintainWiFi() {
  if(WiFi.status() != WL_CONNECTED) {
    Blynk.virtualWrite(V6, 0);
    StandardOutput("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
    StandardOutput("Trying to re-establish a WiFi connection on " + getCurrentDateTimeString() + "...\n");
    WiFi.end();
    WiFi.begin(ssid, pass);
    StandardOutput("Waiting 5 seconds...\n");
    delay(5000);
    if(WiFi.status() == WL_CONNECTED){
      StandardOutput("Connected to: " + String(ssid) + "\n");
      Blynk.virtualWrite(V6, 1);
    } else {
      StandardOutput("Still not connected.\n");
    }
    StandardOutput("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n\n");
  }
}

void updateSystemDateTime()
{
  StandardOutput("\n##### Update Sys Time ############################\n");
  StandardOutput("Starting connection to worldtimeapi server...\n"); 
  if (client.connect(dt_server, 80)) { 
    StandardOutput("Connected to worldtimeapi server.\n"); 
    // Make a HTTP request: 
    client.println("GET /api/timezone/Africa/Johannesburg HTTP/1.1");
    client.println("Host: worldtimeapi.org");
    client.println("Connection: close");
    client.println();
    int secondsWaited = 0;
    while (!client.available()) {
      delay(2000);
      secondsWaited += 2;
      StandardOutput("Waiting for data from worldtimeapi server...\n");
      if (secondsWaited > 20) {
        StandardOutput("Time Out! Took too long to respond\n");
        StandardOutput("##################################################\n\n");
        return;
      }
    }
    //  Check HTTP Status
    char httpStatus[32] = {0};
    client.readBytesUntil('\r', httpStatus, sizeof(httpStatus));
    if(strcmp(httpStatus + 9, "200 OK") != 0){
      StandardOutput("Unexpected Response: " + String(httpStatus + 9) + "\n");
      StandardOutput("##################################################\n\n");
      return;
    }
    //  Skip HTTP Headers
    char endOfHeaders[] = "\r\n\r\n";
    if(!client.find(endOfHeaders)){
      StandardOutput("Invalid Response\n");
      StandardOutput("##################################################\n\n");
      return;
    }
    //  Create filter for parsing Json
    StaticJsonDocument<16> filter;
    filter["unixtime"] = true;
    StaticJsonDocument<256> doc; // Document to store incoming json after being filtered (experimentally found that a size of at least 3 is required)
    DeserializationError error;
    error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
    if (error) {
      StandardOutput("deserializeJson() failed: " + String(error.f_str()) + "\n");
      StandardOutput("##################################################\n\n");
      return;
    }
    StandardOutput("WorldTimeApi UnixTime Response:\n");
    StandardOutput("**JSON only printed in serial monitor, but api\n  call has been successful.\n");
    Serial.println();
    serializeJsonPretty(doc, Serial);
    Serial.println();
    uint32_t unixtime = 0;
    unixtime = doc["unixtime"];
    unixtime += 120*60; // Plus two hours
    setTime(unixtime);
    APIState.last_post_time = unixtime;
    client.stop(); //  Disconnect
    filter.clear();
    doc.clear();
    StandardOutput("DT Set: " + getCurrentDateTimeString() + "\n");
    retry_updateTime = false;
  } else { 
    StandardOutput("Unable to connect to WorldTimeApi server\n");
    retry_updateTime = true;
  } 
  StandardOutput("##################################################\n\n");
}
