#include <WiFi.h>
#include <HTTPClient.h>
#include "SSD1306Wire.h"
#include <WebServer.h>
#include "arduino_secrets.h";

String version = "4.1.2";

// Initialize the OLED display using Wire library
SSD1306Wire  display(0x3c, 5, 4);

// Solenoids
#define PIN_SCREEN 14
#define PIN_A 12

// retained data
bool isHunting = true;
int srCount = 0;
int retryCount = 0; // retries for calls to camera

//Webserver for reading measurements
WebServer server(80);

// timing for non-blocking loop
long lastLoopStart = 0;

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
  MonitorShiny
};

loopState resetState = StartLoop;

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

void handleContinue() {
  server.send(200, "text/plain", "");
  display.println("Reset from web");
  resetState = StartLoop;
  srCount += 1;
  resetGame();
  isHunting = true;
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
    int tries = 0;
    WiFi.begin(SECRET_SSID, SECRET_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      display.print(".");
      display.drawLogBuffer(0, 0);
      display.display();
      tries++;
      if (tries > 30) {
        reconnectWifi();
      }
    }
    display.println("WiFi connected.");
    display.println(WiFi.localIP());
  }
}

void setupWebserver() {
  server.on("/", handleRoot);
  server.on("/continue", handleContinue);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  setupSolenoids();
  setupDisplay();
  setupWifi();
  setupWebserver();
}

void postToTwilio(String body) {
  textMsg = SECRET_TWILIO_POST_BODY;
  textMsg += body;
  outgoingText = true;
}

bool getShinyFromCam() {
  HTTPClient http;
  bool isShiny = true;
  // assume true until proven otherwise
  // for error identification
  http.begin(ESP_CAM_IP);
  int httpCode = http.GET();
  display.print("Response: ");
  display.println(httpCode);
  if (httpCode == 200) {
    String payload = http.getString();
    http.end();
    display.print("Code: ");
    display.println(payload);
    if (!payload.equals("1")) {
      isShiny = false;
    }
  }
  else if(httpCode == 500){
    // error at the camera
    String payload = http.getString();
    http.end();
    display.println("Camera error:");
    display.println(payload);
  }
  else {
    // probably a request timeout
    http.end();
    if (retryCount > 10) {
      display.println("Too many camera timeouts");
      return true;
    }
    display.println("Camera timeout, retrying");
    retryCount++;
    return getShinyFromCam();
  }
  return isShiny;
}

void checkTwilio() {
  if(outgoingText && (nextText < millis())) {
    HTTPClient http;
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

void softResetLoop() {
  long timeSinceLast = millis() - lastLoopStart;
  if (resetState == StartLoop) {
    lastLoopStart = millis();
    display.print("Starting loop ");
    display.println(srCount);
    resetState = StartGame;
    retryCount = 0;
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
    resetState = MonitorShiny;
  }
  else if (resetState == MonitorShiny && timeSinceLast > 41000) {
    display.clear();
    display.println("Checking camera");
    display.drawLogBuffer(0, 0);
    display.display();
    bool isShiny = getShinyFromCam();
    if(isShiny) {
      // deviation from standard lux curve
      display.println("Found shiny!");
      isHunting = false;
      postToTwilio("Shiny found!");
    }
    else {
      display.println("Loop timed out");
      resetState = StartLoop;
      srCount += 1;
      resetGame();
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
