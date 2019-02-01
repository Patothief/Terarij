#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <EasyNTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>

#define DHT_PIN 4     // D2
#define IR_PIN 0     // D3
#define UV_PIN 14    // D5
#define DHT_TYPE DHT22   // there are multiple kinds of DHT sensors

//const char* ssid = "Infoart";
//const char* password = "Iart236WEP707";
const char* ssid = "AMIS-1-002196675318";
const char* password = "malamerica";

DHT dht(DHT_PIN, DHT_TYPE);
WiFiServer server(80);

const unsigned long DHT_TIME = 10000; // 10000
const unsigned long OWM_TIME = 3 * 60000; // 40 * 60000
const unsigned long LAMPS_TIME = 10000; // 10000
const unsigned long THING_SPEAK_TIME = 30000; // 30000
const unsigned long FUNC_TIME = 10 * 60000; // 30 * 60000;

const long BIG_NEGATIVE = -10000000;
const unsigned long BIG_POSITIVE = 4294967295;

long prevDht = BIG_NEGATIVE;
long prevOwm = BIG_NEGATIVE;
long prevFunc = BIG_NEGATIVE;
unsigned long prevLamps = 0;
unsigned long prevThingSpeak = 0;

const unsigned long UTC_OFFSET = 2 * 60 * 60;

WiFiUDP udp;
EasyNTPClient ntpClient(udp, "hr.pool.ntp.org", UTC_OFFSET);

WiFiClient clientThingSpeak;
String thingSpeakApiKey = "QFJR5FAY6XNTE4NZ";     //  Write API key from ThingSpeak
const char* thingSpeakServer = "api.thingspeak.com";

HTTPClient clientOwm;
const String OWM_API_KEY = "1635308b354d17ba10ad50eade774a06";	 // Open Weather Map API Key
String owmCityId = "2446796"; // Blima, Niger
unsigned long owmSunrise;
unsigned long owmSunset;
float owmTemp;
String owmDescription;
float owmTempOffset = -5.0;

float dhtHumidity;
float dhtTemp;

float funcLowTemp = 31.00;
float funcHighTemp = 32.00;

float fallbackLowTemp = 27.00;
float fallbackHighTemp = 28.00;

float lowTemp;
float highTemp;

short uvLamp = 2; // 0 off, 1 on, 2 not set
short uvLampMode = 1; // 0 function, 1 owm, 2 manual
const short UV_START_HOUR = 11;
const short UV_STOP_HOUR = 21;
unsigned long uvStart;
unsigned long uvStop;

short irLamp = 2; // 0 off, 1 on, 2 not set
short irLampMode = 0; // 0 function, 1 owm, 2 manual
const short IR_START_HOUR = 7;
const short IR_STOP_HOUR = 21;
unsigned long irStart;
unsigned long irStop;


void setup() {
    Serial.begin(9600);
    delay(10);

    // Connect to WiFi network
    Serial.println("Connecting to " + String(ssid));

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int tryCycles = 0; // prevent connecting endless loop
        
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

    // Start the server
    server.begin();
    Serial.println("Server started");

    // Print the IP address
    Serial.println(WiFi.localIP());

	unsigned long nowNtp = ntpClient.getUnixTime();
	Serial.println("Time " + unixDateToHumanString(nowNtp));

    Serial.println("==========================================");

    pinMode(IR_PIN, OUTPUT);
    pinMode(UV_PIN, OUTPUT);
}

void loop() {
	unsigned long now = millis();
	unsigned long nowNtp = ntpClient.getUnixTime();
	
	readSensorData(now);
	
	getOwmData(now, nowNtp);
	
	funcCalculate(now, nowNtp);
	
	updateLamps(now, nowNtp);
	
	handleHttpRequest();
	
	updateThingSpeak(now);
}

void readSensorData(unsigned long now) {
    if (now - prevDht > DHT_TIME) {
        prevDht = now;

        dhtTemp = dht.readTemperature();
        dhtHumidity = dht.readHumidity();

        Serial.println("DHT Temp: " + String(dhtTemp) + "   DHT Humidity: " + String(dhtHumidity));
    }
}

