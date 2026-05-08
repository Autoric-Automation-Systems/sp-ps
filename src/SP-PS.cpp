/*  Smart Plant APP
    https://smartplant.app.br/

    Module:
    SP-PS | Smart Plant Production States
    ESP32-C3 DevKitM-1
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// ================= CONFIG =================
#define VERSION "1.0"
#define DEVICE_TYPE "SP-PS"

RTC_DATA_ATTR unsigned long heartbeatInterval = 10000;
RTC_DATA_ATTR unsigned long counterInterval = 2;

unsigned long accumulatedPulses = 0;

// ********* Local Test ****************
bool isLocal = false;

String baseUrl = isLocal
                     ? "http://192.168.100.103:3000"
                     : "https://smartplant.app.br";

String apiHeartbeat = baseUrl + "/api/devices/heartbeat";
String apiEvents = baseUrl + "/api/devices/events";
//**************************************

//================ NTP ================
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;
const int daylightOffset_sec = 0;

//================ GPIO =================
#define COUNTER_PIN 5
#define PIN_EVENT1 6
#define PIN_EVENT2 7
#define PIN_EVENT3 10

//==============================================

unsigned long lastHeartbeat = 0;
int lastWifi = -100;
String mac;
volatile unsigned long pulseCount = 0;

bool lastEvent1;
bool lastEvent2;
bool lastEvent3;

// ================= EVENT =================
struct Event
{
  String name;
  float value;
  unsigned long ts;
};

Event buffer[100];
int bufferIndex = 0;

//========== ISR (interrupção)
void IRAM_ATTR pulseCounter()
{
  static unsigned long lastInterrupt = 0;
  unsigned long now = millis();
  if (now - lastInterrupt > 10)
  {
    pulseCount++;
    lastInterrupt = now;
  }
}

// ================= NTP =================
bool timeSynced = false;
void setupTime()
{
  if (timeSynced)
    return;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.print("Sincronizando NTP");
  time_t now = time(nullptr);
  int retry = 0;
  while (now < 100000 && retry < 20)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    retry++;
  }
  Serial.println();
  if (now < 100000)
  {
    Serial.println("Falha NTP");
  }
  else
  {
    Serial.println("NTP OK");
    Serial.println(ctime(&now));
  }
  if (now >= 100000)
    timeSynced = true;
}

// ================= ADD EVENT =================
void addEvent(String name, float value)
{
  if (bufferIndex >= 100)
  {
    Serial.println("BUFFER CHEIO");
    return;
  }
  float rounded = round(value * 100.0) / 100.0;
  time_t now = time(nullptr);
  if (now < 100000)
  {
    Serial.println("Sem tempo válido ainda");
    return;
  }
  buffer[bufferIndex].name = name;
  buffer[bufferIndex].value = rounded;
  buffer[bufferIndex].ts = now;
  Serial.print("Evento: ");
  Serial.print(name);
  Serial.print(" = ");
  Serial.println(rounded);
  bufferIndex++;
}

// ================= SEND EVENTS =================
void sendEvents()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (bufferIndex == 0)
    return;
  Serial.println("Enviando eventos " + apiEvents);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, apiEvents);
  http.addHeader("Content-Type", "application/json");
  DynamicJsonDocument doc(2048);
  doc["mac"] = mac;
  JsonArray events = doc.createNestedArray("events");
  for (int i = 0; i < bufferIndex; i++)
  {
    JsonObject ev = events.createNestedObject();
    ev["name"] = buffer[i].name;
    ev["value"] = buffer[i].value;
    ev["ts"] = buffer[i].ts;
  }
  String body;
  serializeJson(doc, body);
  Serial.println(body);
  int httpCode = http.POST(body);
  Serial.print("Events HTTP: ");
  Serial.println(httpCode);
  if (httpCode > 0)
  {
    Serial.println(http.getString());
    Serial.println();
  }
  http.end();
  bufferIndex = 0;
}

// ================= SEND HEARTBEAT =================
void sendHeartbeat()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  Serial.println("Enviando heartbeat " + apiHeartbeat);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, apiHeartbeat);
  http.addHeader("Content-Type", "application/json");
  DynamicJsonDocument doc(256);
  doc["mac"] = mac;
  doc["type"] = DEVICE_TYPE;
  doc["version"] = VERSION;
  doc["wifi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["uptime"] = millis();
  String body;
  serializeJson(doc, body);
  Serial.println(body);
  int httpCode = http.POST(body);
  Serial.print("Heartbeat HTTP: ");
  Serial.println(httpCode);
  if (httpCode > 0)
  {
    String payload = http.getString();
    Serial.println(payload);
    DynamicJsonDocument resp(256);
    deserializeJson(resp, payload);
    if (resp["heartbeatInterval"])
    {
      unsigned long newValue = resp["heartbeatInterval"];
      if (newValue != heartbeatInterval)
      {
        heartbeatInterval = newValue; // RTC_DATA_ATTR persiste automaticamente
        Serial.print("Novo intervalo de heartbeat salvo: ");
        Serial.println(heartbeatInterval);
        Serial.println();
      }
      else
      {
        Serial.println("Não foi necessário atualizar heartbeatInterval");
        Serial.println();
      }
    }
    if (resp["counterInterval"])
    {
      unsigned long newValue = resp["counterInterval"];
      if (newValue != counterInterval)
      {
        counterInterval = newValue; // RTC_DATA_ATTR persiste automaticamente
        Serial.print("Novo intervalo de contagem salvo: ");
        Serial.println(counterInterval);
        Serial.println();
      }
      else
      {
        Serial.println("Não foi necessário atualizar counterInterval");
        Serial.println();
      }
    }
  }
  http.end();
}

// =================Check  WIFI =================
void checkWifi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi não conectado");
    return;
  }
  int wifi = WiFi.RSSI();
  if (wifi > 0 || wifi < -100)
  {
    Serial.print("RSSI inválido: ");
    Serial.println(wifi);
    return;
  }
  if (abs(wifi - lastWifi) > 2)
  {
    addEvent("wifi", wifi);
    lastWifi = wifi;
  }
}

//=========== Conect WIFI =======================
void connectWifi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;
  Serial.println("Ligando WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20)
  {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi conectado");
  }
  else
  {
    Serial.println("Falha WiFi");
  }
}

//======== Disconect WIFI =======================
void disconnectWifi()
{
  Serial.println("Desligando WiFi...");
  WiFi.mode(WIFI_OFF);
  int retry = 0;
  while (WiFi.status() == WL_CONNECTED && retry < 20)
  {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi ainda conectado");
  }
  else
  {
    Serial.println("WiFi Desligado");
  }
}

//================= Start Message ================
void startmsg()
{
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 3000)
  {
  }
  Serial.println();
  Serial.println();
  Serial.print("Boot Smart Plant Version: ");
  Serial.print(VERSION);
  Serial.print(" Type: ");
  Serial.println(DEVICE_TYPE);
  Serial.println();
}

void checkCounter()
{
  noInterrupts();
  unsigned long pulses = pulseCount;
  pulseCount = 0;
  interrupts();
  if (pulses == 0)
    return;
  accumulatedPulses += pulses;
  Serial.print("Pulsos: ");
  Serial.print(pulses);
  Serial.print(" | Acumulado: ");
  Serial.println(accumulatedPulses);
  while (accumulatedPulses >= counterInterval)
  {
    addEvent("counter", counterInterval);
    accumulatedPulses -= counterInterval;
  }
}
void checkEvents()
{

  bool e1 = !digitalRead(PIN_EVENT1);
  bool e2 = !digitalRead(PIN_EVENT2);
  bool e3 = !digitalRead(PIN_EVENT3);

  if (e1 != lastEvent1)
  {
    addEvent("event1", e1);
    lastEvent1 = e1;
  }

  if (e2 != lastEvent2)
  {
    addEvent("event2", e2);
    lastEvent2 = e2;
  }

  if (e3 != lastEvent3)
  {
    addEvent("event3", e3);
    lastEvent3 = e3;
  }
}
void setup()
{
  startmsg();

  WiFiManager wm;

  if (!wm.autoConnect("SmartPlant SP-PS"))
  {
    ESP.restart();
  }
  Serial.println();
  Serial.println("WiFi conectado");

  mac = WiFi.macAddress();
  Serial.print("MAC: ");
  Serial.println(mac);
  Serial.println();

  setupTime();

  pinMode(COUNTER_PIN, INPUT_PULLUP);
  pinMode(PIN_EVENT1, INPUT_PULLUP);
  pinMode(PIN_EVENT2, INPUT_PULLUP);
  pinMode(PIN_EVENT3, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(COUNTER_PIN), pulseCounter, RISING);

  lastEvent1 = !digitalRead(PIN_EVENT1);
  lastEvent2 = !digitalRead(PIN_EVENT2);
  lastEvent3 = !digitalRead(PIN_EVENT3);

  Serial.println("Iniciando contagem...");
}

void loop()
{
  checkCounter();
  checkEvents();
  if (millis() - lastHeartbeat > heartbeatInterval)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      setupTime();
      checkWifi();
      sendHeartbeat();
      sendEvents();
    }
    else
    {
      Serial.println("Sem WiFi, não enviou");
    }
    lastHeartbeat = millis();
  }
}
