#include <WiFi.h>
#include <HTTPClient.h>
#include "SSD1306Wire.h"
#include <Sparkfun_APDS9301_Library.h>
#include "arduino_secrets.h";

String version = "4.0.1";

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

// timing for non-blocking loop
long lastLoopStart = 0.0;
long lux = 0.0;
long lastLux = 0.0;

// text message properties;
long nextText = 0.0;
bool outgoingText = false;
String textMsg = SECRET_TWILIO_POST_BODY;

enum loopState {
  StartLoop,
  StartGame,
  SelectFile,
  OpenFile,
  InteractShrine,
  Read1,
  Read2,
  Read3,
  Read4,
  Read5,
  Read6,
  Read7,
  SayYes,
  ConfirmBall,
  MonitorLux,
  MonitorShiny
};

loopState resetState = StartLoop;

// fire a solenoid
void pushButton(int pin) {
  display.print("pushing button ");
  display.println(pin);
  digitalWrite(pin, HIGH);
  delay(100);
  digitalWrite(pin, LOW);
}

void resetGame() {
  pushButton(PIN_SCREEN);
  delay(1500);
  pushButton(PIN_SCREEN);
  delay(1000);
  pushButton(PIN_SCREEN);
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
}

void setup() {
  setupSolenoids();
  setupDisplay();
  setupWifi();
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
    resetState = StartGame;
  }
  else if (resetState == StartGame && timeSinceLast > 6000) {
    pushButton(PIN_A);
    resetState = SelectFile;
  }
  else if (resetState == SelectFile && timeSinceLast > 9000) {
    pushButton(PIN_A);
    resetState = OpenFile;
  }
  else if (resetState == OpenFile && timeSinceLast > 11000) {
    // open file
    pushButton(PIN_A);
    resetState = InteractShrine;
  }
  else if (resetState == InteractShrine && timeSinceLast > 13000) {
    pushButton(PIN_A);
    resetState = Read1;
    display.print("entering read1");
  }
  else if (resetState == Read1 && timeSinceLast > 15000) {
    pushButton(PIN_A);
    resetState = Read2;
  }
  else if (resetState == Read2 && timeSinceLast > 16000) {
    pushButton(PIN_A);
    resetState = Read3;
  }
  else if (resetState == Read3 && timeSinceLast > 17000) {
    pushButton(PIN_A);
    resetState = Read4;
  }
  else if (resetState == Read4 && timeSinceLast > 18000) {
    pushButton(PIN_A);
    resetState = Read5;
  }
  else if (resetState == Read5 && timeSinceLast > 19000) {
    pushButton(PIN_A);
    resetState = Read6;
  }
  else if (resetState == Read6 && timeSinceLast > 20000) {
    pushButton(PIN_A);
    resetState = Read7;
  }
  else if (resetState == Read7 && timeSinceLast > 21000) {
    pushButton(PIN_A);
    resetState = SayYes;
  }
  else if (resetState == SayYes && timeSinceLast > 22000) {
    pushButton(PIN_A);
    resetState = ConfirmBall;
  }
  else if (resetState == ConfirmBall && timeSinceLast > 23000) {
    pushButton(PIN_A);
    resetState = MonitorLux;
  }
  else if (resetState == MonitorLux) {
    display.println("starting lux meter");
    lux = 0.0;
    lastLux = 0.0;
    resetState = MonitorShiny;
  }
  else if (resetState == MonitorShiny && timeSinceLast > 38000) {
    // monitor lux
    lastLux = lux;
    lux = updateLux();
    display.println(lux);
    if (lux < 20.0 && lastLux > 20.0) {
      // screen goes from light to dark
      display.println("found shiny!");
      isHunting = false;
      postToTwilio("Shiny found!");
      
    }
    else if (timeSinceLast > 42000) {
      display.println("loop timed out");
      resetState = StartLoop;
      srCount += 1;
      resetGame();
    }
  }
}

void loop() {
  reconnectWifi();
  checkTwilio();
  if (isHunting) {
    softResetLoop();
  }
  display.clear();
  display.drawLogBuffer(0, 0);
  display.display(); 
}
