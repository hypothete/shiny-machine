#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <ESP32Servo.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include "arduino_secrets.h";

String version = "2.3.0";

// Initialize the OLED display using Wire library
SSD1306Wire  display(0x3c, 5, 4);

// Servo stuff
Servo startservo;
Servo aservo;
#define A_PIN 12
#define START_PIN 2

// Luminosity sensor
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

// Webserver & retained data
WebServer server(80);
String huntmode = "ambush";
bool isHunting = true;
int srCount = 0;
String redirectPage = "<html><head><title>Redirecting...</title><meta http-equiv='refresh' content='1;url=/' /></head><body></body></html>";
String backPage = "<html><head><title>Going back to referrer...</title></head><body><script type='text/javascript'>location.replace(document.referrer);</script></body></html>";

// HTTP client for Twilio
HTTPClient http;

// timing for non-blocking loop
long lastLoopStart = 0.0;
long timeLuxStart = 0.0;
long lux = 0.0;
long lastLux = 0.0;
long timeUntilOptions = 0.0;
long lastMeasuredLoop = 0.0;

enum loopState {
  StartLoop,
  SkipIntro,
  OpenFile,
  StartEncounter,
  OverworldStart,
  StartLuxMeter,
  MonitorLux,
  EndLoop
};

loopState resetState = StartLoop;

#define MIN_SHINY_TIME 1080
#define MAX_SHINY_TIME 1280

// cache of known values
#define KNOWN_VALUES_LENGTH 100
long knownValues[KNOWN_VALUES_LENGTH];
int knownValuesTally[KNOWN_VALUES_LENGTH];

void pressButton(Servo servo, int pin) {
  servo.attach(pin);
  servo.write(55);
  delay(200);
  servo.write(15);
  delay(200);
  servo.detach();
}

void handleReset() {
  if (server.method() == HTTP_POST) {
    clearKnownValues();
    huntmode = server.arg("huntmode");
    srCount = 0;
    isHunting = true;
    resetState = StartLoop;
    server.send(200, "text/html", backPage);
    display.clear();
    display.println("Restarting hunt");
    display.print("Setting hunt mode to ");
    display.println(huntmode);
    display.drawLogBuffer(0, 0);
    display.display();
    pressButton(startservo, START_PIN);
  }
  else {
    // weird verb - just redirect to form
    server.send(200, "text/html", redirectPage);
  }
}

void handleContinue() {
  if (server.method() == HTTP_POST) {
    srCount += 1;
    isHunting = true;
    resetState = StartLoop;
    server.send(200, "text/html", backPage);
    display.clear();
    display.println("Continuing hunt");
    display.drawLogBuffer(0, 0);
    display.display();
    pressButton(startservo, START_PIN);
  }
  else {
    // weird verb - just redirect to form
    server.send(200, "text/html", redirectPage);
  }
}

void handleJson() {
  String page = "{\n";
  page += "  \"version\": \"";
  page += version;
  page += "\",\n";
  page += "  \"values\": [";
  for(int i=0; i<KNOWN_VALUES_LENGTH; i++){
    if (knownValues[i] == 0.0) {
      break;
      }
    if (i > 0 && i < KNOWN_VALUES_LENGTH - 1) {
      page += ", ";
    }
    page += "[";
    page += knownValues[i];
    page += ", ";
    page += knownValuesTally[i];
    page += "]";
  }
  page += "],\n";
  page += "  \"resetCount\": ";
  page += srCount;
  page += ",\n";
  page += "  \"isHunting\": ";
  page += isHunting ? "true" : "false";
  page += ",\n";
  page += "  \"lastMeasuredLoop\": ";
  page += lastMeasuredLoop;
  page += ",\n";
  page += "  \"huntmode\": \"";
  page += huntmode;
  page += "\"\n";
  page += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", page);
}

void handleNotFound() {
  server.send(404, "text/plain", "File not found\n");
  display.clear();
  display.println("Sent 404");
  display.drawLogBuffer(0, 0);
  display.display();
}

void clearKnownValues() {
  for(int i=0; i<KNOWN_VALUES_LENGTH; i++){
    knownValues[i] = 0.0;
    knownValuesTally[i] = 0;
  }
}

void addKnownValue(long last) {
  // first check if there's a close value
  int closestValue = -1;
  int lastZero = -1;
  for(int i=0; i<KNOWN_VALUES_LENGTH; i++){
    long delta = knownValues[i] - last;
    if (delta == 0.0) {
      // within 5ms of a known value, increment value count
      closestValue = i;
      knownValuesTally[i] += 1;
      break;
    }
    else if (knownValues[i] == 0.0) {
      // empty slot, make a note & break - fill from start
      lastZero = i;
      break;
    }
  }
  if (closestValue == -1 && lastZero > -1) {
    if (lastZero == KNOWN_VALUES_LENGTH - 1) {
      // table has run out of room!
      isHunting = false;
      display.clear();
      display.println("Error - too many values!");
      display.drawLogBuffer(0, 0);
      display.display();
    }
    // add to array
    knownValues[lastZero] = last;
    knownValuesTally[lastZero] += 1;
  }
}

