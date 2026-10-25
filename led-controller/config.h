#pragma once

// Hardware
#define NUM_LEDS       86
#define DATA_PIN       4      // GPIO4 = D2, safe at boot
#define BUTTON_PIN     0      // FLASH button, LOW when pressed
#define MAX_MILLIAMPS  400    // USB power budget for the strip (leaves room for the ESP)

#define FW_VERSION     "1.0.0"

// 1 = bench test without Wi-Fi/MQTT: effects always run and leds/cmd JSON is read
// line by line from Serial. Build with:
//   --build-property "compiler.cpp.extra_flags=-DSERIAL_TEST_MODE=1"
#ifndef SERIAL_TEST_MODE
#define SERIAL_TEST_MODE 0
#endif

// MQTT topics: the controller's whole interface (contract in docs/design.md)
#define TOPIC_CMD      "leds/cmd"
#define TOPIC_FRAME    "leds/frame"
#define TOPIC_STATUS   "leds/status"
#define TOPIC_INFO     "leds/info"

#define FRAME_BYTES    (NUM_LEDS * 3)

// Timing
#define SHOW_INTERVAL_MS      20
#define REFRESH_MS            1000    // re-send an unchanged picture, in case a frame was lost
// A silent drop is noticed after ~1.5x this. Lower values make weak Wi-Fi links flap:
// one delayed ping and the broker cuts the session. Lost Wi-Fi is detected immediately anyway.
#define MQTT_KEEPALIVE_S      15
#define INFO_INTERVAL_MS      60000
#define RECONNECT_MIN_MS      1000
#define RECONNECT_MAX_MS      30000
#define PORTAL_TIMEOUT_S      180
#define RESET_WINDOW_MS       5000
#define RESET_HOLD_MS         5000
#define PORTAL_HOLD_MS        1000    // FLASH held this long while running: open setup portal
#define SETUP_BLINK_MS        150     // built-in LED blink while joining Wi-Fi / portal open
#define STATUS_BLINK_MS       500
#define WIFI_RECONNECT_MS     20000   // offline this long: force WiFi.reconnect()
#define WIFI_RESTART_MS       60000   // offline this long: reboot

// Defaults for the setup portal
#define DEFAULT_MQTT_PORT     1883
#define DEFAULT_MQTT_USER     "leds"
