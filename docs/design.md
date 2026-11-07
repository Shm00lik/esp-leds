# ESP8266 Wi-Fi LED controller — design

A **generic** LED controller. It connects to Wi-Fi and an MQTT broker, then runs built-in
effects or shows raw frames that any app publishes. It has no knowledge of its clients: the
MQTT topics below are its whole interface.

## Hardware
- **Board:** ESP8266 dev board with a CH340 USB-serial chip. If the board isn't detected,
  suspect the micro-USB cable (flaky or charge-only) before the drivers.
- **Built-in LED:** `LED_BUILTIN` = GPIO2, **active LOW** (`LOW` = on).
- **Strip:** WS2812B, **86 LEDs** by default, data on **GPIO4 (D2)**, which is safe at boot.
  The LED count is a runtime setting (1–`MAX_LEDS` = 300), saved in `/config.json`; see
  `leds/config` below. A WS2812B strip has no way to report its length, so it must be set.
- **Power: USB only, no external supply.**
  - Strip 5 V → the board's `VIN`/`VU` pin (USB 5 V), strip GND → GND.
  - Recommended: a 330 Ω resistor in series on the data line, and a ≥470 µF capacitor across
    the strip's 5 V/GND.
  - A USB port gives about 500 mA, so the firmware **caps power** with
    `FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS)` and `MAX_MILLIAMPS 400`.
    FastLED then dims frames automatically to stay under the limit and leaves room for the ESP.
  - 86 red LEDs at full brightness would draw about 1.7 A, so bright full-strip colours look
    dimmer than on an external supply. That's expected.
  - At this current the 3.3 V data signal is normally fine, so no level shifter is needed.

## Tooling
- `arduino-cli` with the `esp8266:esp8266` 3.1.2 core. FQBN: `esp8266:esp8266:generic`
- Libraries: `FastLED`, `PubSubClient` (2.8), `ArduinoJson` (7.x), `WiFiManager` by tzapu
  (2.0.x). The exact versions are in the README.

## MQTT contract

Broker: any MQTT 3.1.1 broker (e.g. Mosquitto), port 1883 by default, username/password, no
TLS. There is one device, so topics have no device ID. Clients only ever talk to the broker,
never to the controller directly. Change the contract only together with its clients.

| Topic | Publisher | Retained | QoS | Payload |
|---|---|---|---|---|
| `leds/cmd` | any app | yes | 1 | JSON command (below) |
| `leds/frame` | any app | no | 0 | raw binary, exactly `numLeds*3` bytes (258 for 86 LEDs), R,G,B per LED, LED 0 first |
| `leds/config` | any app | no | 1 | `{"numLeds":120}`: sets the LED count (1–300) and saves it on the device |
| `leds/status` | ESP (last will) | yes | 1 | `online` / `offline` |
| `leds/info` | ESP | yes | 0 | `{"numLeds":86,"maxLeds":300,"ip":"192.168.1.50","rssi":-61,"fw":"1.2.0"}` |
| `leds/log` | ESP | no | 0 | one log line as plain text, e.g. `[cmd] effect=fill color=#00FF00 brightness=255` |

`leds/cmd` JSON. Every field except `effect` is optional:
```
{"effect":"fill",     "color":"#FF0000", "brightness":200}
{"effect":"blink",    "color":"#FF0000", "periodMs":1000}           // on for half the period, off for half
{"effect":"progress", "color":"#FF4500", "from":100, "to":0, "durationMs":600000}
                      // lights the first p% of LEDs (index 0 upward); animates from→to over durationMs,
                      // timed from when the command was received; no durationMs = static at `from`
{"effect":"rainbow",  "speedMs":20}                                 // ms per hue step
{"effect":"idle"}                                                   // all LEDs off
```
- Defaults: `color` `#FFFFFF`, `brightness` = the last brightness used (initially 255),
  `periodMs` 1000, `from` 100, `to` 0, `speedMs` 20.
- `brightness` (0–255) applies to every effect and to frames. A message with only
  `{"brightness":N}` (no `effect`) changes brightness and keeps the current effect.
- A `leds/frame` message replaces the current effect until the next `leds/cmd`.
- Invalid JSON, an unknown effect or a wrong frame size is **ignored** (logged to Serial at
  115200).
- `leds/config` is not retained: the count is stored on the device, and a retained message
  would only re-apply it on every reconnect. After a change the ESP re-publishes `leds/info`,
  so clients can read `numLeds` there to size their frames. When the count shrinks, the LEDs
  past the new end are turned off. An invalid count is ignored.
- `leds/log` carries the same lines as Serial, one MQTT message per line. Lines logged while
  MQTT is down (boot, connection failures) are kept, the last 16, and sent right after
  connecting, so they arrive late and the oldest can be lost. Lines are cut at 127 chars.
  Not retained: a client sees only lines published while it is subscribed.
- A client that wants the strip to turn off when it dies can register its own last will:
  retained `{"effect":"idle"}` on `leds/cmd`. The controller needs no special handling for this.

