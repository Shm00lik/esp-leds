// ESP8266 Wi-Fi LED controller: WS2812B strip driven over MQTT.
// See README.md for wiring, setup and the MQTT contract.

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Ticker.h>

#include "config.h"
#include "settings.h"
#include "effects.h"

static WiFiClient net;
static PubSubClient mqtt(net);
static String clientId;

static uint32_t reconnectDelay = RECONNECT_MIN_MS;
static uint32_t nextReconnect = 0;
static uint32_t lastInfo = 0;

// ---------- factory reset ----------

static void factoryReset() {
  Serial.println(F("[reset] factory reset: wiping Wi-Fi and MQTT settings"));
  WiFiManager wm;
  wm.resetSettings();
  settingsErase();
  delay(200);
  ESP.restart();
}

// FLASH (GPIO0) must be pressed within the first RESET_WINDOW_MS after boot and held
// for RESET_HOLD_MS. (Holding it while the board resets would enter the bootloader.)
// A press still held from before the reboot doesn't count: it must be released first.
static void checkFactoryReset() {
  Serial.println(F("[reset] hold FLASH for 5 s now to factory reset"));
  uint32_t pressedSince = 0;
  bool released = digitalRead(BUTTON_PIN) != LOW;
  while (millis() < RESET_WINDOW_MS || pressedSince) {
    uint32_t now = millis();
    if (digitalRead(BUTTON_PIN) != LOW) released = true;
    if (released && digitalRead(BUTTON_PIN) == LOW) {
      if (!pressedSince) pressedSince = now ? now : 1;
      if (now - pressedSince >= RESET_HOLD_MS) factoryReset();
      digitalWrite(LED_BUILTIN, (now / 100) % 2 ? LOW : HIGH);  // fast blink while held
    } else {
      pressedSince = 0;
      digitalWrite(LED_BUILTIN, LOW);  // solid on during the reset window
    }
    delay(10);
  }
  digitalWrite(LED_BUILTIN, HIGH);
}

// ---------- Wi-Fi ----------

static bool paramsSaved = false;
static Ticker setupBlinker;  // fast built-in LED blink while setupWifi() blocks

// Called when FLASH is held while running. Saves a portal request and reboots; the next
// boot opens the setup portal with the saved Wi-Fi kept, so only the MQTT settings need
// to be re-entered.
static void requestPortal() {
  Serial.println(F("[wifi] setup portal requested, rebooting"));
  Serial.flush();
  settingsRequestPortal();
  delay(100);
  ESP.restart();
}

// The one setup portal, however it was reached (first setup, FLASH request, or Wi-Fi
// join failed): "Configure WiFi" for the network, "Setup" for the MQTT fields.
// Closes and reboots once the setup is complete (MQTT saved with Wi-Fi known, or Wi-Fi
// joined with MQTT known), when exited, or after PORTAL_TIMEOUT_S.
static void runPortal(WiFiManager &wm, const __FlashStringHelper *why) {
  Serial.printf("[wifi] opening portal 'LED-Setup' (%s)\n", (const char *)why);
  // WiFiManager's own button for the parameter page just says "Setup"; label it clearly.
  std::vector<const char *> menu = {"wifi", "custom", "info", "exit"};
  wm.setMenu(menu);
  wm.setCustomMenuHTML("<form action='/param' method='get'><button>MQTT settings</button></form><br/>");
  wm.setParamsPage(true);  // MQTT fields on /param, not inside "Configure WiFi"
  wm.setConfigPortalBlocking(false);
  wm.setDisableConfigPortal(false);  // keep the portal up after a Wi-Fi join
  wm.startConfigPortal("LED-Setup");
  uint32_t start = millis(), savedAt = 0;
  while (wm.getConfigPortalActive()) {
    bool joined = wm.process();
    if (joined && !settings.mqttHost.isEmpty()) break;
    if (paramsSaved && !savedAt && WiFi.SSID().length()) savedAt = millis();
    if (savedAt && millis() - savedAt > 1500) break;  // let the "saved" page load first
    if (millis() - start > PORTAL_TIMEOUT_S * 1000UL) break;
    delay(5);
  }
  Serial.println(F("[wifi] portal closed, restarting"));
  settingsClearPortalRequest();
  Serial.flush();
  delay(100);
  ESP.restart();
}

