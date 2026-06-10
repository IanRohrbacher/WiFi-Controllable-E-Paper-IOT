/**
 * @file wifi_lease.cpp
 * @brief Implementation of per-client lease bookkeeping.
 *
 */

#include <Arduino.h>
#include <cstring>
#include <ESP8266WiFi.h>
#include <user_interface.h>

#include "configs.h"
#include "wifi_lease.h"
#include "logger.h"

namespace {
unsigned long nowLoop = 0;

struct newLeaseIndex {
    bool success;
    int8_t index;
};

struct ClientLease {
  bool inUse;
  uint8 mac[6];
  unsigned long leaseStartMs;
  unsigned long lastSeenMs;
};

static ClientLease clientLeases[wifi_config::kMaxClientLeases] = {};
static uint8_t numberOfLeases = 0;

String stationMacToString(const uint8* mac) {
  char macText[18];
  snprintf(macText,
           sizeof(macText),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5]);
  return String(macText);
}

int8_t findLeaseIndexByMac(const uint8* mac) {
  for (uint8_t i = 0; i < wifi_config::kMaxClientLeases; i++) {
    if (clientLeases[i].inUse && std::memcmp(clientLeases[i].mac, mac, 6) == 0) {
      return i;
    }
  }

  debug_logs::leaseLogging("No lease found for MAC: %s", stationMacToString(mac).c_str());
  return -1;
}

int8_t findFreeLeaseIndex() {
  for (uint8_t i = 0; i < wifi_config::kMaxClientLeases; i++) {
    if (!clientLeases[i].inUse) {
      debug_logs::leaseLogging("Found free lease at index: %d", i);
      return i;
    }
  }

  debug_logs::leaseLogging("No free lease found.");
  return -1;
}

newLeaseIndex addLease(const uint8* mac) {
    if (numberOfLeases >= wifi_config::kMaxClientLeases) {
      debug_logs::leaseLogging("Cannot add %s: lease table full", stationMacToString(mac).c_str());
      return {false, -1};
    }

    int8_t index = findFreeLeaseIndex();
    if (index < 0) { return {false, -1}; }

    ClientLease lease = {};
    lease.inUse = true;
    memcpy(lease.mac, mac, 6);
    unsigned long now = millis();
    lease.leaseStartMs = now;
    lease.lastSeenMs = now;

    clientLeases[index] = lease;
    if (numberOfLeases < wifi_config::kMaxClientLeases) { numberOfLeases++; }

    debug_logs::leaseLogging("Client joined: %s", stationMacToString(mac).c_str());
    return {true, index};
}

bool removeLease(uint8_t index) {
    if (index >= wifi_config::kMaxClientLeases || !clientLeases[index].inUse) { return false; }

    unsigned long duration = millis() - clientLeases[index].leaseStartMs;

    debug_logs::leaseLogging("Client left: %s (connected %lu ms)", stationMacToString(clientLeases[index].mac).c_str(), duration);
    clientLeases[index] = {};
    if (numberOfLeases > 0) { numberOfLeases--; }

    return true;
}

}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in wifi_lease.h)
// -----------------------------------------------------------------------------

uint8_t const getLeaseCount() {
  return numberOfLeases;
}

bool updateLeases() {
    const unsigned long now = millis();
    bool connected[wifi_config::kMaxClientLeases] = {};

    // --- Pass 1: Walk current station list ---
    station_info* station = wifi_softap_get_station_info();

    while (station != nullptr) {
        int8_t index = findLeaseIndexByMac(station->bssid);

        if (index >= 0) {
            // Existing client.
            clientLeases[index].lastSeenMs = now;
            connected[index] = true;
        } else {
            // New client.
            newLeaseIndex result = addLease(station->bssid);
            if (result.success && result.index >= 0) { connected[result.index] = true; }
        }

        station = STAILQ_NEXT(station, next);
    }
    wifi_softap_free_station_info();

    // --- Pass 2: Remove expired leases. ---
    for (uint8_t i = 0; i < wifi_config::kMaxClientLeases; i++) {
        if (!clientLeases[i].inUse) { continue; }

        const bool maxLeaseExceeded = (now - clientLeases[i].leaseStartMs) > wifi_config::kMaxLeaseTimeMs;
        const bool stale = !connected[i] && ((now - clientLeases[i].lastSeenMs) >wifi_config::kLeaseStaleMs);

        if (maxLeaseExceeded || stale) {
            debug_logs::leaseLogging("Removing lease for: %s", maxLeaseExceeded && stale ? "maxLeaseExceeded and stale?" : maxLeaseExceeded ? "maxLeaseExceeded" : "stale");
            debug_logs::leaseLogging(
                "Removing %s (duration=%lu ms, lastSeen=%lu ms ago)",
                stationMacToString(clientLeases[i].mac).c_str(),
                now - clientLeases[i].leaseStartMs,
                now - clientLeases[i].lastSeenMs);

            removeLease(i);
        }
    }

    if (millis() - nowLoop >= debug_config::kWiFiLoopDelay) {
      debug_logs::leaseLogging("Finished updating leases.");
      debug_logs::leaseLogging("Number of active leases: %u", getLeaseCount());
      nowLoop = millis();
    }
    return true;
}