## Files
```
led-controller/
  led-controller.ino   setup() / loop(), Wi-Fi + MQTT connection, message dispatch
  config.h             DEFAULT_NUM_LEDS 86, MAX_LEDS 300, DATA_PIN 4, MAX_MILLIAMPS 400, FW_VERSION, topic names, timing
  effects.h/.cpp       effect state + applyCommand(JsonDocument&), applyFrame(bytes,len), effectsTick(now, active)
  log.h/.cpp           Log: prints to Serial and queues each line for leds/log
  settings.h/.cpp      MQTT host, port, user, password, LED count as /config.json on LittleFS; setup-portal request flag
broker/                local Mosquitto in Docker, for testing
tools/                 serial bench-test script
README.md              wiring, power notes, setup, test cheat-sheet
```

## Behaviour
1. **Boot:**
   - `Serial.begin(115200)`.
   - Mount LittleFS (format if mounting fails) and load the settings.
   - Start FastLED (`#define FASTLED_ESP8266_RAW_PIN_ORDER` before the include;
     `FastLED.addLeds<WS2812B, DATA_PIN, GRB>`) with `setMaxPowerInVoltsAndMilliamps(5,
     MAX_MILLIAMPS)` for the saved LED count, all LEDs off.
2. **Factory reset:** if the FLASH button (GPIO0) is pressed during the first 5 s after boot
   and held LOW for 5 s, wipe the Wi-Fi credentials (`wm.resetSettings()`) and `/config.json`,
   then reboot.
3. **Wi-Fi setup with WiFiManager:**
   - Portal network `LED-Setup` with a "Configure WiFi" page and an "MQTT settings" page (host,
     port default `1883`, user default `leds`, password, LED count default `86`), and a 180 s
     timeout.
   - The portal opens when no MQTT host or no Wi-Fi is saved, when the saved Wi-Fi hasn't
     connected 2 min after boot (e.g. its password changed), or when FLASH is held for 1 s
     while running (Wi-Fi is kept). It reboots once setup is complete or it times out.
   - `WiFi.setAutoReconnect(true)`, radio sleep off.
4. **MQTT with PubSubClient:**
   - `setBufferSize(MAX_LEDS*3 + 64)`, room for a full 900-byte frame.
   - Client ID `leds-<ESP.getChipId() hex>`.
   - Last will: `leds/status` = `offline`, retained, QoS 1.
   - On connect: publish `leds/status` = `online` (retained), subscribe to `leds/cmd` (QoS 1)
     `leds/frame` (QoS 0) and `leds/config` (QoS 1), and publish `leds/info` (retained).
   - Re-publish `leds/info` every 60 s.
5. **Reconnect without blocking:** try again after 1 s → 2 → 4 … up to a 30 s maximum, and
   reset the delay after a successful connection.
6. **Not connected to MQTT** (including before the first connection): strip **off**, built-in
   LED blinking 500 ms on / 500 ms off. When connected: built-in LED off (`HIGH`), effects run.
   Each accepted `leds/cmd` or `leds/config` (MQTT or Serial) lights the built-in LED for
   100 ms, so commands can be checked without a strip. Frames don't flash.
7. **Commands:**
   - Parse with ArduinoJson (`JsonDocument`) as described in the contract.
   - `progress` and `blink` take their start time from `millis()` when the command arrives.
   - The broker delivers the retained `leds/cmd` on every (re)connect, which restores the last
     state automatically.
8. **Frames:** if `length == numLeds*3`, copy into `leds[]`, set mode = FRAME and show.
   Otherwise ignore and log.
9. **Loop:**
   - `mqtt.loop()`, then `effectsTick(millis())`.
   - Call `FastLED.show()` at most every 20 ms, when something changed or an animation is
     running, and at least once a second so a corrupted frame can't stick.
   - Never `delay()` for more than a few ms; let the loop `yield()`.

## Verify
With `mosquitto_pub` / `mosquitto_sub` (or `docker exec mosquitto …` on the local test broker):
- Each effect from the contract, e.g.
  `mosquitto_pub -u … -P … -r -t leds/cmd -m '{"effect":"blink","color":"#FF0000"}'`, shows
  correctly on the strip. `progress` with `durationMs` drains smoothly.
- A brightness-only message dims the current effect without changing it.
- `{"numLeds":40}` on `leds/config` leaves only the first 40 LEDs in use, updates `leds/info`, and
  survives a reboot. Set it back to 86 afterwards.
- A raw frame shows: `head -c 258 /dev/urandom | mosquitto_pub -u … -P … -t leds/frame -s`.
- Invalid JSON and a 100-byte frame are ignored, and the device keeps running.
- Stop the broker: the strip turns off and the built-in LED blinks. Start the broker again: the
  retained state returns.
- Power-cycle the ESP: it rejoins Wi-Fi and MQTT and shows the retained state.
- Unplug the ESP: `leds/status` becomes `offline` within about 1.5× the keepalive time.
- Factory reset brings up the `LED-Setup` network.
- Full white with `brightness` 255 doesn't brown out or reset the board on USB power.
