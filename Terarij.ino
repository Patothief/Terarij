/*
    This sketch demonstrates how to set up a simple HTTP-like server.
    The server will set a GPIO pin depending on the request
      http://server_ip/gpio/0 will set the GPIO2 low,
      http://server_ip/gpio/1 will set the GPIO2 high
    server_ip is the IP address of the ESP8266 module, will be
    printed to Serial when the module is connected.
*/

#include <ESP8266WiFi.h>

#include "DHT.h"

#define DHTPIN 4     // what digital pin the DHT22 is conected to
#define DHTTYPE DHT22   // there are multiple kinds of DHT sensors

DHT dht(DHTPIN, DHTTYPE);

const char* ssid = "Infoart";
const char* password = "Iart236WEP707";

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);

float h;
float t;

void setup() {
  Serial.begin(9600);
  delay(10);

  // Connect to WiFi network
  Serial.println("Connecting to " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.println(WiFi.localIP());
}

unsigned long prev = millis();

float lowTemp = 28.00;
float highTemp = 29.00;
bool uvLamp = false;

void loop() {
  unsigned long now = millis();

  if (now - prev > 3000) {
    prev = now;
  
    // read sensor data
    h = dht.readHumidity();
    t = dht.readTemperature();

    Serial.println("Temp: " + String(t) + "   Humidity: " + String(h));

    if (t < lowTemp && uvLamp) {
      uvLamp = false;
      Serial.println("Turning UV lamp on");
      // turn on uv lamp relay
    }

    if (t > highTemp && !uvLamp) {
      uvLamp = true;
      Serial.println("Turning UV lamp off");
      // turn on uv lamp relay
    }
  }
  
  WiFiClient client = server.available();
  if (client) {
    handleHttpRequest(client);
  }
}

void handleHttpRequest(WiFiClient &client) {
  Serial.print("New client");
  
  int tryCycles = 0; // prevent client wait endless loop
  bool aborted = false;
  
  while (!client.available()) {
    delay(1);
    Serial.print(".");
    
    if (tryCycles++>500) {
      aborted = true;
      Serial.println();
      Serial.print("Aborted");
      break;
    }
  }
  Serial.println();

  if (!aborted) {
    Serial.println("Reading client request");
    // Read the first line of the request
    String req = client.readStringUntil('\r');
    Serial.println(req);
    client.flush();

    String val;
    
    if (req.indexOf("/get") != -1) {
      Serial.println("Get data request");
      val = "Temp: " + String(t) + "   Humidity: " + String(h);
    } else if (req.indexOf("/set") != -1) {
      Serial.println("Set parameters request");
      val = "Setting not yet implemented";
    } else if (req.indexOf("/resetDevice") != -1) {
      Serial.println("Reset device request");
      val = "Reset device not yet implemented";
    } else {
      Serial.println("Unsupported request");
      val = "Unsupported request";
    }
    
  
    client.flush();

    // Prepare the response
    String s = prepareResponse(val);

    // Send the response to the client
    client.print(s);
  } else {
    client.flush();

    // Prepare error response
    String s = prepareErrorResponse();
    client.print(s);
  }
    
  delay(1);
  Serial.println("Client disonnected");
  //client.flush();
  //client.stop();
}

String prepareResponse(String val) {
  String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n";
  s += val;
  s += "</html>\n";
  return s;
}

String prepareErrorResponse() {
  return "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\nCould not read request</html>\n";
}

