# esp-leds — ESP8266 Wi-Fi LED controller

A generic WS2812B controller. It joins Wi-Fi and an MQTT broker, then runs built-in effects
(`leds/cmd`) or shows raw RGB frames (`leds/frame`) sent by any app. Apps only talk to the
broker, never to the controller directly, so the MQTT topics are its whole interface. The full
design is in [docs/design.md](docs/design.md).

## Hardware

| Part | Detail |
|---|---|
| Board | ESP8266 dev board, CH340 USB-serial (COM6 on the dev PC) |
| Strip | WS2812B, 86 LEDs by default (set 1–300 at runtime), data on **GPIO4 / D2** |
| Status LED | built-in LED, GPIO2, active LOW |
| Button | FLASH (GPIO0), used for factory reset |

### Wiring (USB power only)

```
                    ESP8266 dev board                       WS2812B strip (input end, arrow →)
                 ┌───────────────────┐
 USB 5 V ───────►│ VIN / VU  (5 V)  ●├────────┬──────────────► +5V   (red)
                 │                   │       ═╪═ 470–1000 µF
                 │                   │        │  (+ leg to 5 V)
                 │ GND              ●├────────┴──────────────► GND   (white/black)
                 │                   │
                 │ D2 (GPIO4)       ●├───[ 330 Ω ]───────────► DIN   (green)
                 └───────────────────┘
```

- Power is wired **in parallel**: strip +5V and the ESP both hang off the board's `VIN`/`VU`
  pin (the USB 5 V rail), and every GND is common. Only the **data** line goes in series
  (through the resistor).
- Connect to the strip's **input** end (`DIN`, arrows pointing away from the wires). `DOUT` at
  the far end does nothing here.
- Not `3V3`: the strip must be fed from 5 V, and the 3.3 V regulator couldn't supply it anyway.
- The 330 Ω resistor (anything 220–470 Ω) sits close to the strip's DIN and protects the first
  LED from ringing. The capacitor goes across +5V/GND at the strip, minus leg to GND.
- No level shifter: at this current the ESP's 3.3 V data signal drives the strip reliably.

### Power limits

USB gives about 500 mA. The firmware calls
`FastLED.setMaxPowerInVoltsAndMilliamps(5, 400)` (`MAX_MILLIAMPS` in `config.h`), so FastLED
dims each frame to stay under 400 mA for the strip and leaves room for the ESP. 86 LEDs at full
white would draw about 5 A (full red, about 1.7 A), so **bright full-strip colours look
noticeably dimmer than on an external supply. That is expected.** Sparse effects
(progress, rainbow) are barely affected. If you move to a 5 V supply later, feed the strip
directly from it, join the grounds, and raise `MAX_MILLIAMPS`.

## Build and flash

Tooling: `arduino-cli` 1.5.1 with the `esp8266:esp8266` 3.1.2 core.

Library versions this was built and tested with:

| Library | Version |
|---|---|
| FastLED | 3.10.3 |
| PubSubClient | 2.8 |
| ArduinoJson | 7.4.3 |
| WiFiManager (tzapu) | 2.0.17 |

```bash
arduino-cli lib install "FastLED@3.10.3" "PubSubClient@2.8" "ArduinoJson@7.4.3" "WiFiManager@2.0.17"
arduino-cli compile --fqbn esp8266:esp8266:generic led-controller
arduino-cli upload -p COM6 --fqbn esp8266:esp8266:generic led-controller
arduino-cli monitor -p COM6 -c baudrate=115200
```

If COM6 is missing, suspect the micro-USB cable before the drivers (the stock one is flaky, and
some spares are charge-only).

## First-time setup

1. Power the board. For the first 5 s the built-in LED is solid (the factory-reset window),
   then the board opens a Wi-Fi network called **`LED-Setup`**.
