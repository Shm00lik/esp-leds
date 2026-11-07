#include "settings.h"
#include "config.h"
#include "log.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

static const char *CONFIG_PATH = "/config.json";

Settings settings;

void settingsBegin() {
  if (!LittleFS.begin()) {
    Log.println(F("[fs] mount failed, formatting"));
    LittleFS.format();
    LittleFS.begin();
  }
  settings.mqttPort = DEFAULT_MQTT_PORT;
  settings.mqttUser = DEFAULT_MQTT_USER;
  settings.numLeds = DEFAULT_NUM_LEDS;
}

uint16_t settingsValidNumLeds(long n) {
  return (n >= 1 && n <= MAX_LEDS) ? (uint16_t)n : 0;
}

bool settingsLoad() {
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    Log.println(F("[fs] no config.json"));
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Log.printf("[fs] config.json invalid: %s\n", err.c_str());
    return false;
  }
  settings.mqttHost = doc["host"] | "";
  settings.mqttPort = doc["port"] | DEFAULT_MQTT_PORT;
  settings.mqttUser = doc["user"] | DEFAULT_MQTT_USER;
  settings.mqttPass = doc["pass"] | "";
  uint16_t n = settingsValidNumLeds(doc["numLeds"] | DEFAULT_NUM_LEDS);
  settings.numLeds = n ? n : DEFAULT_NUM_LEDS;  // a missing or bad value falls back to the default
  Log.printf("[fs] loaded mqtt %s:%u user=%s numLeds=%u\n",
             settings.mqttHost.c_str(), settings.mqttPort, settings.mqttUser.c_str(), settings.numLeds);
  return true;
}

bool settingsSave() {
  JsonDocument doc;
  doc["host"] = settings.mqttHost;
  doc["port"] = settings.mqttPort;
  doc["user"] = settings.mqttUser;
  doc["pass"] = settings.mqttPass;
  doc["numLeds"] = settings.numLeds;
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) {
    Log.println(F("[fs] cannot write config.json"));
    return false;
  }
  serializeJson(doc, f);
  f.close();
  Log.println(F("[fs] saved config.json"));
  return true;
}

void settingsErase() {
  LittleFS.remove(CONFIG_PATH);
}

static const char *PORTAL_FLAG_PATH = "/portal";

void settingsRequestPortal() {
  File f = LittleFS.open(PORTAL_FLAG_PATH, "w");
  if (f) f.close();
}

bool settingsPortalRequested() {
  return LittleFS.exists(PORTAL_FLAG_PATH);
}

void settingsClearPortalRequest() {
  LittleFS.remove(PORTAL_FLAG_PATH);
}
