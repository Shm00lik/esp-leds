#!/usr/bin/env sh
# Creates broker/config/passwd for the LOCAL TEST broker, with deliberately simple logins:
#   leds   / leds     (the ESP)
#   tester / tester   (you, for mosquitto_pub / mosquitto_sub)
# Don't reuse these on a real broker. Run once, then: docker compose up -d
set -e
cd "$(dirname "$0")"
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)/config:/mosquitto/config" eclipse-mosquitto:2 sh -c "
  rm -f /mosquitto/config/passwd &&
  mosquitto_passwd -b -c /mosquitto/config/passwd leds leds &&
  mosquitto_passwd -b /mosquitto/config/passwd tester tester &&
  chown mosquitto:mosquitto /mosquitto/config/passwd && chmod 0600 /mosquitto/config/passwd"
printf 'leds leds\ntester tester\n' > credentials.txt
echo "Users: leds/leds, tester/tester"
