#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <DNSServer.h>

#define LGFX_ESP32_S3_BOX_V3
#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>

static LGFX lcd;
static LGFX_Sprite canvas(&lcd);

// ---------------- Display ----------------
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
uint8_t currentPage = 0;
const uint8_t TOTAL_PAGES = 6;

// ---------------- Sensor Values ----------------
float temperature = 25;
int humidity = 70;
int co = 5;
int no2 = 0;

// ---------------- WiFi Config ----------------
#define TRIGGER_PIN 21
#define CONFIG_FILE "/config.json"
#define AP_SSID "ESP32-Setup"
const char* apiEndpoint = "/api/sensor-history";

struct Config {
  char wifi_ssid[33] = "edison science corner";
  char wifi_password[65] = "eeeeeeee";
  char server_url[100] = "https://air-buddy.onrender.com";
  char access_token[70] = "API_KEY";
};

Config config;
AsyncWebServer server(80);
DNSServer dnsServer;
bool inConfigMode = false;

// ---------------- Timing ----------------
unsigned long lastSensorUpdate = 0;
unsigned long lastUpload = 0;
bool lastTouch = false;

// ---------- Function Declarations ----------
void drawPage(uint8_t page);
bool isTouched();
void drawGauge(int cx,int cy,int r,int t,int val,int maxV,uint32_t col,const char* lab,const char* unit);
void drawRectGauge(int x,int y,int w,int h,int val,int maxV,uint32_t col,const char* lab,const char* unit);

bool loadConfig();
bool saveConfig();
void startConfigMode();
bool connectToWiFi();
void sendSensorData();

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.setBrightness(255);
  canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);

  if (!LittleFS.begin()) LittleFS.format();

  bool triggerActive = digitalRead(TRIGGER_PIN) == HIGH;
  bool configLoaded = loadConfig();

  if (triggerActive || !configLoaded) {
    startConfigMode();
    return;
  }

  if (!connectToWiFi()) {
    startConfigMode();
    return;
  }

  drawPage(currentPage);
}

// =====================================================
// LOOP
// =====================================================
void loop() {

  // ---------- Captive portal ----------
  if (inConfigMode) {
    dnsServer.processNextRequest();
    return;
  }

  // ---------- Touch page switch ----------
  bool touchNow = isTouched();
  if (touchNow && !lastTouch) {
    currentPage = (currentPage + 1) % TOTAL_PAGES;
    drawPage(currentPage);
    delay(200);
  }
  lastTouch = touchNow;

  // ---------- Fake sensor update every 2s ----------
  if (millis() - lastSensorUpdate > 2000) {
    lastSensorUpdate = millis();

    temperature = random(180, 281) / 10.0;
    humidity = random(30, 71);
    co = random(0, 11);
    no2 = random(0, 51);

    drawPage(currentPage);
  }

  // ---------- Upload every 10s ----------
  if (millis() - lastUpload > 10000 && WiFi.status() == WL_CONNECTED) {
    lastUpload = millis();
    sendSensorData();
  }
}

// =====================================================
// DISPLAY
// =====================================================
bool isTouched() {
  uint16_t x, y;
  return lcd.getTouch(&x, &y);
}

void drawPage(uint8_t page) {

  canvas.clear(TFT_BLACK);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold24pt7b);

  switch (page) {

    case 0:
      canvas.setTextColor(TFT_RED);
      canvas.drawString("TEMP",160,100);
      canvas.drawString(String(temperature,1)+" C",160,150);
      break;

    case 1:
      canvas.setTextColor(TFT_CYAN);
      canvas.drawString("HUMIDITY",160,100);
      canvas.drawString(String(humidity)+" %",160,150);
      break;

    case 2:
      canvas.setTextColor(TFT_GREEN);
      canvas.drawString("NO2",160,100);
      canvas.drawString(String(no2)+" ppm",160,150);
      break;

    case 3:
      canvas.setTextColor(TFT_YELLOW);
      canvas.drawString("CO",160,100);
      canvas.drawString(String(co)+" ppm",160,150);
      break;

    case 4:
      drawGauge(80,90,48,10,temperature,150,TFT_RED,"TEMP","C");
      drawGauge(240,90,48,10,humidity,100,TFT_CYAN,"HUM","%");
      drawGauge(80,190,48,10,no2,100,TFT_GREEN,"NO2","ppm");
      drawGauge(240,190,48,10,co,100,TFT_YELLOW,"CO","ppm");
      break;

    case 5:
      drawRectGauge(40,40,200,20,temperature,150,TFT_RED,"TEMP","C");
      drawRectGauge(40,90,200,20,humidity,100,TFT_CYAN,"HUM","%");
      drawRectGauge(40,140,200,20,no2,100,TFT_GREEN,"NO2","ppm");
      drawRectGauge(40,190,200,20,co,100,TFT_YELLOW,"CO","ppm");
      break;
  }

  canvas.pushSprite(0,0);
}

// ---------- Gauges ----------
void drawGauge(int cx,int cy,int r,int t,int val,int maxV,uint32_t col,const char* lab,const char* unit){
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawString(lab,cx,cy-r-18);

  for(int i=0;i<t;i++) canvas.drawArc(cx,cy,r-i,r-i-1,135,405,TFT_DARKGREY);

  int ang = map(val,0,maxV,135,405);
  for(int i=0;i<t;i++) canvas.drawArc(cx,cy,r-i,r-i-1,135,ang,col);

  canvas.drawString(String(val),cx,cy-5);
  canvas.drawString(unit,cx,cy+18);
}

void drawRectGauge(int x,int y,int w,int h,int val,int maxV,uint32_t col,const char* lab,const char* unit){
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(top_left);
  canvas.drawString(lab,x,y-14);

  canvas.fillRoundRect(x,y,w,h,6,TFT_DARKGREY);
  int fillW = map(val,0,maxV,0,w);
  canvas.fillRoundRect(x,y,fillW,h,6,col);

  canvas.setTextDatum(middle_right);
  canvas.drawString(String(val)+" "+unit,x+w+60,y+h/2);
}

// =====================================================
// WIFI + CONFIG
// =====================================================
void startConfigMode() {
  inConfigMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.onNotFound([](AsyncWebServerRequest *r){
    r->send(LittleFS,"/index.html","text/html");
  });

  server.begin();
}

bool loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) return false;

  File f = LittleFS.open(CONFIG_FILE,"r");
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc,f)) return false;

  strlcpy(config.wifi_ssid, doc["wifi_ssid"] | "", sizeof(config.wifi_ssid));
  strlcpy(config.wifi_password, doc["wifi_password"] | "", sizeof(config.wifi_password));
  strlcpy(config.server_url, doc["server_url"] | "", sizeof(config.server_url));
  strlcpy(config.access_token, doc["access_token"] | "", sizeof(config.access_token));

  return strlen(config.wifi_ssid) > 0;
}

bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid, config.wifi_password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - start > 20000) return false;
  }
  return true;
}

// =====================================================
// CLOUD UPLOAD
// =====================================================
void sendSensorData() {

  HTTPClient http;
  String url = String(config.server_url) + apiEndpoint;

  http.begin(url);
  http.addHeader("Content-Type","application/json");
  http.addHeader("X-API-Key", config.access_token);

  StaticJsonDocument<200> doc;
  JsonObject v = doc.createNestedObject("values");
  v["Temperature"] = temperature;
  v["Humidity"] = humidity;
  v["CO"] = co;
  v["NO2"] = no2;

  String payload;
  serializeJson(doc, payload);

  http.POST(payload);
  http.end();
}