void getOwmData(unsigned long now, unsigned long nowNtp) {
	if (uvLampMode != 1 && irLampMode != 1) {
		return;
	}

	if (now - prevOwm > OWM_TIME) {
        prevOwm = now;

        Serial.println("-----Getting data from OWM server-----");

        clientOwm.begin("http://api.openweathermap.org/data/2.5/weather?id=" + owmCityId + "&units=metric" + "&appid=" + OWM_API_KEY);

        int httpCode = clientOwm.GET();

        StaticJsonBuffer<1024> jsonBuffer;

        if (httpCode == HTTP_CODE_OK) {
            String payload = clientOwm.getString();
            Serial.println(payload);
            
            JsonObject& owmData = jsonBuffer.parseObject(payload);

            if (!owmData.success()) {
                Serial.println("Parsing failed");
                clientOwm.end();
                return;
            }

            owmTemp = owmData["main"]["temp"];
            owmSunrise = owmData["sys"]["sunrise"].as<long>() + UTC_OFFSET;
            owmSunset = owmData["sys"]["sunset"].as<long>() + UTC_OFFSET;
            owmDescription = owmData["weather"][0]["main"].as<String>();

            Serial.println("OWM temp: " + String(owmTemp));
            Serial.println("OWM sunrise (UTC): " + unixDateToHumanString(owmSunrise));
            Serial.println("OWM sunset (UTC): " + unixDateToHumanString(owmSunset));
            Serial.println("OWM description: " + owmDescription);

			if (uvLampMode == 1) {
				uvStart = owmSunrise;
				uvStop = owmSunset;

				if (owmDescription == "Rain") {
					Serial.println("Setting UV lamp start/stop to zero due to rain");
					uvStart = 0;
					uvStop = 0;
				}

                Serial.println("Setting uvStart to: " + unixDateToHumanString(uvStart));
				Serial.println("Setting uvStop to: " + unixDateToHumanString(uvStop));
			}

			if (irLampMode == 1) {
		    	unsigned long dayStart = nowNtp / 86400;
	
				irStart = owmSunrise;
				if (dayStart != 0) {
					irStop = unixDateFromStartAndHour(dayStart, IR_STOP_HOUR);
				} else {
					irStop = irStart + 14 * 60 * 60; // if NTP time could not be fetched, calculate irStop as 14 hours after irStart
				}
				
				lowTemp = owmTemp - 0.5 + owmTempOffset;
				highTemp = owmTemp + 0.5 + owmTempOffset;

                Serial.println("Setting irStart to: " + unixDateToHumanString(irStart));
				Serial.println("Setting irStop to: " + unixDateToHumanString(irStop));
				Serial.println("Setting lowTemp to: " + String(lowTemp));
				Serial.println("Setting highTemp to: " + String(highTemp));
			}
        } else {
        	// fallback in case OWM data could not be fetched
            Serial.println("Fallback in case OWM data could not be fetched");

	    	unsigned long dayStart = nowNtp / 3600;
	    	byte hour = dayStart % 24; 
	    	dayStart /= 24;

        	if (dayStart != 0) {
        		// fallback to function mode
				uvStart = unixDateFromStartAndHour(dayStart, UV_START_HOUR);
				uvStop = unixDateFromStartAndHour(dayStart, UV_STOP_HOUR);
				irStart = unixDateFromStartAndHour(dayStart, IR_START_HOUR);
				irStop = unixDateFromStartAndHour(dayStart, IR_STOP_HOUR);
				
				lowTemp = getAdjustedValue(funcLowTemp, hour);
				highTemp = getAdjustedValue(funcHighTemp, hour);

                Serial.println("Setting uvStart to: " + unixDateToHumanString(uvStart));
				Serial.println("Setting uvStop to: " + unixDateToHumanString(uvStop));
                Serial.println("Setting irStart to: " + unixDateToHumanString(irStart));
				Serial.println("Setting irStop to: " + unixDateToHumanString(irStop));
				Serial.println("Setting lowTemp to: " + String(lowTemp));
				Serial.println("Setting highTemp to: " + String(highTemp));
        	} else {
        		// probably no internet connection, keep lamps always active with fallback temperature range
	        	uvStart = 0;
	        	uvStop = BIG_POSITIVE;
	        	irStart = 0;
	        	irStop = BIG_POSITIVE;
        		lowTemp = fallbackLowTemp;
        		highTemp = fallbackHighTemp;
                Serial.println("Setting uvStart to: " + unixDateToHumanString(uvStart));
				Serial.println("Setting uvStop to: " + unixDateToHumanString(uvStop));
                Serial.println("Setting irStart to: " + unixDateToHumanString(irStart));
				Serial.println("Setting irStop to: " + unixDateToHumanString(irStop));
				Serial.println("Setting lowTemp to: " + String(lowTemp));
				Serial.println("Setting highTemp to: " + String(highTemp));
        	}
        }

        clientOwm.end();
        Serial.println("-----END OWM-----");
    }
}

