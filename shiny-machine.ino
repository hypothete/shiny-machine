#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "SSD1306Wire.h"
#include <Sparkfun_APDS9301_Library.h>
#include "arduino_secrets.h";

String version = "4.0.1";

// Initialize the OLED display using Wire library
SSD1306Wire  display(0x3c, 5, 4);

// Solenoids
#define PIN_START 12
#define PIN_X 13
#define PIN_RIGHT 14
#define PIN_A 15

// Luminosity sensor
APDS9301 apds;

// Webserver & retained data
WebServer server(80);
String huntmode = "honey";
bool isHunting = true;
int srCount = 0;
int timeoutCount = 0;
bool useWindow = false; // to limit successes to a particular window of time
int windowStart = 0;
int windowEnd = 30000;

// Webserver templates
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

// text message properties;
long nextText = 0.0;
bool outgoingText = false;
String textMsg = SECRET_TWILIO_POST_BODY;

enum loopState {
  StartLoop,
  SkipIntro,
  OpenFile,
  StartEncounter,
  HoneyStart,
  StartLuxMeter,
  MonitorLux,
  EndLoop
};

enum honeyState {
  GotoBag,
  OpenBag,
  GotoItems,
  SelectHoney,
  ConfirmHoney
};

loopState resetState = StartLoop;
honeyState honeyStep = GotoBag;

#define MIN_SHINY_TIME 1080
#define MAX_SHINY_TIME 1280

// cache of known values
#define KNOWN_VALUES_LENGTH 100
long knownValues[KNOWN_VALUES_LENGTH];
int knownValuesTally[KNOWN_VALUES_LENGTH];

// fire a solenoid
void pushButton(int pin) {
  display.print("pushing button ");
  display.println(pin);
  digitalWrite(pin, HIGH);
  delay(50);
  digitalWrite(pin, LOW);
}

void handleReset() {
  if (server.method() == HTTP_POST) {
    clearKnownValues();
    huntmode = server.arg("huntmode");
    srCount = 0;
    timeoutCount = 0;
    isHunting = true;
    String windowForm = server.arg("usewindow");
    useWindow = windowForm.equals("on");
    if (useWindow) {
      String startString = server.arg("windowstart");
      String endString = server.arg("windowend");
      windowStart = startString.toInt();
      windowEnd = endString.toInt();
    }
    resetState = StartLoop;
    honeyStep = GotoBag;
    server.send(200, "text/html", backPage);
    display.println("Restarting hunt");
    display.print("Setting hunt mode to ");
    display.println(huntmode);
    display.print("Window: ");
    display.println(windowForm);
    pushButton(PIN_START);
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
    honeyStep = GotoBag;
    server.send(200, "text/html", backPage);
    display.println("Continuing hunt");
    pushButton(PIN_START);
  }
  else {
    // weird verb - just redirect to form
    server.send(200, "text/html", redirectPage);
  }
}

void handlePause() {
  if (server.method() == HTTP_POST) {
    isHunting = false;
    resetState = StartLoop;
    honeyStep = GotoBag;
    server.send(200, "text/html", backPage);
    display.println("Pausing hunt");
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
  page += "\",\n";
  page += "  \"timeoutCount\": ";
  page += timeoutCount;
  page += ",\n";
  page += "  \"useWindow\": ";
  page += useWindow ? "true" : "false";
  page += ",\n";
  page += "  \"windowStart\": ";
  page += windowStart;
  page += ",\n";
  page += "  \"windowEnd\": ";
  page += windowEnd;
  page += "\n";
  page += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", page);
}

void handleNotFound() {
  server.send(404, "text/plain", "File not found\n");
  display.println("Sent 404");
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
      display.println("Error: too many values!");
    }
    // add to array
    knownValues[lastZero] = last;
    knownValuesTally[lastZero] += 1;
  }
}

