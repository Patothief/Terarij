#include <ESP8266HTTPClient.h>

#include <ArduinoJson.h>

#include <ESP8266WiFi.h>

#include <EasyNTPClient.h>
#include <WiFiUdp.h>

#include "DHT.h"

#include "time.h"

#define DHTPIN 4     // D2
#define IR_PIN 0     // D3
#define UV_PIN 14    // D5
#define DHTTYPE DHT22   // there are multiple kinds of DHT sensors

DHT dht(DHTPIN, DHTTYPE);

//const char* ssid = "Infoart";
//const char* password = "Iart236WEP707";
const char* ssid = "AMIS-1-002196675318";
const char* password = "malamerica";

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);

float h;
float t;

unsigned long prev = millis();
unsigned long prevThingSpeak = millis();
unsigned long prevOWM = millis();

float lowTemp = 31.00;
float highTemp = 32.00;
float realTempOffset = 0.0;

unsigned long sunrizeTime;
unsigned long sunsetTime;
          
short uvLamp = 2; // 0 off, 1 on, 2 not set
short uvLampMode = 0; // 0 auto, 1 manual

short irLamp = 2; // 0 off, 1 on, 2 not set
short irLampMode = 0; // 0 auto, 1 manual

short calculationMode = 1; // 0 adjusted, 1 real

WiFiUDP udp;

EasyNTPClient ntpClient(udp, "hr.pool.ntp.org", (2*60*60));

WiFiClient clientThingSpeak;
String thingSpeakApiKey = "QFJR5FAY6XNTE4NZ";     //  Write API key from ThingSpeak
const char* thingSpeakServer = "api.thingspeak.com";

HTTPClient clientOWM;
String OWM_API_KEY = "1635308b354d17ba10ad50eade774a06";  // Open Weather Map API Key
String owmCityId = "2562305";                           // Open Weather Map CityID


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

  pinMode(IR_PIN, OUTPUT);
  pinMode(UV_PIN, OUTPUT);
}

byte hour;

void loop() {
  unsigned long now = millis();

  if (now - prev > 5000) {
    prev = now;
    
    unsigned long epoch = ntpClient.getUnixTime();
    Serial.println("epoch: " + String(epoch));
    byte second = epoch%60; epoch /= 60;
    byte minute = epoch%60; epoch /= 60;
    hour        = epoch%24; epoch /= 24;
    Serial.println(String(hour) + ":" + String(minute) + ":" + String(second));

    unsigned long newEpoch = ((epoch * 24 + hour) * 60 + minute) * 60 + second;
    Serial.println("newEpoch: " + String(newEpoch));
    
    // read sensor data
    h = dht.readHumidity();
    t = dht.readTemperature();

    Serial.println("Temp: " + String(t) + "   Humidity: " + String(h));

    float highTempAdjusted = highTemp;
    float lowTempAdjusted = lowTemp;
    
    if (calculationMode == 0) { // adjusted
      highTempAdjusted = getAdjustedValue(highTemp, hour);
      lowTempAdjusted = getAdjustedValue(lowTemp, hour);

      sunriseEpoch = ((epoch * 24 + 7) * 60) * 60;
      sunsetEpoch = ((epoch * 24 + 21) * 60) * 60;
    } else if (calculationMode == 1) { // real
    }
    
    //Serial.println("highTempAdjusted: " + String(highTempAdjusted));
    //Serial.println("lowTempAdjusted: " + String(lowTempAdjusted));
    
    if (irLampMode == 0) { // auto
      if ((hour >= 7 && hour < 21) || epoch == 0) {
        if (!isnan(t) && t <= lowTempAdjusted && (irLamp == 0 || irLamp == 2)) {
          irLamp = 1;
          Serial.println("Automatic IR lamp on");
          digitalWrite(IR_PIN, HIGH);
        } else if ((isnan(t) || t > highTempAdjusted) && (irLamp == 1 || irLamp == 2)) {
          irLamp = 0;
          Serial.println("Automatic IR lamp off (sensor-based)");
          digitalWrite(IR_PIN, LOW);
        }
      } else if (irLamp == 1 || irLamp == 2) {
        irLamp = 0;
        Serial.println("Automatic IR lamp off (time-based)");
        digitalWrite(IR_PIN, LOW);
      }
    }

    if (uvLampMode == 0) { // auto
      if (epoch == 0) {
        if (irLamp == 0 && (uvLamp == 1 || uvLamp == 2)) {
          uvLamp = 0;
          Serial.println("Automatic UV lamp off (temperature fallback)");
          digitalWrite(UV_PIN, LOW);
        } else if (irLamp == 1 && (uvLamp == 0 || uvLamp == 2)) {
          uvLamp = 1;
          Serial.println("Automatic UV lamp on (temperature fallback)");
          digitalWrite(UV_PIN, HIGH);
        }
      } else if ((hour >= 7 && hour < 9) || (hour >= 11 && hour < 14) || (hour >= 16 && hour < 21)) {
        if (uvLamp == 0 || uvLamp == 2) {
          uvLamp = 1;
          Serial.println("Automatic UV lamp on");
          digitalWrite(UV_PIN, HIGH);
        }
      } else if (uvLamp == 1 || uvLamp == 2) {
          uvLamp = 0;
          Serial.println("Automatic UV lamp off");
          digitalWrite(UV_PIN, LOW);
      }
    }
  }
  
  WiFiClient client = server.available();
  if (client) {
    handleHttpRequest(client, hour);
  }

  updateThingSpeak(now);

  getOWMData(now);
}