void funcCalculate(unsigned long now, unsigned long nowNtp) {
	if (uvLampMode != 0 && irLampMode != 0) {
		return;
	}

    if (now - prevFunc > FUNC_TIME) {
        prevFunc = now;

        Serial.println("-----Function calculations-----");

    	unsigned long dayStart = nowNtp / 3600;
    	byte hour = dayStart % 24; 
    	dayStart /= 24;

    	if (dayStart != 0) {
			if (uvLampMode == 0) {
			   	uvStart = unixDateFromStartAndHour(dayStart, UV_START_HOUR);
			   	uvStop = unixDateFromStartAndHour(dayStart, UV_STOP_HOUR);
	
	           	Serial.println("Setting uvStart to: " + unixDateToHumanString(uvStart));
				Serial.println("Setting uvStop to: " + unixDateToHumanString(uvStop));
			}
	
			if (irLampMode == 0) {
			    irStart = unixDateFromStartAndHour(dayStart, IR_START_HOUR);
			    irStop = unixDateFromStartAndHour(dayStart, IR_STOP_HOUR);
		
			    lowTemp = getAdjustedValue(funcLowTemp, hour);
			    highTemp = getAdjustedValue(funcHighTemp, hour);
	
	            Serial.println("Setting irStart to: " + unixDateToHumanString(irStart));
				Serial.println("Setting irStop to: " + unixDateToHumanString(irStop));
				Serial.println("Setting lowTemp to: " + String(lowTemp));
				Serial.println("Setting highTemp to: " + String(highTemp));
			}
    	} else {
		    // probably no internet connection, keep lamps always active with fallback temperature range
        	uvStart = 0;
        	uvStop = BIG_POSITIVE;
        	irStart = 0;
        	irStop = BIG_POSITIVE;
    		lowTemp = fallbackLowTemp;
    		highTemp = fallbackHighTemp;
			Serial.println("Setting uvStart to: " + unixDateToHumanString(uvStart));
			Serial.println("Setting uvStop to: " + unixDateToHumanString(uvStop));
			Serial.println("Setting irStart to: " + unixDateToHumanString(irStart));
			Serial.println("Setting irStop to: " + unixDateToHumanString(irStop));
			Serial.println("Setting lowTemp to: " + String(lowTemp));
			Serial.println("Setting highTemp to: " + String(highTemp));
    	}
        Serial.println("-----END Function-----");
	}
}

