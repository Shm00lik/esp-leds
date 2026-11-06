#pragma once

#include <Arduino.h>
#include "config.h"

struct Settings {
  String   mqttHost;
  uint16_t mqttPort = 1883;
  String   mqttUser;
  String   mqttPass;
  uint16_t numLeds = DEFAULT_NUM_LEDS;
};

extern Settings settings;

// Mounts LittleFS (formatting it if the mount fails).
void settingsBegin();
// Loads /config.json into `settings`. Returns false if missing or invalid (defaults are kept).
bool settingsLoad();
bool settingsSave();
// Returns n if it is a valid LED count (1..MAX_LEDS), otherwise 0.
uint16_t settingsValidNumLeds(long n);
void settingsErase();

// Asks the next boot to open the setup portal (survives the reboot as a marker file).
void settingsRequestPortal();
// True if a portal was requested. The request stays until settingsClearPortalRequest(),
// so a reset or power glitch while the portal is open brings it back on the next boot.
bool settingsPortalRequested();
void settingsClearPortalRequest();