bool inWindow(long last) {
  if (!useWindow) {
    return false;
  }
  return last > windowStart && last < windowEnd;
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

void setupSolenoids() {
  pinMode(PIN_START, OUTPUT);
  pinMode(PIN_X, OUTPUT);
  pinMode(PIN_RIGHT, OUTPUT);
  pinMode(PIN_A, OUTPUT);
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
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.begin(SECRET_SSID, SECRET_PASSWORD);
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

void setupServer() {
  // start server
  server.on("/", handleJson);
  server.on("/reset", handleReset);
  server.on("/continue", handleContinue);
  server.on("/pause", handlePause);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setupLuxSensor() {
  if (apds.begin(0x39)) {
    display.println("Error with lux sensor");
    while(1);
  }
}

void setup() {
  setupSolenoids();
  setupDisplay();
  setupWifi();
  setupServer();
  setupLuxSensor();
}

long updateLux() {
  return apds.readLuxLevel();
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
      nextText = millis() + 60000.0;
      display.print("Twilio error: ");
      display.println(httpResponse);
    }
    else {
      outgoingText = false;
      display.println("Sent text!");
    }
  }
}

void softResetLoop() {
  long timeSinceLast = millis() - lastLoopStart;
  if (resetState == StartLoop) {
    lastLoopStart = millis();
    display.print("starting loop ");
    display.println(srCount);
    resetState = SkipIntro;
  }
  else if (resetState == SkipIntro && timeSinceLast > 8000) {
    // skip intro
    pushButton(PIN_A);
    resetState = OpenFile;
  }
  else if (resetState == OpenFile && timeSinceLast > 11000) {
    // open file
    pushButton(PIN_A);
    resetState = StartEncounter;
  }
  else if (resetState == StartEncounter && timeSinceLast > 16000) {
    display.print("starting ");
    display.print(huntmode);
    display.println(" hunt");
    if (huntmode.equals("ambush")) {
      resetState = StartLuxMeter;
      // jump to lux phase after 9s delay
    }
    else if(huntmode.equals("overworld")) {
      pushButton(PIN_A);
      resetState = StartLuxMeter;
    }
    else if(huntmode.equals("honey")) {
      pushButton(PIN_X);
      honeyStep = GotoBag;
      resetState = HoneyStart;
    }
  }
  else if (resetState == HoneyStart) {
    if (honeyStep == GotoBag && timeSinceLast > 17000){
      pushButton(PIN_RIGHT);
      honeyStep = OpenBag;
    }
    else if (honeyStep == OpenBag && timeSinceLast > 18000) {
      pushButton(PIN_A);
      honeyStep = GotoItems;
    }
    else if (honeyStep == GotoItems && timeSinceLast > 20000) {
      pushButton(PIN_RIGHT);
      honeyStep = SelectHoney;
    }
    else if(honeyStep == SelectHoney && timeSinceLast > 21000) {
      pushButton(PIN_A);
      honeyStep = ConfirmHoney;
    }
    else if (honeyStep == ConfirmHoney && timeSinceLast > 22000) {
      pushButton(PIN_A);
      resetState = StartLuxMeter;
    }
  }
  else if (resetState == StartLuxMeter) {
    display.println("starting lux meter");
    lux = 0.0;
    lastLux = 0.0;
    resetState = MonitorLux;
  }
  else if (resetState == MonitorLux) {
    // monitor lux
    lastLux = lux;
    lux = updateLux();
    timeUntilOptions = millis() - timeLuxStart;
    if (lux > 40.0) {
      resetState = EndLoop;
    }
    else if (lux > 10.0 && lastLux < 4.0) {
      // screen goes from black to dark
      display.println("measuring animation");
      timeLuxStart = millis();
    }
    else if (timeUntilOptions > 30000) {
      // something has snagged - a shiny animation should not take this long
      display.println("loop timed out");
      resetState = StartLoop;
      pushButton(PIN_START);
      srCount += 1;
      timeoutCount += 1;
    }
  }
  else if (resetState == EndLoop) {
    resetState = StartLoop;
    lastMeasuredLoop = timeUntilOptions;
    int roundedTime = 10 * round(timeUntilOptions/10);
    display.print("animation took ");
    display.print(roundedTime);
    display.println("ms");
    addKnownValue(roundedTime);
    bool windowCheck = inWindow(roundedTime);
    bool shinyCheck = inShinyRange(roundedTime);
    if (shinyCheck) {
      display.println("found shiny!");
      isHunting = false;
      postToTwilio("Shiny found!");
    }
    else if(windowCheck) {
      display.println("paused in window");
      isHunting = false;
      postToTwilio("Paused in window.");
    }
    else {
      pushButton(PIN_START);
      srCount += 1;
    }
  }
}

void loop() {
  reconnectWifi();
  checkTwilio();
  server.handleClient();
  if (isHunting) {
    softResetLoop();
  }
  display.clear();
  display.drawLogBuffer(0, 0);
  display.display(); 
}