static void setupWifi(bool portalRequested) {
  setupBlinker.attach_ms(SETUP_BLINK_MS, []() {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  });
  WiFiManager wm;
  char port[6];
  snprintf(port, sizeof(port), "%u", settings.mqttPort);

  WiFiManagerParameter pHost("mqtt_host", "MQTT host", settings.mqttHost.c_str(), 64);
  WiFiManagerParameter pPort("mqtt_port", "MQTT port", port, 5);
  WiFiManagerParameter pUser("mqtt_user", "MQTT user", settings.mqttUser.c_str(), 32);
  WiFiManagerParameter pPass("mqtt_pass", "MQTT password", settings.mqttPass.c_str(), 64);
  wm.addParameter(&pHost);
  wm.addParameter(&pPort);
  wm.addParameter(&pUser);
  wm.addParameter(&pPass);

  wm.setSaveParamsCallback([&]() {
    settings.mqttHost = pHost.getValue();
    settings.mqttHost.trim();
    int p = atoi(pPort.getValue());
    settings.mqttPort = (p > 0 && p < 65536) ? p : DEFAULT_MQTT_PORT;
    settings.mqttUser = pUser.getValue();
    settings.mqttPass = pPass.getValue();
    settingsSave();
    paramsSaved = true;
  });
  // Weak links can take well over WiFiManager's default 10 s to associate.
  wm.setConnectTimeout(30);
  wm.setConnectRetries(3);

  // runPortal() never returns.
  if (portalRequested) runPortal(wm, F("FLASH held"));
  if (settings.mqttHost.isEmpty()) runPortal(wm, F("no MQTT host set"));
  wm.setEnableConfigPortal(false);  // on failure, use our portal instead of the built-in one
  if (!wm.autoConnect("LED-Setup")) runPortal(wm, F("Wi-Fi join failed"));
  WiFi.setAutoReconnect(true);
  // Modem sleep adds latency and drops packets on weak links, which makes the broker
  // time out our keepalive. The strip already draws far more than the radio saves.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  setupBlinker.detach();
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.printf("[wifi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
}

// FLASH held for PORTAL_HOLD_MS while running opens the setup portal.
// Returns true while the button is held (the built-in LED then stays on as feedback).
static bool buttonMaintain(uint32_t now) {
  static uint32_t pressedSince = 0;
  if (digitalRead(BUTTON_PIN) != LOW) {
    pressedSince = 0;
    return false;
  }
  if (!pressedSince) {
    pressedSince = now ? now : 1;
    Serial.println(F("[button] FLASH pressed"));
  }
  if (now - pressedSince >= PORTAL_HOLD_MS) requestPortal();
  return true;
}

// Auto-reconnect alone sometimes never recovers on a weak link, so escalate:
// force a reconnect, then reboot (the retained leds/cmd restores the state).
static void wifiWatchdog(uint32_t now) {
  static bool wasUp = true;
  static uint32_t downSince = 0;
  static bool kicked = false;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up != wasUp) {
    wasUp = up;
    if (up) {
      Serial.printf("[wifi] back, ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.printf("[wifi] lost (status %d)\n", WiFi.status());
      downSince = now;
      kicked = false;
    }
  }
  if (up) return;
  if (!kicked && now - downSince >= WIFI_RECONNECT_MS) {
    Serial.println(F("[wifi] still down, forcing reconnect"));
    WiFi.reconnect();
    kicked = true;
  }
  if (now - downSince >= WIFI_RESTART_MS) {
    Serial.println(F("[wifi] still down, restarting"));
    ESP.restart();
  }
}

// ---------- MQTT ----------

static void publishInfo() {
  JsonDocument doc;
  doc["numLeds"] = NUM_LEDS;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["fw"] = FW_VERSION;
  char buf[128];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(TOPIC_INFO, (const uint8_t *)buf, n, true);
  lastInfo = millis();
}

static void onMessage(char *topic, byte *payload, unsigned int length) {
  if (!strcmp(topic, TOPIC_FRAME)) {
    applyFrame(payload, length);
  } else if (!strcmp(topic, TOPIC_CMD)) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
      Serial.printf("[cmd] ignored: invalid JSON (%s)\n", err.c_str());
      return;
    }
    applyCommand(doc);
  }
}

