#include <ESP8266WiFi.h>
#include <DHT.h>         // Adafruit Unified Sensor; DHT Sensor Library
#include <ThingSpeak.h>  // ThingSpeak by MathWorks

#define DHT_PIN 4        // Mapped to D2 pin on NodeMCU
#define DHT_TYPE DHT22   // There are multiple kinds of DHT sensors

DHT dht(DHT_PIN, DHT_TYPE);

float dhtHumidity;
float dhtTemp;

const char* ssid = "tvoj wifi ssid";
const char* password = "tvoj wifi password";

const unsigned long UPDATE_INTERVAL = 30000; // update interval in milliseconds
long prev = 0;

WiFiClient clientThingSpeak;
const char* thingSpeakApiKey = "your think speak api key";     // Write API key from ThingSpeak
unsigned long thingSpeakChannelNumber = 0123456;               // ThingSpeak channel number


void setup() {
    Serial.begin(9600);
    delay(10);

    // Connect to WiFi network
    Serial.println("Connecting to " + String(ssid));

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int tryCycles = 0; // prevent endless loop
        
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");

        if (tryCycles++ > 30) {
          Serial.println("Aborted! (maximuim try cycles reached).");
          break;
        }
    }
    Serial.println("");
    Serial.println("WiFi connected");

    // Print the IP address
    Serial.println(WiFi.localIP());
    Serial.println("==========================================");

    dht.begin();

    ThingSpeak.begin(clientThingSpeak);
}

void loop() {
    unsigned long now = millis();

    if (now - prev > UPDATE_INTERVAL) {
        prev = now;

        readSensorData();
  
        updateThingSpeak();
    }
}

void readSensorData() {
    dhtTemp = dht.readTemperature();
    dhtHumidity = dht.readHumidity();

    Serial.println("DHT Temp: " + String(dhtTemp) + "   DHT Humidity: " + String(dhtHumidity));
}

void updateThingSpeak() {
    Serial.println("Sending data to ThingSpeak server");

    ThingSpeak.setField(1, dhtTemp);
    ThingSpeak.setField(2, dhtHumidity);

    int status = ThingSpeak.writeFields(thingSpeakChannelNumber, thingSpeakApiKey);
    if(status == 200){
        Serial.println("ThingSpeak update successful.");
    } else {
        Serial.println("Problem updating ThingSpeak. HTTP error code " + String(status));
    }
}
