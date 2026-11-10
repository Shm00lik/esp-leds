#include "dhcp.h"
#include "log.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

static bool active = false;       // a broadcast lease is in use
static uint32_t renewAt = 0;      // millis() of the next renewal
static uint32_t xidBase = 0;

enum { DISCOVER = 1, OFFER = 2, REQUEST = 3, ACK = 5, NAK = 6 };

struct Reply {
  uint8_t type = 0;
  IPAddress ip, mask, router, dns, server;
  uint32_t leaseS = 0;
};

static void send(WiFiUDP &udp, uint8_t type, uint32_t xid, IPAddress reqIp, IPAddress server) {
  uint8_t p[300] = {0};
  p[0] = 1;  // BOOTREQUEST
  p[1] = 1;  // Ethernet
  p[2] = 6;  // MAC length
  memcpy(p + 4, &xid, 4);
  p[10] = 0x80;  // broadcast flag: the whole point of this file
  WiFi.macAddress(p + 28);
  uint8_t *o = p + 236;
  const uint8_t head[] = {0x63, 0x82, 0x53, 0x63, 53, 1, type, 55, 4, 1, 3, 6, 51};
  memcpy(o, head, sizeof(head));
  o += sizeof(head);
  if (type == REQUEST) {
    *o++ = 50; *o++ = 4;
    for (int i = 0; i < 4; i++) *o++ = reqIp[i];
    *o++ = 54; *o++ = 4;
    for (int i = 0; i < 4; i++) *o++ = server[i];
  }
  *o = 255;
  udp.beginPacket(IPAddress(255, 255, 255, 255), 67);
  udp.write(p, sizeof(p));
  udp.endPacket();
}

static bool receive(WiFiUDP &udp, uint32_t xid, Reply &r, uint32_t timeoutMs) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (udp.parsePacket() < 240) {
      delay(10);
      continue;
    }
    uint8_t p[576];
    int n = udp.read(p, sizeof(p));
    if (n < 240 || p[0] != 2 || memcmp(p + 4, &xid, 4) || memcmp(p + 28, mac, 6)) continue;
    r = Reply();
    r.ip = IPAddress(p[16], p[17], p[18], p[19]);
    for (int i = 240; i + 1 < n && p[i] != 255;) {
      if (p[i] == 0) { i++; continue; }
      uint8_t code = p[i], len = p[i + 1];
      const uint8_t *v = p + i + 2;
      if (i + 2 + len > n) break;
      if (code == 53 && len == 1) r.type = v[0];
      if (len >= 4) {
        IPAddress a(v[0], v[1], v[2], v[3]);
        if (code == 1) r.mask = a;
        if (code == 3) r.router = a;
        if (code == 6) r.dns = a;
        if (code == 54) r.server = a;
        if (code == 51) r.leaseS = (uint32_t)v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3];
      }
      i += 2 + len;
    }
    return true;
  }
  return false;
}

// One DISCOVER -> OFFER -> REQUEST -> ACK exchange. Needs an address on the interface so
// WiFiUDP can send and receive broadcasts; the server goes by the MAC, not the source IP.
static bool exchange(Reply &ack) {
  WiFiUDP udp;
  if (!udp.begin(68)) return false;
  bool ok = false;
  for (int attempt = 0; attempt < 3 && !ok; attempt++) {
    uint32_t xid = xidBase + attempt;
    Reply offer;
    send(udp, DISCOVER, xid, IPAddress(), IPAddress());
    if (!receive(udp, xid, offer, 4000) || offer.type != OFFER) continue;
    send(udp, REQUEST, xid, offer.ip, offer.server);
    ok = receive(udp, xid, ack, 4000) && ack.type == ACK;
  }
  udp.stop();
  xidBase += 16;
  return ok;
}

static bool apply(const Reply &ack) {
  IPAddress dns = ack.dns.isSet() ? ack.dns : ack.router;
  if (WiFi.localIP() != ack.ip && !WiFi.config(ack.ip, ack.router, ack.mask, dns)) return false;
  // Renew at half the lease, capped so the millis() arithmetic can't overflow.
  uint32_t renewS = min<uint32_t>(ack.leaseS ? ack.leaseS / 2 : 3600, 12UL * 3600);
  renewAt = millis() + renewS * 1000UL;
  active = true;
  Log.printf("[dhcp] broadcast lease %s gw %s, %lu s, renew in %lu s\n",
             ack.ip.toString().c_str(), ack.router.toString().c_str(),
             (unsigned long)ack.leaseS, (unsigned long)renewS);
  return true;
}

bool dhcpBroadcastLease() {
  if (!xidBase) xidBase = ESP.random() & 0xFFFFFF00;
  // A link-local placeholder (from the MAC) gives WiFiUDP an interface address to use.
  // The interface only comes up when the SDK sees "got IP" at association, so set it
  // and re-associate. (reconnect(), unlike disconnect(), keeps the saved credentials.)
  uint8_t mac[6];
  WiFi.macAddress(mac);
  IPAddress tmp(169, 254, mac[4], mac[5] ? mac[5] : 1);
  WiFi.config(tmp, IPAddress(169, 254, 0, 1), IPAddress(255, 255, 0, 0));
  WiFi.reconnect();
  for (uint32_t t = millis(); WiFi.status() != WL_CONNECTED && millis() - t < 10000;) delay(50);
  Reply ack;
  if (WiFi.status() == WL_CONNECTED && exchange(ack) && apply(ack)) return true;
  Log.println(F("[dhcp] broadcast DHCP got no lease"));
  WiFi.config(IPAddress(), IPAddress(), IPAddress());  // back to the built-in DHCP client
  return false;
}

void dhcpMaintain(uint32_t now) {
  if (!active || WiFi.status() != WL_CONNECTED || (int32_t)(now - renewAt) < 0) return;
  Reply ack;
  if (exchange(ack) && apply(ack)) return;
  Log.println(F("[dhcp] renewal failed, retrying in 60 s"));
  renewAt = now + 60000;
}
