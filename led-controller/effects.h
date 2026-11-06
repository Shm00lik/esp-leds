#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Starts FastLED for `count` LEDs with the power cap and turns every LED off.
void effectsBegin(uint16_t count);

// Changes the strip length at runtime (clamped to 1..MAX_LEDS). When shrinking, the
// LEDs past the new end are turned off first.
void effectsSetNumLeds(uint16_t count);
uint16_t effectsNumLeds();

// Applies a leds/cmd JSON command. Invalid commands are logged and ignored.
// Returns true if the command was accepted.
bool applyCommand(JsonDocument &doc);

// Applies a raw leds/frame payload (numLeds*3 bytes, RGB). Wrong sizes are ignored.
bool applyFrame(const uint8_t *data, unsigned int len);

// Renders the current effect and shows it (at most every SHOW_INTERVAL_MS, and only
// when the output changed). With active == false the strip is kept off.
void effectsTick(uint32_t now, bool active);
