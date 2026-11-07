// FastLED goes first: its templates use a parameter named DATA_PIN, which the macro
// in config.h would otherwise clobber.
#define FASTLED_ESP8266_RAW_PIN_ORDER
#define FASTLED_ALLOW_INTERRUPTS 0   // avoids flicker caused by Wi-Fi interrupts on the ESP8266
#include <FastLED.h>

#include "effects.h"
#include "config.h"
#include "log.h"

enum Mode { MODE_IDLE, MODE_FILL, MODE_BLINK, MODE_PROGRESS, MODE_RAINBOW, MODE_FRAME };

// Buffers are sized for MAX_LEDS; only the first numLeds are drawn and sent.
static CRGB leds[MAX_LEDS];    // what FastLED sends to the strip
static CRGB frame[MAX_LEDS];   // last raw frame received
static CRGB shown[MAX_LEDS];   // what was last shown, to skip redundant show() calls
static uint16_t numLeds = DEFAULT_NUM_LEDS;
static CLEDController *strip = nullptr;
static uint8_t shownBrightness = 0;
static bool forceShow = true;
static uint32_t lastTick = 0;

static Mode mode = MODE_IDLE;
static CRGB color = CRGB::White;
static uint8_t brightness = 255;
static uint32_t startMs = 0;
static uint32_t periodMs = 1000;
static float progFrom = 100, progTo = 0;
static uint32_t durationMs = 0;
static uint32_t speedMs = 20;

void effectsBegin(uint16_t count) {
  numLeds = constrain(count, 1, MAX_LEDS);
  strip = &FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, numLeds);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  FastLED.setBrightness(brightness);
  FastLED.clear(true);
}

void effectsSetNumLeds(uint16_t count) {
  count = constrain(count, 1, MAX_LEDS);
  if (count == numLeds) return;
  if (count < numLeds) {
    // LEDs past the new end would otherwise keep their last colour forever.
    fill_solid(leds, numLeds, CRGB::Black);
    FastLED.show();
  }
  numLeds = count;
  strip->setLeds(leds, numLeds);
  forceShow = true;
  Log.printf("[leds] numLeds=%u\n", numLeds);
}

uint16_t effectsNumLeds() {
  return numLeds;
}

// Accepts "#RRGGBB" or "RRGGBB".
static bool parseColor(const char *s, CRGB &out) {
  if (!s) return false;
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (!isxdigit((unsigned char)s[i])) return false;
  }
  uint32_t v = strtoul(s, nullptr, 16);
  out = CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
  return true;
}

bool applyCommand(JsonDocument &doc) {
  if (!doc.is<JsonObject>()) {
    Log.println(F("[cmd] ignored: not a JSON object"));
    return false;
  }

  JsonVariant b = doc["brightness"];
  if (!b.isNull() && !b.is<int>()) {
    Log.println(F("[cmd] ignored: brightness must be a number"));
    return false;
  }
  uint8_t newBrightness = b.isNull() ? brightness : (uint8_t)constrain(b.as<int>(), 0, 255);

  const char *effect = doc["effect"];
  if (!effect) {
    if (b.isNull()) {
      Log.println(F("[cmd] ignored: no effect and no brightness"));
      return false;
    }
    brightness = newBrightness;
    Log.printf("[cmd] brightness=%u\n", brightness);
    return true;
  }

  CRGB newColor = CRGB::White;
  if (!doc["color"].isNull() && !parseColor(doc["color"], newColor)) {
    Log.println(F("[cmd] ignored: bad color"));
    return false;
  }

  Mode newMode;
  if      (!strcmp(effect, "fill"))     newMode = MODE_FILL;
  else if (!strcmp(effect, "blink"))    newMode = MODE_BLINK;
  else if (!strcmp(effect, "progress")) newMode = MODE_PROGRESS;
  else if (!strcmp(effect, "rainbow"))  newMode = MODE_RAINBOW;
  else if (!strcmp(effect, "idle"))     newMode = MODE_IDLE;
  else {
    Log.printf("[cmd] ignored: unknown effect '%s'\n", effect);
    return false;
  }

  mode = newMode;
  color = newColor;
  brightness = newBrightness;
  startMs = millis();
  periodMs = max<uint32_t>(doc["periodMs"] | 1000u, 2);
  progFrom = constrain(doc["from"] | 100.0f, 0.0f, 100.0f);
  progTo = constrain(doc["to"] | 0.0f, 0.0f, 100.0f);
  durationMs = doc["durationMs"] | 0u;
  speedMs = max<uint32_t>(doc["speedMs"] | 20u, 1);

  Log.printf("[cmd] effect=%s color=#%02X%02X%02X brightness=%u\n",
             effect, color.r, color.g, color.b, brightness);
  return true;
}

bool applyFrame(const uint8_t *data, unsigned int len) {
  unsigned int expected = numLeds * 3;
  if (len != expected) {
    Log.printf("[frame] ignored: %u bytes, expected %u\n", len, expected);
    return false;
  }
  memcpy(frame, data, expected);  // CRGB is laid out as r, g, b
  mode = MODE_FRAME;
  forceShow = true;
  return true;
}

static void renderProgress(uint32_t now) {
  float p = progFrom;
  if (durationMs > 0) {
    float t = (float)(now - startMs) / durationMs;
    if (t > 1) t = 1;
    p = progFrom + (progTo - progFrom) * t;
  }
  // Fractional LED count so a slow drain fades the last LED instead of jumping.
  float lit = numLeds * p / 100.0f;
  int full = (int)lit;
  fill_solid(leds, numLeds, CRGB::Black);
  fill_solid(leds, full, color);
  if (full < numLeds) {
    leds[full] = color;
    leds[full].nscale8_video((uint8_t)((lit - full) * 255));
  }
}

static void render(uint32_t now) {
  switch (mode) {
    case MODE_IDLE:
      fill_solid(leds, numLeds, CRGB::Black);
      break;
    case MODE_FILL:
      fill_solid(leds, numLeds, color);
      break;
    case MODE_BLINK: {
      bool on = ((now - startMs) % periodMs) < periodMs / 2;
      fill_solid(leds, numLeds, on ? color : CRGB::Black);
      break;
    }
    case MODE_PROGRESS:
      renderProgress(now);
      break;
    case MODE_RAINBOW:
      fill_rainbow(leds, numLeds, (uint8_t)((now - startMs) / speedMs), max(1, 256 / numLeds));
      break;
    case MODE_FRAME:
      memcpy(leds, frame, numLeds * sizeof(CRGB));
      break;
  }
}

void effectsTick(uint32_t now, bool active) {
  if (now - lastTick < SHOW_INTERVAL_MS) return;
  lastTick = now;

  static bool wasActive = true;
  bool changedActive = active != wasActive;
  wasActive = active;

  if (active) {
    render(now);
  } else {
    fill_solid(leds, numLeds, CRGB::Black);
  }

  static uint32_t lastShow = 0;
  bool same = !forceShow && brightness == shownBrightness && memcmp(leds, shown, numLeds * sizeof(CRGB)) == 0;
  // A single frame can get corrupted (e.g. by Wi-Fi activity); a static picture would
  // then stay wrong forever, so re-send it now and then even when nothing changed.
  if (same && now - lastShow >= REFRESH_MS) same = false;
  if (changedActive) {
    Log.println(active ? F("[leds] output running") : F("[leds] output off (not connected)"));
  }
  if (same) return;
  FastLED.setBrightness(brightness);
  FastLED.show();
  lastShow = now;
  memcpy(shown, leds, numLeds * sizeof(CRGB));
  shownBrightness = brightness;
  forceShow = false;
}