static bool mqttConnect() {
  Serial.printf("[mqtt] connecting to %s:%u as %s ... ",
                settings.mqttHost.c_str(), settings.mqttPort, clientId.c_str());
  const char *user = settings.mqttUser.isEmpty() ? nullptr : settings.mqttUser.c_str();
  const char *pass = settings.mqttPass.isEmpty() ? nullptr : settings.mqttPass.c_str();
  if (!mqtt.connect(clientId.c_str(), user, pass, TOPIC_STATUS, 1, true, "offline")) {
    Serial.printf("failed (state %d)\n", mqtt.state());
    return false;
  }
  Serial.println(F("ok"));
  mqtt.publish(TOPIC_STATUS, "online", true);
  mqtt.subscribe(TOPIC_CMD, 1);
  mqtt.subscribe(TOPIC_FRAME, 0);
  publishInfo();
  return true;
}

// Non-blocking reconnect with exponential backoff (1 s, 2 s, 4 s ... 30 s).
static void mqttMaintain(uint32_t now) {
  static bool wasConnected = false;
  if (WiFi.status() != WL_CONNECTED && mqtt.connected()) {
    // Don't wait for the keepalive to notice: without Wi-Fi the session is gone.
    // Closing the socket (not a clean DISCONNECT) keeps the broker's last will in force.
    net.stop();
  }
  if (mqtt.connected()) {
    wasConnected = true;
    mqtt.loop();
    if (now - lastInfo >= INFO_INTERVAL_MS) publishInfo();
    return;
  }
  if (wasConnected) {
    wasConnected = false;
    Serial.printf("[mqtt] connection lost (state %d, wifi %d, rssi %d)\n",
                  mqtt.state(), WiFi.status(), WiFi.RSSI());
    // Wait before the first retry: a connect attempt blocks for up to ~2 s, and the
    // strip must go dark first (effectsTick runs right after this).
    nextReconnect = now + RECONNECT_MIN_MS;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;  // auto-reconnect is handling Wi-Fi
  if ((int32_t)(now - nextReconnect) < 0) return;

  if (mqttConnect()) {
    reconnectDelay = RECONNECT_MIN_MS;
  } else {
    nextReconnect = millis() + reconnectDelay;
    reconnectDelay = min<uint32_t>(reconnectDelay * 2, RECONNECT_MAX_MS);
  }
}

// ---------- serial commands ----------

// Reads leds/cmd JSON from Serial, one command per line. Handy for bench tests.
static void serialMaintain() {
  static char line[256];
  static size_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (len < sizeof(line) - 1) line[len++] = c;
      continue;
    }
    line[len] = '\0';
    if (len > 0) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, line, len);
      if (err) {
        Serial.printf("[serial] ignored: invalid JSON (%s)\n", err.c_str());
      } else {
        applyCommand(doc);
      }
    }
    len = 0;
  }
}

// ---------- main ----------

#if SERIAL_TEST_MODE

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.printf("[boot] led-controller %s SERIAL TEST MODE (no Wi-Fi/MQTT)\n", FW_VERSION);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  effectsBegin();
  Serial.println(F("[serial] send one leds/cmd JSON per line"));
}

void loop() {
  serialMaintain();
  effectsTick(millis(), true);
  yield();
}

#else

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.printf("[boot] led-controller %s, reset reason: %s\n", FW_VERSION,
                ESP.getResetReason().c_str());

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  effectsBegin();

  settingsBegin();
  settingsLoad();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // After a setup request go straight to the portal: the user may still be holding FLASH,
  // and the reset window would look like nothing happened.
  bool portalRequested = settingsPortalRequested();
  if (!portalRequested) checkFactoryReset();
  setupWifi(portalRequested);

  clientId = "leds-" + String(ESP.getChipId(), HEX);
  net.setTimeout(2000);  // bounds how long a connect attempt can block
  mqtt.setServer(settings.mqttHost.c_str(), settings.mqttPort);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(2);
  mqtt.setCallback(onMessage);
}

void loop() {
  uint32_t now = millis();
  wifiWatchdog(now);
  bool held = buttonMaintain(now);
  mqttMaintain(now);

  bool connected = mqtt.connected();
  if (held) {
    digitalWrite(LED_BUILTIN, LOW);  // solid on: FLASH press registered
  } else if (connected) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, (now / STATUS_BLINK_MS) % 2 ? HIGH : LOW);
  }
  serialMaintain();
  effectsTick(millis(), connected);
  yield();
}

#endif
