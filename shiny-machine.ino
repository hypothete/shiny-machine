#include <WiFi.h>
#include <HTTPClient.h>
#include "SSD1306Wire.h"
#include <Sparkfun_APDS9301_Library.h>
#include <WebServer.h>
#include "arduino_secrets.h";

String version = "4.1.2";

// Initialize the OLED display using Wire library
SSD1306Wire  display(0x3c, 5, 4);

// Solenoids
#define PIN_SCREEN 14
#define PIN_A 12

// Luminosity sensor
APDS9301 apds;

// retained data
bool isHunting = true;
int srCount = 0;

// HTTP client for Twilio
HTTPClient http;

//Webserver for reading measurements
WebServer server(80);

// timing for non-blocking loop
long lastLoopStart = 0;
int lux = 0;

// text message properties
long nextText = 0;
bool outgoingText = false;
String textMsg = SECRET_TWILIO_POST_BODY;

enum loopState {
  StartLoop,
  StartGame,
  SelectFile,
  OpenFile,
  InteractShrine,
  Read,
  MonitorLux,
  MonitorShiny
};

loopState resetState = StartLoop;

#define MEASUREMENTS_LENGTH 20000
int measurements[MEASUREMENTS_LENGTH];
int measurementCount = 0;

// fire a solenoid
void pushButton(int pin) {
  digitalWrite(pin, HIGH);
  delay(150);
  digitalWrite(pin, LOW);
}

void resetGame() {
  pushButton(PIN_SCREEN);
  delay(1500);
  pushButton(PIN_SCREEN);
  delay(1000);
  pushButton(PIN_SCREEN);
}

void handleRoot() {
  String page = "{\n";
  page += "  \"version\": \"";
  page += version;
  page += "\",\n";
  page += "  \"values\": [";
  for(int i=0; i<measurementCount; i++){
    if (i > 0 && i < measurementCount) {
      page += ", ";
    }
    page += measurements[i];
  }
  page += "],\n";
  page += "  \"resetCount\": ";
  page += srCount;
  page += ",\n";
  page += "  \"isHunting\": ";
  page += isHunting ? "true" : "false";
  page += "\n";
  page += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", page);
}

void handleNotFound() {
  server.send(404, "text/plain", "File not found\n");
  display.println("Sent 404");
}

void setupSolenoids() {
  pinMode(PIN_SCREEN, OUTPUT);
  pinMode(PIN_A, OUTPUT);
}

void setupDisplay() {
  display.init();
  display.clear();
  display.setLogBuffer(5, 30);
}

void setupWifi() {
  // Wifi setup
  display.print("Connecting to ");
  display.println(SECRET_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASSWORD);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    display.print(".");
    display.drawLogBuffer(0, 0);
    display.display();
  }
  display.println("WiFi connected.");
  display.println(WiFi.localIP());
}

void reconnectWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    display.println("Reconnecting to WiFi");
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(SECRET_SSID, SECRET_PASSWORD);
    display.clear();
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      display.print(".");
      display.drawLogBuffer(0, 0);
      display.display();
    }
    display.println("WiFi connected.");
    display.println(WiFi.localIP());
  }
}

void setupLuxSensor() {
  if (apds.begin(0x39)) {
    display.println("Error with lux sensor");
    while(1);
  }
  apds.setGain(APDS9301::HIGH_GAIN);
  apds.setIntegrationTime(APDS9301::INT_TIME_101_MS);
}

void setupWebserver() {
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup() {
  setupSolenoids();
  setupDisplay();
  setupWifi();
  setupLuxSensor();
  setupWebserver();
}

int updateLux() {
  return int(apds.readLuxLevel());
}

void postToTwilio(String body) {
  textMsg = SECRET_TWILIO_POST_BODY;
  textMsg += body;
  outgoingText = true;
}

void checkTwilio() {
  if(outgoingText && (nextText < millis())) {
    http.begin(SECRET_TWILIO_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.setAuthorization(SECRET_TWILIO_AUTH);
    int httpResponse = http.POST(textMsg);
    http.end();
    if (httpResponse < 200 || httpResponse > 300) {
      nextText = millis() + 60000;
      display.print("Twilio error: ");
      display.println(httpResponse);
    }
    else {
      outgoingText = false;
      display.println("Sent text!");
    }
  }
}

void addMeasurement(int value) {
  if(measurementCount >= MEASUREMENTS_LENGTH - 1) {
    isHunting = false;
    display.println("Error: too many values!");
    postToTwilio("Error: too many values!");
  }
  else {
    measurements[measurementCount] = value;
    measurementCount++;
  }
}

void softResetLoop() {
  long timeSinceLast = millis() - lastLoopStart;
  if (resetState == StartLoop) {
    lastLoopStart = millis();
    display.print("starting loop ");
    display.println(srCount);
    resetState = StartGame;
  }
  else if (resetState == StartGame && timeSinceLast > 6000) {
    pushButton(PIN_A);
    resetState = SelectFile;
  }
  else if (resetState == SelectFile && timeSinceLast > 8000) {
    pushButton(PIN_A);
    resetState = OpenFile;
  }
  else if (resetState == OpenFile && timeSinceLast > 10000) {
    // open file
    pushButton(PIN_A);
    resetState = InteractShrine;
  }
  else if (resetState == InteractShrine && timeSinceLast > 12000) {
    pushButton(PIN_A);
    resetState = Read;
  }
  else if (resetState == Read) {
    delay(1000);
    for(int i=0; i<9; i++){
      pushButton(PIN_A);
      if (i < 8) {
        delay(1000);
      }
    }
    resetState = MonitorLux;
  }
  else if (resetState == MonitorLux) {
    display.println("starting lux meter");
    lux = 0;
    resetState = MonitorShiny;
  }
  else if (resetState == MonitorShiny) {
    int newLux = updateLux();
    if (newLux != lux) {
      display.println(newLux);
    }
    lux = newLux;
    if (timeSinceLast > 39900) {
      addMeasurement(lux);
      if(lux > 1) {
        // deviation from standard lux curve
        display.println(" found shiny!");
        isHunting = false;
        postToTwilio("Shiny found!");
      }
      else {
        display.println("loop timed out");
        resetState = StartLoop;
        srCount += 1;
        resetGame();
      }
    }
  }
}

void loop() {
  if (isHunting) {
    softResetLoop();
  }
  else {
    checkTwilio();
  }
  reconnectWifi();
  server.handleClient();
  display.clear();
  display.drawLogBuffer(0, 0);
  display.display(); 
}