void updateThingSpeak(unsigned long now) {
  if (now - prevThingSpeak > 30000) {
    prevThingSpeak = now;
    
    Serial.println("Sending data to ThingSpeak server");
    
    if (clientThingSpeak.connect(thingSpeakServer, 80)) {  
      String postStr = thingSpeakApiKey;
      
      postStr +="&field1=";
      postStr += String(t);
      postStr +="&field2=";
      postStr += String(h);
      postStr +="&field3=";
      postStr += String(irLamp);
      postStr +="&field4=";
      postStr += String(uvLamp);
      postStr += "\r\n\r\n";
      
      clientThingSpeak.print("POST /update HTTP/1.1\n");
      clientThingSpeak.print("Host: api.thingspeak.com\n");
      clientThingSpeak.print("Connection: close\n");
      clientThingSpeak.print("X-THINGSPEAKAPIKEY: "+thingSpeakApiKey+"\n");
      clientThingSpeak.print("Content-Type: application/x-www-form-urlencoded\n");
      clientThingSpeak.print("Content-Length: ");
      clientThingSpeak.print(postStr.length());
      clientThingSpeak.print("\n\n");
      clientThingSpeak.print(postStr);
      
      clientThingSpeak.stop();
    }
  }
}

void handleHttpRequest(WiFiClient &client, byte hour) {
  Serial.print("New client");
  
  int tryCycles = 0; // prevent client wait endless loop
  bool aborted = false;
  
  while (!client.available()) {
    delay(1);
    Serial.print(".");
    
    if (tryCycles++>500) {
      aborted = true;
      Serial.println();
      Serial.print("Aborted! (maximuim try cycles reached)");
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
    
    if (req.indexOf("/info") != -1) {
      Serial.println("Get data request");
      val = "Temp: " + String(t) + "   Humidity: " + String(h);

      val += "<br/><br/>IR lamp mode: ";
      if (irLampMode == 0) {
        val += "auto";
      } else {
        val += "manual";
      }
      val += "<br/>IR lamp status: ";
      if (irLamp == 0) {
        val += "off";
      } else if (irLamp == 1) {
        val += "on";
      } else if (irLamp ==2) {
        val += "not set";
      }

      val += "<br/><br/>UV lamp mode: ";
      if (uvLampMode == 0) {
        val += "auto";
      } else {
        val += "manual";
      }
      val += "<br/>UV lamp status: ";
      if (uvLamp == 0) {
        val += "off";
      } else if (uvLamp == 1) {
        val += "on";
      } else if (uvLamp ==2) {
        val += "not set";
      }

      val += "<br/><br/>High temp set to: " + String(highTemp) + ", Adjusted: " + String(getAdjustedValue(highTemp, hour));
      val += "<br/>Low temp set to: " + String(lowTemp) + ", Adjusted: " + String(getAdjustedValue(lowTemp, hour));
    } else if (req.indexOf("/uvLamp") != -1) {
      Serial.println("UV lamp request");
      if (req.indexOf("/forceOn") != -1) {
        val = "Forcing UV lamp ON";
        uvLamp = 1;
        uvLampMode = 1;
        digitalWrite(UV_PIN, HIGH);
      } else if (req.indexOf("/forceOff") != -1) {
        val = "Forcing UV lamp OFF";
        uvLamp = 0;
        uvLampMode = 1;
        digitalWrite(UV_PIN, LOW);
      } else if (req.indexOf("/auto") != -1) {
        val = "UV lamp set to auto";
        uvLamp = 2;
        uvLampMode = 0;
      }
    } else if (req.indexOf("/irLamp") != -1) {
      Serial.println("IR lamp request");
      if (req.indexOf("/forceOn") != -1) {
        val = "Forcing IR lamp ON";
        irLamp = 1;
        irLampMode = 1;
        digitalWrite(IR_PIN, HIGH);
      } else if (req.indexOf("/forceOff") != -1) {
        val = "Forcing IR lamp OFF";
        irLamp = 0;
        irLampMode = 1;
        digitalWrite(IR_PIN, LOW);
      } else if (req.indexOf("/auto") != -1) {
        val = "IR lamp set to auto";
        irLamp = 2;
        irLampMode = 0;
      }
    } else if (req.indexOf("/resetDevice") != -1) {
      Serial.println("Reset device request");
      val = "Reset device not yet implemented";
    } else if (req.indexOf("/set?") != -1) {
      Serial.println("Set");
      if (req.indexOf("lowTemp") != -1) {
        String lowTempString = req.substring(req.indexOf("=") + 1, req.lastIndexOf(" "));
        lowTemp = lowTempString.toFloat();
        Serial.println("lowTempString: <" + lowTempString + "> lowTemp: " + String(lowTemp));
        val = "Setting low temperature to: " + String(lowTemp);
      } else if (req.indexOf("highTemp") != -1) {
        String highTempString = req.substring(req.indexOf("=") + 1, req.lastIndexOf(" "));
        highTemp = highTempString.toFloat();
        Serial.println("highTempString: <" + highTempString + "> highTemp: " + String(highTemp));
        val = "Setting high temperature to: " + String(highTemp);
      } else if (req.indexOf("cityId") != -1) {
        String cityIdString = req.substring(req.indexOf("=") + 1, req.lastIndexOf(" "));
        owmCityId = cityIdString.toInt();
        Serial.println("cityIdString: <" + cityIdString + "> owmCityId: " + String(owmCityId));
        val = "Setting owmCityId to: " + String(owmCityId);
      } else if (req.indexOf("realTempOffset") != -1) {
        String realTempOffsetString = req.substring(req.indexOf("=") + 1, req.lastIndexOf(" "));
        realTempOffset = realTempOffsetString.toFloat();
        Serial.println("realTempOffsetString: <" + realTempOffsetString + "> realTempOffset: " + String(realTempOffset));
        val = "Setting realTempOffset to: " + String(realTempOffset);
      }   
    } else {
      Serial.println("Unsupported request.");
      val = "Unsupported request. Supported commands:<br/>";
      val += "    - /info<br/>";
      val += "    - /irLamp/forceOn<br/>";
      val += "    - /irLamp/forceOff<br/>";
      val += "    - /irLamp/auto<br/>";
      val += "    - /irLamp/real<br/>";
      val += "    - /uvLamp/forceOn<br/>";
      val += "    - /uvLamp/forceOff<br/>";
      val += "    - /uvLamp/auto<br/>";
      val += "    - /uvLamp/real<br/>";
      val += "    - /set?lowTemp=29.0<br/>";
      val += "    - /set?highTemp=30.0<br/>";
      val += "    - /set?cityId=2562305<br/>";
      val += "    - /set?realTempOffset=1.5<br/>";
    }
    
  
    client.flush();

    // Prepare the response
    String s = prepareResponse(val);

    // Send the response to the client
    client.print(s);
  } else {
    // Prepare error response
    String s = prepareErrorResponse();
    client.print(s);
    
    client.flush();
    client.stop();
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

void getOWMData(unsigned long now) {
    if (now - prevOWM > 15000) {
      prevOWM = now;
      
      Serial.println("Getting data from OWM server");

      clientOWM.begin("http://api.openweathermap.org/data/2.5/weather?id="+owmCityId+"&units=metric" + "&appid="+OWM_API_KEY);
      
      int httpCode = clientOWM.GET();
      
      StaticJsonBuffer<1024> jsonBuffer;
      
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          String payload = clientOWM.getString();
          Serial.println(payload);
          JsonObject& owm_data = jsonBuffer.parseObject(payload);
          
          if (!owm_data.success()) {
            Serial.println("Parsing failed");
            clientOWM.end();
            return;
          }
          
          int temp = owm_data["main"]["temp"];
          sunrizeEpoch = owm_data["sys"]["sunrise"];
          sunsetEpoch = owm_data["sys"]["sunset"];
          String description = owm_data["weather"][0]["description"];
          
          Serial.println("OWM temp: " + String(temp));
          Serial.println("OWM sunrizeEpoch: " + String(sunrizeEpoch));
          Serial.println("OWM sunsetEpoch: " + String(sunsetEpoch));
          Serial.println("OWM description: " + description);

          lowTemp = temp - 0.5 + realTempOffset;
          highTemp = temp + 0.5 + realTempOffset;

          Serial.println("lowTemp: " + String(lowTemp));
          Serial.println("highTemp: " + String(highTemp));
        }
      }

      clientOWM.end();
    }
}

float getAdjustedValue(float value, int hour) {
  return value - sq(abs(hour - 15.0) / 4.0);
}
