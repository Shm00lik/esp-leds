#pragma once

#include <Arduino.h>

// Fallback for routers that never deliver the normal (unicast) DHCP reply to a Wi-Fi client
// without an IP. The ESP's built-in DHCP client can't ask for a broadcast reply, so this does
// the DISCOVER/OFFER/REQUEST/ACK exchange itself with the broadcast flag set, then applies
// the lease as a static configuration. Call only while associated with the access point.
// Returns true once the lease is applied.
bool dhcpBroadcastLease();

// Renews a lease taken by dhcpBroadcastLease() at half its lifetime. Call from loop().
void dhcpMaintain(uint32_t now);
