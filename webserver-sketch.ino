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

// Initialize the OLED display using Wire library
SSD1306Wire  display(0x3c, 5, 4);

// Servo stuff
Servo startservo;
Servo aservo;
int A_PIN = 12;
int START_PIN = 2;

// Luminosity sensor
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

// Webserver & retained data
WebServer server(80);
bool isAmbush = true;
bool isHunting = true;
int srCount = 0;
String redirectPage = "<html><head><title>Redirecting...</title><meta http-equiv='refresh' content='1;url=/' /></head><body></body></html>";

// HTTP client for Twilio
HTTPClient http;

// timing for non-blocking loop
long lastLoopStart = 0.0;
long timeLuxStart = 0.0;
int resetState = 0;
long timeUntilOptions = 0.0;
long lastMeasuredLoop = 0.0;

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

void handleRoot() {
  if (server.method() == HTTP_GET) {
    long maxVal = 0;
    long minVal = 30000.0;
    int maxValTally = 0;
    for (int i=0; i< KNOWN_VALUES_LENGTH; i++) {
      long d = knownValues[i];
      if (d == 0.0) {
        break;
      }
      if (d < minVal) {
        minVal = d;
      }
      if (d > maxVal) {
        maxVal = d;
      }
      int e = knownValuesTally[i];
      if (e > maxValTally) {
        maxValTally = e;
      }
    }
    // round values for better graphing
    minVal -= 100; // to give some room around measurements
    maxValTally = maxValTally * 2; // this should work well with log scaling

    String page = "<html><head><title>Shiny Hunting Machine</title></head><body>";
    page += "<h1>Shiny Hunting Machine v2.2.0</h1>";
    page += "<h2>Reset #";
    page += srCount;
    page += ", ";
    page += (isHunting ? "still hunting" : "found shiny!");
    page += "</h2>";
    page += "<h2>Last measured loop took ";
    page += lastMeasuredLoop;
    page += "ms</h2>";
    page += "<form action='/' method='post'>";
    page += "<label for='ambush'>Ambush encounter? </label>";
    page += "<input type='checkbox' name='ambush' ";
    page += isAmbush ? "checked" : "";
    page += " />";
    page += "<br /><br />";
    page += "<input type='submit' value='Reset Hunt' />";
    page += "</form>";
    page += "<canvas width='500' height='150'></canvas>";
    page += "<br /><br /><a id='dl-link' download='shiny-machine.json'>Download data</a>";
    page += "<script type='text/javascript'>";
    page += "  let vals = [";
    for(int i=0; i<KNOWN_VALUES_LENGTH; i++){
      if (knownValues[i] == 0.0) {
        break;
      }
      if (i > 0 && i < KNOWN_VALUES_LENGTH - 1) {
        page += ", ";
      }
      page += knownValues[i];
    }
    page += "];\n";
    page += "  let valTallies = [";
    for(int i=0; i<KNOWN_VALUES_LENGTH; i++){
      if (knownValuesTally[i] == 0.0) {
        break;
      }
      if (i > 0 && i < KNOWN_VALUES_LENGTH - 1) {
        page += ", ";
      }
      page += knownValuesTally[i];
    }
    page += "];\n";
    page += "  const minVal = ";
    page += minVal;
    page += ";\n";
    page += "  const maxVal = ";
    page += maxVal;
    page += ";\n";
    page += "  const maxValTally = ";
    page += maxValTally;
    page += ";\n";
    page += "const margin = 20;\n";
    page += "const can = document.querySelector('canvas');\n";
    page += "const ctx = can.getContext('2d');\n";
    page += "ctx.strokeStyle = 'black';\n";
    page += "ctx.fillStyle = 'rgba(0, 192, 255, 0.2)';\n";
    page += "const w = can.width;\n";
    page += "const h = can.height;\n";
    page += "const scaleX = item => {\n";
    page += "  return (w - margin) * (item - minVal)/((maxVal + ";
    page + MAX_SHINY_TIME;
    page += ") - minVal) + margin;\n";
    page += "};\n";
    page += "const scaleY = item => {\n";
    page += "  return -(h - margin) * (Math.log(item + 1) / Math.log(maxValTally)) + h - margin;\n";
    page += "};\n";
    page += "ctx.beginPath();\n";
    page += "ctx.moveTo(margin, 0);\n";
    page += "ctx.lineTo(margin, h - margin);\n";
    page += "ctx.lineTo(w, h - margin);\n";
    page += "for(let i=0; i<=10; i++) {\n";
    page += "  const tickVal = minVal + (i * (maxVal + ";
    page += MAX_SHINY_TIME;
    page += " - minVal)/10);\n";
    page += "  const tx = scaleX(tickVal);\n";
    page += "  ctx.moveTo(tx, h - margin);\n";
    page += "  ctx.lineTo(tx, h - margin + 10);\n";
    page += "  ctx.strokeText(tickVal,tx + 1, h);\n";
    page += "}\n";
    page += "const mlt = Math.floor(Math.log(maxValTally));\n";
    page += "for(let i=0; i<=mlt; i++){\n";
    page += "  const tickVal = Math.pow(mlt, i);\n";
    page += "  const tx = scaleY(tickVal);\n";
    page += "  ctx.moveTo(margin - 10, tx);\n";
    page += "  ctx.lineTo(margin, tx);\n";
    page += "  ctx.strokeText(tickVal, 1, tx + 10);\n";
    page += "}\n";
    page += "vals.forEach((val, index) => {\n";
    page += "  const valTal = valTallies[index];\n";
    page += "  const scaledVal = scaleX(val);\n";
    page += "  const scaledTal = scaleY(valTal);\n";
    page += "  const scaledMinTal = scaleY(0);\n";
    page += "  const shinyStart = scaleX(val + ";
    page += MIN_SHINY_TIME;
    page += ");\n";
    page += "  const shinyEnd = scaleX(val + ";
    page += MAX_SHINY_TIME;
    page += ");\n";
    page += "  ctx.moveTo(scaledVal, scaledMinTal);\n";
    page += "  ctx.lineTo(scaledVal, scaledTal);\n";
    page += "  ctx.fillRect(shinyStart, scaledTal, shinyEnd - shinyStart, scaledMinTal - scaledTal);\n";
    page += "});\n";
    page += "ctx.stroke();\n";
    page += "const dlLink = document.querySelector('#dl-link');\n";
    page += "dlLink.setAttribute('href', 'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify({ values: vals, tallies: valTallies })));\n";
    page += "</script>\n";
    page += "</body></html>";
    server.send(200, "text/html", page);
  }
  else if (server.method() == HTTP_POST) {
    String ambush = server.arg("ambush");
    clearKnownValues();
    isAmbush = ambush.equals("on");
    srCount = 0;
    isHunting = true;
    resetState = 0;
    server.send(200, "text/html", redirectPage);
    display.clear();
    display.println("Restarting hunt...");
    display.drawLogBuffer(0, 0);
    display.display();
    pressButton(startservo, START_PIN);
  }
  else {
    // weird verb - just redirect to form
    server.send(200, "text/html", redirectPage);
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "File not found\n");
  display.clear();
  display.println("Bad request");
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
  server.on("/", handleRoot);
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
  long lux = 0.0;
  long timeSinceLast = millis() - lastLoopStart;
  if (resetState == 0) {
    lastLoopStart = millis();
    display.clear();
    display.print("starting loop ");
    display.println(srCount);
    display.drawLogBuffer(0, 0);
    display.display();
    resetState = 1;
  }
  else if (resetState == 1 && timeSinceLast > 8000) {
    // skip intro
    pressButton(aservo, A_PIN);
    resetState = 2;
  }
  else if (resetState == 2 && timeSinceLast > 11000) {
    // open file
    pressButton(aservo, A_PIN);
    resetState = 3;
  }
  else if (resetState == 3) {
    if (isAmbush && timeSinceLast > 20000) {
      resetState = 6;
      // jump to lux phase after 9s delay
    }
    else if(!isAmbush && timeSinceLast > 16000){
     resetState = 4;
     // 5s delay
    }
  }
  else if (resetState == 4 && timeSinceLast > 21000) {
    // ultra beast, press A to interact
    resetState = 5;
    pressButton(aservo, A_PIN);
  }
  else if (resetState == 5 && timeSinceLast > 26000) {
    // 5s wait after pressing A
    resetState = 6;
  }
  else if (resetState == 6) {
    display.clear();
    display.println("monitoring lux");
    display.drawLogBuffer(0, 0);
    display.display();
    timeLuxStart = millis();
    resetState = 7;
  }
  else if (resetState == 7) {
    // monitor lux
    lux = updateLux();
    timeUntilOptions = millis() - timeLuxStart;
    if (lux > 49.0) {
      resetState = 8;
    }
    else if (timeUntilOptions > 30000) {
      // something has snagged - a shiny animation should not take this long
      display.clear();
      display.println("Unexpected error");
      display.drawLogBuffer(0, 0);
      display.display();
      resetState = 0;
      pressButton(startservo, START_PIN);
      srCount += 1;
    }
  }
  else if (resetState == 8) {
    resetState = 0;
    lastMeasuredLoop = timeUntilOptions;
    int roundedTime = 10 * round(timeUntilOptions/10);
    display.clear();
    display.print("loop ");
    display.print(srCount);
    display.print(" took ");
    display.print(roundedTime);
    display.println("ms");
    display.drawLogBuffer(0, 0);
    display.display();
    addKnownValue(roundedTime);
    bool shinyCheck = inShinyRange(roundedTime);
    if (shinyCheck) {
      display.clear();
      display.println("Found shiny!");
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
