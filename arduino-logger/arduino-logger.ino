#include <SPI.h>
#include <WiFiNINA.h>
#include "secrets.h"
#include <ArduinoJson.h>
#include <TimeLib.h>

// Wifi Credentials
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int status = WL_IDLE_STATUS;     // the WiFi radio's status

// API Credentialss
char server[] = "21593698.pythonanywhere.com";
char dt_server[] = "worldtimeapi.org";
//uint8_t API_KEY[] = "b'eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpZCI6MiwiZW1haWwiOiIyMTU5MzY5OEBzdW4uYWMuemEifQ.ohTyMQ8Wky5OZMNOkUpF9fE33FLQeO4y7kvqnghEc90'";

class APIStateTemplate{
  public:
    String datetime;
    int usage;
    int peak;
};

WiFiClient client;
APIStateTemplate APIState;

void setup() {

  APIState.usage = 1000;
  APIState.peak = 500;
  
  Serial.begin(9600);
  while (!Serial);
  setupWiFi();
  getDateTime();
  postData();

}

void loop() {
  delay(10000);
  Serial.println("Main Loop");
}

uint8_t timeToString()
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
  APIState.datetime = (char*)buf;
  Serial.print("(char*)buf: ");
  Serial.println((char*)buf);
}

void postData()
{
  timeToString();
  StaticJsonDocument<JSON_OBJECT_SIZE(3)> usage_doc;
  StaticJsonDocument<JSON_OBJECT_SIZE(3)> peak_doc;
  usage_doc["datetime"] = APIState.datetime;
  usage_doc["api_key"] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpZCI6MiwiZW1haWwiOiJkZm1vbGxlckBnbWFpbC5jb20ifQ.xndFJxOtsZ4Alsj5r-I59cfxetvWCM3DhfBv2fHmRE4";
  usage_doc["usage"] = APIState.usage;
  peak_doc["datetime"] = APIState.datetime;
  peak_doc["api_key"] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpZCI6MiwiZW1haWwiOiJkZm1vbGxlckBnbWFpbC5jb20ifQ.xndFJxOtsZ4Alsj5r-I59cfxetvWCM3DhfBv2fHmRE4";
  peak_doc["usage"] = APIState.peak;
  Serial.println("JSON Objects to be sent via POST request:");
  serializeJsonPretty(usage_doc, Serial);
  serializeJsonPretty(peak_doc, Serial);
  Serial.println();
  postToEndpoint(client, "/postUsage", usage_doc);
  postToEndpoint(client, "/postPeak", peak_doc);
  usage_doc.clear();
  peak_doc.clear();
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
    Serial.println("Client not connecting...");
  }

  Serial.println();
  Serial.println("Response from Server:");
  String line = "";
  while (client.connected()) { 
   line = client.readStringUntil('\n'); 
   Serial.println(line); 
 } 
}

void setupWiFi()
{
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 5 seconds for connection:
    delay(5000);
  }
  Serial.print("Connected to: ");
  Serial.println(ssid);
}

void getDateTime()
{
  Serial.println("\nStarting connection to server..."); 
  if (client.connect(dt_server, 80)) { 
    Serial.println("connected to server"); 
    // Make a HTTP request: 
    client.println("GET /api/timezone/Africa/Johannesburg HTTP/1.1");
    client.println("Host: worldtimeapi.org");
    client.println("Connection: close");
    client.println();
    int secondsWaited = 0;
    while (!client.available()) {
      delay(2000);
      secondsWaited += 2;
      Serial.println("Waiting for data...");
      if (secondsWaited > 20) {
        Serial.println("Time Out! Took too long to respond");
        return;
      }
    }
    //  Check HTTP Status
    char httpStatus[32] = {0};
    client.readBytesUntil('\r', httpStatus, sizeof(httpStatus));
    if(strcmp(httpStatus + 9, "200 OK") != 0){
      Serial.print(F("Unexpected Response: "));
      Serial.println(httpStatus + 9);
      return;
    }
    //  Skip HTTP Headers
    char endOfHeaders[] = "\r\n\r\n";
    if(!client.find(endOfHeaders)){
      Serial.println(F("Invalid Response"));
      return;
    }
    //  Create filter for parsing Json
    StaticJsonDocument<JSON_OBJECT_SIZE(1)> filter;
    filter["unixtime"] = true;
    StaticJsonDocument<JSON_OBJECT_SIZE(3)> doc; // Document to store incoming json after being filtered (experimentally found that a size of at least 3 is required)
    DeserializationError error;
    error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    } 
    Serial.println(" ");
    Serial.println("WorldTimeApi UnixTime Response:");
    serializeJsonPretty(doc, Serial);
    Serial.println(" ");
    uint32_t unixtime = 0;
    unixtime = doc["unixtime"];
    unixtime += 120*60; // Plus two hours
    setTime(unixtime);
    
    //  Disconnect
    client.stop();
  
  } else { 
    Serial.println("Unable to connect to WorldTimeApi server");
  } 
  Serial.println("End of getDateTime() function");
}
