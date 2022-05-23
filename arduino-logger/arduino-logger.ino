#include <SPI.h>
#include <WiFiNINA.h>
#include "secrets.h"
#include <ArduinoJson.h>

// Wifi Credentials
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int status = WL_IDLE_STATUS;     // the WiFi radio's status

// API Credentialss
char server[] = "21593698.pythonanywhere.com";
uint8_t API_KEY[] = "b'eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpZCI6MiwiZW1haWwiOiIyMTU5MzY5OEBzdW4uYWMuemEifQ.ohTyMQ8Wky5OZMNOkUpF9fE33FLQeO4y7kvqnghEc90'";

class APIStateTemplate{
  public:
    uint8_t unix; 
    StaticJsonDocument<JSON_OBJECT_SIZE(3)> usage_doc;
    StaticJsonDocument<JSON_OBJECT_SIZE(3)> peak_doc;
};

WiFiClient client;

void setup() {

  APIStateTemplate APIState;
  
  Serial.begin(9600);
  
  while (!Serial);
  
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 5 seconds for connection:
    delay(5000);
  }

  // you're connected now, so print out the data:
  Serial.print("Connected to: ");
  Serial.println(ssid);

  // Build JSON objects
  APIState.usage_doc["datetime"] = "2022-04-08 11:05";
  APIState.usage_doc["usage"] = 1000;
  APIState.usage_doc["api_key"] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpZCI6MiwiZW1haWwiOiJkZm1vbGxlckBnbWFpbC5jb20ifQ.xndFJxOtsZ4Alsj5r-I59cfxetvWCM3DhfBv2fHmRE4";
  APIState.peak_doc["datetime"] = "2022-04-08 11:05";
  APIState.peak_doc["peak"] = 500;
  APIState.peak_doc["api_key"] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpZCI6MiwiZW1haWwiOiJkZm1vbGxlckBnbWFpbC5jb20ifQ.xndFJxOtsZ4Alsj5r-I59cfxetvWCM3DhfBv2fHmRE4";
  Serial.println("JSON Objects to be sent via POST request:");
  serializeJsonPretty(APIState.usage_doc, Serial);
  serializeJsonPretty(APIState.peak_doc, Serial);
  Serial.println();

  // Send Data
  postData(client, "/postUsage", APIState.usage_doc);
  postData(client, "/postPeak", APIState.peak_doc);

}

void loop() {
  delay(10000);
  Serial.println("Main Loop");
}

void postData(WiFiClient client, String endpoint, const JsonDocument& doc)
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