2. Join it from a phone. If the captive portal doesn't open, browse to `192.168.4.1`.
3. **Configure WiFi**: pick your network and enter its password. Save.
4. **MQTT settings**: fill in **MQTT host** (the broker PC's IP), **port** (1883), **user** and
   **password**. Save. The portal closes 1.5 s later and the board reboots and connects.
5. Every setup portal looks the same: first-time setup, FLASH held, or Wi-Fi join failed. It
   closes after 180 s if nothing is saved, and the board reboots and tries again.

Joining logs a scan of every network in range (the saved one marked `*`, with RSSI and channel)
and the reason code of every failed join. Some routers stop delivering the normal DHCP reply to
Wi-Fi clients: the board then associates but never gets an IP. After 15 s without a lease it
runs DHCP itself, asking the router to broadcast its reply (`[dhcp] broadcast lease …` in the
log), and renews that lease the same way at half its lifetime.

While it has no broker connection the strip is off and the built-in LED blinks once a second.
Once it connects, the built-in LED goes off and the retained `leds/cmd` is applied.

### Connection robustness

- Joining Wi-Fi at boot: the board keeps trying the saved network for 2 minutes, then opens
  the setup portal so you can enter a new password. If nothing is saved within 180 s, it
  reboots and tries the saved network again, so it recovers by itself once the router is back.
- Radio sleep is off (`WIFI_NONE_SLEEP`). It caused lost packets and broker timeouts.
- A lost Wi-Fi link counts as disconnected immediately: the strip goes off and the MQTT socket
  is closed. A Wi-Fi watchdog forces a reconnect after 20 s and reboots after 60 s.
- MQTT keepalive is 15 s, so a silent drop is noticed in about 22 s. Don't lower it on a weak
  link: at 5 s the board flapped every few seconds.
- The strip is re-sent every second even when nothing changes, so a frame corrupted by Wi-Fi
  activity can't get stuck (this once left the strip white while it was disconnected).
- Keep the Wi-Fi signal better than about −70 dBm (`rssi` in `leds/info`). At −80 the board
  drops off regularly. Keep the antenna end of the board clear of the strip, wires and metal.

**Change the MQTT settings** (e.g. the broker's IP changed): while the board is running, hold
FLASH for 1 s. The built-in LED lights solid while you hold, then the board reboots straight into
`LED-Setup`, blinking fast. Open **MQTT settings** (not "Configure WiFi"), edit the fields and
Save. The same page holds the **LED count**. Wi-Fi is kept, and the board reboots 1.5 s after
saving. If you do nothing, it closes after 180 s. The request survives resets and power dips: if
the board restarts while the portal is open, it comes back up in the portal.

**Factory reset:** let the board boot, and **within the first 5 s** (built-in LED solid on)
press and hold FLASH for 5 s (the LED flickers while held). It wipes Wi-Fi and MQTT settings and
reboots into `LED-Setup`. A press still held from before the boot doesn't count. Don't hold FLASH
while resetting or plugging in: that starts the bootloader instead.

**Built-in LED:** solid on = factory-reset window or FLASH held · fast blink = joining Wi-Fi or
setup portal open · slow blink = on Wi-Fi, no broker · off = connected · one short flash =
a command (`leds/cmd`, `leds/config` or Serial) was accepted. Frames don't flash.

## MQTT contract

| Topic | Publisher | Retained | QoS | Payload |
|---|---|---|---|---|
| `leds/cmd` | any app | yes | 1 | JSON command |
| `leds/frame` | any app | no | 0 | `numLeds*3` raw bytes (258 for 86 LEDs), R,G,B per LED, LED 0 first |
| `leds/config` | any app | no | 1 | `{"numLeds":120}`: set and save the LED count |
| `leds/status` | ESP (last will) | yes | 1 | `online` / `offline` |
| `leds/info` | ESP | yes | 0 | `{"numLeds":86,"maxLeds":300,"ip":"…","rssi":-61,"fw":"1.3.0"}`, every 60 s |
| `leds/log` | ESP | no | 0 | one log line as plain text, e.g. `[cmd] effect=fill color=#00FF00 brightness=255` |

```json
{"effect":"fill",     "color":"#FF0000", "brightness":200}
{"effect":"blink",    "color":"#FF0000", "periodMs":1000}
{"effect":"progress", "color":"#FF4500", "from":100, "to":0, "durationMs":600000}
{"effect":"rainbow",  "speedMs":20}
{"effect":"idle"}
{"brightness":64}
```

- Defaults: `color` `#FFFFFF`, `brightness` = last used (starts at 255), `periodMs` 1000,
  `from` 100, `to` 0, `speedMs` 20. No `durationMs` means `progress` is static at `from`.
- A message with only `brightness` dims the current effect or frame and keeps it running.
- A frame replaces the effect until the next `leds/cmd`.
- `leds/log` mirrors the Serial log, one line per message. Up to 16 lines from while MQTT
  was down (e.g. the boot log) are sent right after connecting.
- Frame size follows the LED count; read `numLeds` from `leds/info` (re-published right
  after a `leds/config` change).
- Invalid JSON, unknown effects, bad colours and wrong-size frames are ignored and logged to
  Serial (115200).
- PubSubClient can only *publish* at QoS 0, so the `online` message goes out at QoS 0. The
  `offline` last will is registered at QoS 1.

## Local test broker (Docker)

`broker/` runs Eclipse Mosquitto 2 with a typical setup (port 1883, username/password, no
TLS).

```bash
sh broker/setup.sh
```

```bash
docker compose -f broker/docker-compose.yml up -d
```

`setup.sh` creates two deliberately simple logins for local testing: `leds`/`leds` (for the ESP)
and `tester`/`tester` (for you). Don't reuse them on the real broker. In the setup portal, use this PC's LAN IP
as the MQTT host.

**Windows firewall:** the ESP has to reach port 1883 on this PC. On a network marked *Public*
the firewall usually blocks it. From an elevated PowerShell:

```powershell
New-NetFirewallRule -DisplayName "Mosquitto 1883" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow
```

## Bench test without Wi-Fi/MQTT

The `SERIAL_TEST_MODE` build skips Wi-Fi and MQTT entirely. The effects always run, and every
line sent over Serial (115200) is applied as a `leds/cmd` JSON command.

```bash
arduino-cli compile --fqbn esp8266:esp8266:generic --build-property "compiler.cpp.extra_flags=-DSERIAL_TEST_MODE=1" --build-path build/serial-test led-controller
```

```bash
arduino-cli upload -p COM6 --fqbn esp8266:esp8266:generic --input-dir build/serial-test led-controller
```

```bash
powershell -ExecutionPolicy Bypass -File tools/serial-test.ps1 tools/serial-test-cmds.txt
```

The script runs through fill red/green/blue, blink, dim, bright, a 10 s progress drain,
rainbow, two invalid commands, then idle. To change the LED count over USB, send a line
`{"numLeds":120}`; it is saved on the board (both builds accept it). The normal firmware also
accepts JSON lines over Serial, but only shows them while it's connected to MQTT. Flash the
normal build again afterwards.

## Test cheat-sheet

The examples use the local broker container and the `tester`/`tester` login. With another
broker, use `mosquitto_pub` directly with its
credentials instead.

```bash
docker exec mosquitto mosquitto_sub -u tester -P tester -v -t 'leds/#'
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -r -q 1 -t leds/cmd -m '{"effect":"fill","color":"#00FF00","brightness":120}'
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -r -q 1 -t leds/cmd -m '{"effect":"blink","color":"#FF0000"}'
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -r -q 1 -t leds/cmd -m '{"effect":"progress","color":"#FF4500","from":100,"to":0,"durationMs":60000}'
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -r -q 1 -t leds/cmd -m '{"effect":"rainbow","speedMs":20}'
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -r -q 1 -t leds/cmd -m '{"brightness":30}'
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -q 1 -t leds/config -m '{"numLeds":40}'
```

```bash
docker exec mosquitto sh -c "head -c 258 /dev/urandom | mosquitto_pub -u tester -P tester -t leds/frame -s"
```

```bash
docker exec mosquitto sh -c "head -c 100 /dev/urandom | mosquitto_pub -u tester -P tester -t leds/frame -s"
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -t leds/cmd -m 'not json'
```

```bash
docker exec mosquitto mosquitto_pub -u tester -P tester -r -q 1 -t leds/cmd -m '{"effect":"idle"}'
```

Broker-outage test: run `docker stop mosquitto`. The strip goes off and the built-in LED blinks.
Then `docker start mosquitto`: the ESP reconnects within 30 s (1→2→4…30 s backoff) and the
retained command comes back. Unplug the ESP and `leds/status` turns `offline` within about
22 s (1.5 × the 15 s keepalive).

## Test status (2026-10-25)

| Check | Result |
|---|---|
| Every effect, brightness-only, raw frame | ✅ over serial and MQTT |
| Invalid JSON, unknown effect, 100-byte frame ignored | ✅ |
| Broker stopped → strip off, built-in LED blinks | ✅ after the 1 s refresh fix |
| Broker restarted → retained state returns | ✅ in about 5 s |
| Power-cycle → rejoins and restores state | ✅ |
| Unplug → `leds/status` `offline` | ✅ 5 s (with the 5 s keepalive used at the time) |
| Full white at 255 on USB for 30 s | ✅ no brownout |
| Factory reset → `LED-Setup` | ✅ (2026-10-25 evening, triggered by an over-long FLASH hold) |
| FLASH 1 s → setup portal, Wi-Fi kept | ✅ portal opened, Save closed it, reconnected in 40 s |
| Router stops delivering unicast DHCP replies → broadcast DHCP fallback | ✅ 2026-11-10: lease in ~18 s, MQTT up, renewal ACKed (tested with a 60 s renew), 150/150 pings |
