#include "settings.h"
#include "config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

static const char *CONFIG_PATH = "/config.json";

Settings settings;

void settingsBegin() {
  if (!LittleFS.begin()) {
    Serial.println(F("[fs] mount failed, formatting"));
    LittleFS.format();
    LittleFS.begin();
  }
  settings.mqttPort = DEFAULT_MQTT_PORT;
  settings.mqttUser = DEFAULT_MQTT_USER;
}

bool settingsLoad() {
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    Serial.println(F("[fs] no config.json"));
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[fs] config.json invalid: %s\n", err.c_str());
    return false;
  }
  settings.mqttHost = doc["host"] | "";
  settings.mqttPort = doc["port"] | DEFAULT_MQTT_PORT;
  settings.mqttUser = doc["user"] | DEFAULT_MQTT_USER;
  settings.mqttPass = doc["pass"] | "";
  Serial.printf("[fs] loaded mqtt %s:%u user=%s\n",
                settings.mqttHost.c_str(), settings.mqttPort, settings.mqttUser.c_str());
  return true;
}

bool settingsSave() {
  JsonDocument doc;
  doc["host"] = settings.mqttHost;
  doc["port"] = settings.mqttPort;
  doc["user"] = settings.mqttUser;
  doc["pass"] = settings.mqttPass;
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) {
    Serial.println(F("[fs] cannot write config.json"));
    return false;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println(F("[fs] saved config.json"));
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