void updateLamps(unsigned long now, unsigned long nowNtp) {
    if (now - prevLamps > LAMPS_TIME) {
        prevLamps = now;

		if (nowNtp == 0) {
			nowNtp = uvStart + 1000;
		}
		
		if (irLampMode != 2) { // not manual
	        if (nowNtp >= irStart && nowNtp <= irStop) {
	            if (!isnan(dhtTemp) && dhtTemp <= lowTemp && (irLamp == 0 || irLamp == 2)) {
	                irLamp = 1;
	                Serial.println("Automatic IR lamp on");
	                digitalWrite(IR_PIN, HIGH);
	            } else if ((isnan(dhtTemp) || dhtTemp > highTemp) && (irLamp == 1 || irLamp == 2)) {
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

		if (uvLampMode != 2) { // not manual
			if (nowNtp >= uvStart && nowNtp <= uvStop) {
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
}

void updateThingSpeak(unsigned long now) {
    if (now - prevThingSpeak > THING_SPEAK_TIME) {
        prevThingSpeak = now;

        Serial.println("Sending data to ThingSpeak server");

        if (clientThingSpeak.connect(thingSpeakServer, 80)) {
            String postStr = thingSpeakApiKey;

            postStr += "&field1=";
            postStr += String(dhtTemp);
            postStr += "&field2=";
            postStr += String(dhtHumidity);
            postStr += "&field3=";
            postStr += String(irLamp);
            postStr += "&field4=";
            postStr += String(uvLamp);
            postStr += "\r\n\r\n";

            clientThingSpeak.print("POST /update HTTP/1.1\n");
            clientThingSpeak.print("Host: api.thingspeak.com\n");
            clientThingSpeak.print("Connection: close\n");
            clientThingSpeak.print("X-THINGSPEAKAPIKEY: " + thingSpeakApiKey + "\n");
            clientThingSpeak.print("Content-Type: application/x-www-form-urlencoded\n");
            clientThingSpeak.print("Content-Length: ");
            clientThingSpeak.print(postStr.length());
            clientThingSpeak.print("\n\n");
            clientThingSpeak.print(postStr);

            clientThingSpeak.stop();
        }
    }
}

void handleHttpRequest() {
    WiFiClient client = server.available();
    if (client) {

        Serial.println("-----New client-----");

        int tryCycles = 0; // prevent client wait endless loop
        bool aborted = false;

        while (!client.available()) {
            delay(1);
            Serial.print(".");

            if (tryCycles++ > 500) {
                aborted = true;
                Serial.println("Aborted! (maximuim try cycles reached)");
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
                val = "DHT Temp: " + String(dhtTemp) + ", DHT Humidity: " + String(dhtHumidity);

                val += "<br/><br/>IR lamp mode: ";
                if (irLampMode == 0) {
                    val += "function";
                } else if (irLampMode == 1) {
                    val += "owm";
                } else if (irLampMode == 2) {
                    val += "manual";
                }
                val += "<br/>IR lamp status: ";
                if (irLamp == 0) {
                    val += "off";
                } else if (irLamp == 1) {
                    val += "on";
                } else if (irLamp == 2) {
                    val += "not set";
                }

                val += "<br/><br/>UV lamp mode: ";
                if (uvLampMode == 0) {
                    val += "function";
                } else if (uvLampMode == 1) {
                    val += "owm";
                } else if (uvLampMode == 2) {
                    val += "manual";
                }
                val += "<br/>UV lamp status: ";
                if (uvLamp == 0) {
                    val += "off";
                } else if (uvLamp == 1) {
                    val += "on";
                } else if (uvLamp == 2) {
                    val += "not set";
                }

                val += "<br/><br/>High temp set to: " + String(highTemp);
                val += "<br/>Low temp set to: " + String(lowTemp);
                val += "<br/><br/>Max temp set to: " + String(funcHighTemp);
                val += "<br/>Min temp set to: " + String(funcLowTemp);
                val += "<br/><br/>OWM CityID: " + String(owmCityId);
                val += "<br/>OWM Sunrise: " + unixDateToHumanString(owmSunrise);
                val += "<br/>OWM Sunset: " + unixDateToHumanString(owmSunset);
                val += "<br/>OWM Description: " + owmDescription;
                val += "<br/><br/>DHTpin: D2, IRpin: D3, UVpin: D5";
            } else if (req.indexOf("/uvLamp") != -1) {
                Serial.println("UV lamp request");
                if (req.indexOf("/forceOn") != -1) {
                    val = "Forcing UV lamp ON";
                    uvLamp = 1;
                    uvLampMode = 2;
                    digitalWrite(UV_PIN, HIGH);
            	    Serial.println(val);
                } else if (req.indexOf("/forceOff") != -1) {
                    val = "Forcing UV lamp OFF";
                    uvLamp = 0;
                    uvLampMode = 2;
                    digitalWrite(UV_PIN, LOW);
            	    Serial.println(val);
                } else if (req.indexOf("/owm") != -1) {
                    val = "UV lamp set to owm";
                    uvLamp = 2;
                    uvLampMode = 1;
                    prevOwm = BIG_NEGATIVE;
            	    Serial.println(val);
                } else if (req.indexOf("/func") != -1) {
                    val = "UV lamp set to function";
                    uvLamp = 2;
                    uvLampMode = 0;
                    prevFunc = BIG_NEGATIVE;
            	    Serial.println(val);
                }
            } else if (req.indexOf("/irLamp") != -1) {
                Serial.println("IR lamp request");
                if (req.indexOf("/forceOn") != -1) {
                    val = "Forcing IR lamp ON";
                    irLamp = 1;
                    irLampMode = 2;
                    digitalWrite(IR_PIN, HIGH);
            	    Serial.println(val);
                } else if (req.indexOf("/forceOff") != -1) {
                    val = "Forcing IR lamp OFF";
                    irLamp = 0;
                    irLampMode = 2;
                    digitalWrite(IR_PIN, LOW);
            	    Serial.println(val);
                } else if (req.indexOf("/owm") != -1) {
                    val = "IR lamp set to owm";
                    irLamp = 2;
                    irLampMode = 1;
                    prevOwm = BIG_NEGATIVE;
            	    Serial.println(val);
                } else if (req.indexOf("/func") != -1) {
                    val = "IR lamp set to function";
                    irLamp = 2;
                    irLampMode = 0;
                    prevFunc = BIG_NEGATIVE;
            	    Serial.println(val);
                }
            } else if (req.indexOf("/resetDevice") != -1) {
                Serial.println("Reset device request");
                val = "Reset device not yet implemented";
            } else if (req.indexOf("/set?") != -1) {
                String value = req.substring(req.indexOf("=") + 1, req.lastIndexOf(" "));
                if (req.indexOf("lowTemp") != -1) {
                    funcLowTemp = value.toFloat();
                    Serial.println("funcLowTemp: " + String(funcLowTemp));
                    val = "Setting low temperature to: " + String(funcLowTemp);
                    prevFunc = BIG_NEGATIVE;
                } else if (req.indexOf("highTemp") != -1) {
                    funcHighTemp = value.toFloat();
                    Serial.println("funcHighTemp: " + String(funcHighTemp));
                    val = "Setting high temperature to: " + String(funcHighTemp);
                    prevFunc = BIG_NEGATIVE;
                } else if (req.indexOf("cityId") != -1) {
                    owmCityId = value.toInt();
                    Serial.println("owmCityId: " + String(owmCityId));
                    val = "Setting owmCityId to: " + String(owmCityId);
                    prevOwm = BIG_NEGATIVE;
                } else if (req.indexOf("owmTempOffset") != -1) {
                    owmTempOffset = value.toFloat();
                    Serial.println("owmTempOffset: " + String(owmTempOffset));
                    val = "Setting owmTempOffset to: " + String(owmTempOffset);
                    prevOwm = BIG_NEGATIVE;
                }
            } else {
                Serial.println("Unsupported request.");
                val = "Unsupported request. Supported commands:<br/>";
                val += "    - /info<br/>";
                val += "    - /irLamp/forceOn<br/>";
                val += "    - /irLamp/forceOff<br/>";
                val += "    - /irLamp/func<br/>";
                val += "    - /irLamp/owm<br/>";
                val += "    - /uvLamp/forceOn<br/>";
                val += "    - /uvLamp/forceOff<br/>";
                val += "    - /uvLamp/func<br/>";
                val += "    - /uvLamp/owm<br/>";
                val += "    - /set?lowTemp=29.0<br/>";
                val += "    - /set?highTemp=30.0<br/>";
                val += "    - /set?owmCityId=2562305<br/>";
                val += "    - /set?owmTempOffset=1.5<br/>";
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
        Serial.println("-----Client disonnected-----");

        //client.flush();
        //client.stop();
    }
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

float getAdjustedValue(float value, int hour) {
    return value - sq(abs(hour - 15.0) / 4.0);
}

String unixDateToHumanString(unsigned long unix) {
    byte second = unix % 60; unix /= 60;
    byte minute = unix % 60; unix /= 60;
    byte hour   = unix % 24; unix /= 24;

    return String(hour) + ":" + String(minute) + ":" + String(second);
}

unsigned long unixDateFromStartAndHour(unsigned long dayStart, short hour) {
	return ((dayStart * 24 + hour) * 60) * 60;
}