bool inShinyRange(long last){
  bool isInRange = false;
  for(int i=0; i<KNOWN_VALUES_LENGTH; i++){
    // don't bother checking zeros
    if (knownValues[i] == 0.0) {
      // array should be filled from start, so just leave
      break;
    }
    // check if last is within a particular range of each known value
    bool shorterThanShiny = last < (knownValues[i] + MIN_SHINY_TIME);
    bool longerThanAbility = last > (knownValues[i] + MAX_SHINY_TIME);
    isInRange = isInRange || (!shorterThanShiny && !longerThanAbility);
  }
  return isInRange;
}

void setupDisplay() {
  // setup display
  display.init();
//  display.flipScreenVertically();
  display.setContrast(255);
  display.clear();
  display.setLogBuffer(5, 30);
}

void setupWifi() {
  // Wifi setup
  display.print("Connecting to ");
  display.println(SECRET_SSID);
  display.drawLogBuffer(0, 0);
  display.display();
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    display.print(".");
    display.drawLogBuffer(0, 0);
    display.display();
  }
  display.println("WiFi connected.");
  display.println("IP address: ");
  display.println(WiFi.localIP());
  display.drawLogBuffer(0, 0);
  display.display();
}

void setupServer() {
  // start server
  server.on("/", handleJson);
  server.on("/reset", handleReset);
  server.on("/continue", handleContinue);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setupLuxSensor() {
  if(!tsl.begin()) {
    display.println("No TSL2561 detected. Check your wiring or I2C ADDR!");
    display.drawLogBuffer(0, 0);
    display.display();
    while(1);
  }
  // set up auto-gain for the lux meter
  tsl.enableAutoRange(true);
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);
}

void setup() {
  setupDisplay();
  setupWifi();
  setupServer();
  setupLuxSensor();
}

long updateLux() {
  sensors_event_t event;
  tsl.getEvent(&event);
  if (event.light) {
    return event.light;
  }
  return 0.0;
}

void postToTwilio() {
  http.begin(SECRET_TWILIO_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setAuthorization(SECRET_TWILIO_AUTH);
  int httpResponse = http.POST(SECRET_TWILIO_POST_BODY);
  display.clear();
  display.print("POST response: ");
  display.println(httpResponse);
  display.drawLogBuffer(0, 0);
  display.display();
  http.end();
}

void softResetLoop() {
  long timeSinceLast = millis() - lastLoopStart;
  if (resetState == StartLoop) {
    lastLoopStart = millis();
    display.clear();
    display.print("starting loop ");
    display.println(srCount);
    display.drawLogBuffer(0, 0);
    display.display();
    resetState = SkipIntro;
  }
  else if (resetState == SkipIntro && timeSinceLast > 8000) {
    // skip intro
    pressButton(aservo, A_PIN);
    resetState = OpenFile;
  }
  else if (resetState == OpenFile && timeSinceLast > 11000) {
    // open file
    pressButton(aservo, A_PIN);
    resetState = StartEncounter;
  }
  else if (resetState == StartEncounter && timeSinceLast > 16000) {
    display.clear();
    display.print("starting ");
    display.print(huntmode);
    display.println(" hunt");
    display.drawLogBuffer(0, 0);
    display.display();
    if (huntmode.equals("ambush")) {
      resetState = StartLuxMeter;
      // jump to lux phase after 9s delay
    }
    else if(huntmode.equals("overworld")) {
      resetState = OverworldStart;
      pressButton(aservo, A_PIN);
      resetState = StartLuxMeter;
    }
  }
  else if (resetState == StartLuxMeter) {
    display.clear();
    display.println("starting lux meter");
    display.drawLogBuffer(0, 0);
    display.display();
    lux = 0.0;
    lastLux = 0.0;
    resetState = MonitorLux;
  }
  else if (resetState == MonitorLux) {
    // monitor lux
    lastLux = lux;
    lux = updateLux();
    timeUntilOptions = millis() - timeLuxStart;
    if (lux > 49.0) {
      resetState = EndLoop;
    }
    else if (lux > 10.0 && lastLux < 4.0) {
      // screen goes from black to dark
      display.clear();
      display.println("measuring animation");
      display.drawLogBuffer(0, 0);
      display.display();
      timeLuxStart = millis();
    }
    else if (timeUntilOptions > 30000) {
      // something has snagged - a shiny animation should not take this long
      display.clear();
      display.println("unexpected error");
      display.drawLogBuffer(0, 0);
      display.display();
      resetState = StartLoop;
      pressButton(startservo, START_PIN);
      srCount += 1;
    }
  }
  else if (resetState == EndLoop) {
    resetState = StartLoop;
    lastMeasuredLoop = timeUntilOptions;
    int roundedTime = 10 * round(timeUntilOptions/10);
    display.clear();
    display.print("animation took ");
    display.print(roundedTime);
    display.println("ms");
    display.drawLogBuffer(0, 0);
    display.display();
    addKnownValue(roundedTime);
    bool shinyCheck = inShinyRange(roundedTime);
    if (shinyCheck) {
      display.clear();
      display.println("found shiny!");
      display.drawLogBuffer(0, 0);
      display.display();
      isHunting = false;
      postToTwilio();
    }
    else {
      pressButton(startservo, START_PIN);
      srCount += 1;
    }
  }
}

void loop() {
  server.handleClient();
  if (isHunting) {
    softResetLoop();
  }
}
