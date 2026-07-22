/**
 * @file wifi_lease.cpp
 *
 * @brief Implementation of per-client lease bookkeeping.
 *
 * @details
 * This file contains the implementation of the WiFi lease management system
 * for the ESP8266 SoftAP. It defines the data structures and functions
 * necessary to track connected clients, their MAC addresses, and the duration
 * of their leases. The module handles adding new leases when clients connect,
 * updating existing leases, and removing leases when clients disconnect or
 * when leases expire. It also manages a blocked-entries table to enforce
 * reconnect cooldowns for clients whose sessions have expired.
 *
 */

#include <Arduino.h>
#include <cstring>
#include <ESP8266WiFi.h>
#include <user_interface.h>

#include "configs.h"
#include "wifi_lease.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the wifi lease module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the lease loop */
unsigned long nowLoop = 0;

/** @brief Structure to hold the result of a new lease addition attempt */
struct newLeaseIndex {
    bool success;
    int8_t index;
};

/** @brief Structure to hold information about a client's lease */
struct ClientLease {
  bool inUse;
  uint8 mac[6];
  unsigned long leaseStartMs;
  unsigned long lastSeenMs;
  bool blocked;
};
/** @brief Array to store all active client leases */
static ClientLease clientLeases[wifi_config::kMaxClientLeases] = {};
/** @brief Counter for the number of active leases */
static uint8_t numberOfLeases = 0;

/** @brief Structure to hold a reconnect block for a MAC no longer connected. */
struct BlockedEntry {
  bool inUse;
  uint8 mac[6];
  unsigned long remainingMs;
  unsigned long lastTickMs;
};
/** @brief Holding space for MACs serving a post-block reconnect wait. */
static BlockedEntry blockedEntries[wifi_config::kMaxBlockedEntries] = {};
/** @brief Counter for the number of blocked leases */
static uint8_t numberOfBlocked = 0;

/**
 * @brief Convert a MAC address to a human-readable string format.
 *
 * @details
 * This function takes a 6-byte MAC address and formats it as a string in the
 * format "XX:XX:XX:XX:XX:XX". This is useful for logging and debugging
 * purposes to easily identify clients by their MAC addresses.
 *
 * @param mac Pointer to the 6-byte MAC address.
 *
 * @return A String representation of the MAC address in the format "XX:XX:XX:XX:XX:XX".
 *
 */
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

/**
 * @brief Find the index of a lease by matching the client's MAC address.
 *
 * @details
 * This function iterates through the list of active client leases and compares
 * the MAC address of each lease with the provided MAC address. If a matching
 * lease is found, the index of that lease is returned. If no matching lease is
 * found, the function returns -1 to indicate that the client does not
 * currently have an active lease. This is used to determine if a connecting
 * client is new or returning, and to update lease information accordingly.
 *
 * @param mac Pointer to the 6-byte MAC address.
 *
 * @return The index of the lease if found
 * @retval -1 No lease found for the given MAC address.
 *
 */
int8_t findLeaseIndexByMac(const uint8* mac) {
  for (uint8_t i = 0; i < wifi_config::kMaxClientLeases; i++) {
    if (clientLeases[i].inUse && std::memcmp(clientLeases[i].mac, mac, 6) == 0) {
      return i;
    }
  }

  debug_logs::leaseLogging("No lease found for MAC: %s", stationMacToString(mac).c_str());
  return -1;
}

/**
 * @brief Find the index of a free lease slot in the clientLeases array.
 * 
 * @details
 * This function iterates through the clientLeases array to find the first slot
 * that is not currently in use (i.e., where inUse is false). If a free slot is
 * found, its index is returned. If no free slot is available, the function
 * returns -1 to indicate that the lease table is full and a new lease cannot
 * be added. This is used when adding new leases for clients that connect to
 * the access point.
 *  
 * @par Parameters
 * None.
 *
 * @return The index of the first free lease slot
 * @retval -1 No free lease slot available.
 *
 */
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

/**
 * @brief Find the index of a blocked entry by matching the client's MAC address.
 *
 * @param mac Pointer to the 6-byte MAC address.
 *
 * @return The index of the blocked entry if found
 * @retval -1 No blocked entry found for the given MAC address.
 *
 */
int8_t findBlockedIndexByMac(const uint8* mac) {
  for (uint8_t i = 0; i < wifi_config::kMaxBlockedEntries; i++) {
    if (blockedEntries[i].inUse && std::memcmp(blockedEntries[i].mac, mac, 6) == 0) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Find the index of a free blocked-entry slot in the blockedEntries array.
 *
 * @return The index of the first free blocked-entry slot
 * @retval -1 No free blocked-entry slot available.
 *
 */
int8_t findFreeBlockedIndex() {
  for (uint8_t i = 0; i < wifi_config::kMaxBlockedEntries; i++) {
    if (!blockedEntries[i].inUse) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Find the in-use blocked entry with the least remaining time, i.e.
 * the one that would clear on its own soonest anyway.
 *
 * @details
 * Used to make room in a full blockedEntries table: evicting this entry is
 * the least disruptive choice, since it was closest to expiring naturally.
 *
 * @return The index of the soonest-to-clear blocked entry.
 * @retval -1 No blocked entries are currently in use.
 *
 */
int8_t findSoonestToClearBlockedIndex() {
  int8_t soonest = -1;
  for (uint8_t i = 0; i < wifi_config::kMaxBlockedEntries; i++) {
    if (!blockedEntries[i].inUse) { continue; }
    if (soonest < 0 || blockedEntries[i].remainingMs < blockedEntries[soonest].remainingMs) {
      soonest = i;
    }
  }
  return soonest;
}

/**
 * @brief Start (or restart) a reconnect block for a MAC that just left after having its session expire.
 *
 * @details
 * This function adds a new blocked entry for a client that has disconnected
 * after its session has expired. It first checks if the blocked duration is
 * enabled (non-zero). If the MAC address already has a blocked entry, it
 * updates the existing entry's remaining time and last tick time. If the MAC
 * address does not have an existing entry, it attempts to find a free slot in
 * the blockedEntries array. If no free slot is available, it evicts the entry
 * that is closest to expiring naturally to make room for the new blocked
 * entry. The function logs the action taken and updates the number of blocked
 * entries accordingly.
 *
 * @param mac Pointer to the 6-byte MAC address.
 * @param now The current time in milliseconds.
 */
void addBlockedEntry(const uint8* mac, unsigned long now) {
  if (wifi_config::kBlockedDurationMs == 0) { return; }

  const int8_t existingIndex = findBlockedIndexByMac(mac);
  int8_t index = existingIndex;
  bool isNewSlot = false;

  if (index < 0) {
    index = findFreeBlockedIndex();
    if (index >= 0) {
      isNewSlot = true;
    } else {
      const int8_t evictIndex = findSoonestToClearBlockedIndex();
      if (evictIndex < 0) {
        debug_logs::leaseLogging("Cannot block %s: blocked-entries table full", stationMacToString(mac).c_str());
        return;
      }
      debug_logs::leaseLogging(
        "Blocked-entries table full; evicting %s (%lu ms left) to block %s",
        stationMacToString(blockedEntries[evictIndex].mac).c_str(),
        blockedEntries[evictIndex].remainingMs,
        stationMacToString(mac).c_str());
      index = evictIndex;
    }
  }

  blockedEntries[index].inUse = true;
  memcpy(blockedEntries[index].mac, mac, 6);
  blockedEntries[index].remainingMs = wifi_config::kBlockedDurationMs;
  blockedEntries[index].lastTickMs = now;
  if (isNewSlot && numberOfBlocked < wifi_config::kMaxBlockedEntries) { numberOfBlocked++; }

  debug_logs::leaseLogging("Blocking %s for %lu ms after disconnect", stationMacToString(mac).c_str(), wifi_config::kBlockedDurationMs);
}

/**
 * @brief Add a new lease for a client with the given MAC address.
 * 
 * @details
 * This function attempts to add a new lease for a client that has connected to
 * the access point. It first checks if the lease table is full by comparing
 * the current number of leases with the maximum allowed. If the table is full,
 * it logs a message and returns a failure result. If there is space for a new
 * lease, it finds a free index in the clientLeases array, initializes a new
 * ClientLease structure with the client's MAC address and the current time for
 * lease start and last seen, and marks it as in use. The function then
 * increments the number of active leases if necessary and returns a success
 * result with the index of the new lease.
 *
 * @param mac Pointer to the 6-byte MAC address of the client for which to add a lease.
 * @param now The current time in milliseconds, used to set the lease start time and last seen time for the new lease.
 *
 * @return A newLeaseIndex structure containing the success status and index of the new lease
 * @retval {success = true, index >= 0} The lease was successfully added at the specified index.
 * @retval {success = false, index = -1} The lease could not be added, either because the lease table is full or due to an error.
 *
 */
newLeaseIndex addLease(const uint8* mac, unsigned long now) {
    if (numberOfLeases >= wifi_config::kMaxClientLeases) {
      debug_logs::leaseLogging("Cannot add %s: lease table full", stationMacToString(mac).c_str());
      return {false, -1};
    }

    int8_t index = findFreeLeaseIndex();
    if (index < 0) { return {false, -1}; }

    ClientLease lease = {};
    lease.inUse = true;
    memcpy(lease.mac, mac, 6);
    lease.leaseStartMs = now;
    lease.lastSeenMs = now;
    lease.blocked = false;

    clientLeases[index] = lease;
    if (numberOfLeases < wifi_config::kMaxClientLeases) { numberOfLeases++; }

    debug_logs::leaseLogging("Client joined: %s", stationMacToString(mac).c_str());
    return {true, index};
}

/**
 * @brief Remove a lease at the specified index.
 * 
 * @details
 * This function removes a client lease from the clientLeases array at the
 * given index. It first checks if the index is valid and if the lease at that
 * index is currently in use. If the index is out of bounds or the lease is
 * not in use, it returns false to indicate that the removal was unsuccessful.
 * If the lease is valid, it clears the lease information at that index, marks
 * it as not in use, and decrements the number of active leases if necessary.
 *
 * @param index The index of the lease to remove from the clientLeases array.
 *
 * @return The status of the lease removal attempt.
 * @retval true The lease was successfully removed.
 * @retval false The lease could not be removed, either because the index is out of bounds or because the lease at that index was not in use.
 *
 */
bool removeLease(uint8_t index) {
    if (index >= wifi_config::kMaxClientLeases || !clientLeases[index].inUse) { return false; }

    unsigned long duration = millis() - clientLeases[index].leaseStartMs;

    debug_logs::leaseLogging("Client left: %s (connected %lu ms)", stationMacToString(clientLeases[index].mac).c_str(), duration);
    clientLeases[index] = {};
    if (numberOfLeases > 0) { numberOfLeases--; }

    return true;
}

/**
 * @brief Resolve a currently-connected station's IP address to its MAC address.
 *
 * @details
 * This function iterates through the list of currently connected stations and
 * attempts to find a station with the specified IP address. If found, it
 * copies the station's MAC address to the provided buffer.
 *
 * @param ip IP address to resolve.
 * @param macOut Pointer to a 6-byte buffer to receive the MAC address.
 *
 * @retval true A currently-connected station with this IP was found.
 * @retval false No currently-connected station has this IP.
 */
bool findMacForIp(const IPAddress& ip, uint8_t* macOut) {
  station_info* station = wifi_softap_get_station_info();
  bool found = false;

  while (station != nullptr) {
    if (IPAddress(station->ip.addr) == ip) {
      memcpy(macOut, station->bssid, 6);
      found = true;
      break;
    }
    station = STAILQ_NEXT(station, next);
  }
  wifi_softap_free_station_info();

  return found;
}

}  // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the wifi lease module, declared in wifi_lease.h.
 * @{
 */
uint8_t const getLeaseCount() {
  return numberOfLeases;
}

uint8_t const getBlockedCount() {
  return numberOfBlocked;
}

LeaseStatus getLeaseStatus(const IPAddress& ip) {
  uint8_t mac[6];
  if (!findMacForIp(ip, mac)) {
    return {LeaseState::Unknown, 0};
  }

  const int8_t leaseIndex = findLeaseIndexByMac(mac);
  if (leaseIndex >= 0) {
    if (clientLeases[leaseIndex].blocked) {
      // Expired but still connected: the block hasn't started counting down
      // yet, so the full duration is what they'll face once they disconnect.
      return {LeaseState::Blocked, wifi_config::kBlockedDurationMs};
    }
    if (wifi_config::kSessionDurationMs == 0) {
      return {LeaseState::Unlimited, 0};
    }
    const unsigned long elapsed = millis() - clientLeases[leaseIndex].leaseStartMs;
    const unsigned long remaining = elapsed >= wifi_config::kSessionDurationMs
        ? 0
        : wifi_config::kSessionDurationMs - elapsed;
    return {LeaseState::Active, remaining};
  }

  const int8_t blockedIndex = findBlockedIndexByMac(mac);
  if (blockedIndex >= 0) {
    return {LeaseState::Blocked, blockedEntries[blockedIndex].remainingMs};
  }

  // Connected but not tracked yet; a lease will be created on the next tick.
  return {LeaseState::Unknown, 0};
}

bool isBlocked(const IPAddress& ip) {
  return getLeaseStatus(ip).state == LeaseState::Blocked;
}

void updateLeases() {
    const unsigned long now = millis();
    bool connected[wifi_config::kMaxClientLeases] = {};
    bool blockedConnected[wifi_config::kMaxBlockedEntries] = {};

    // --- Pass 1: Walk current station list ---
    station_info* station = wifi_softap_get_station_info();

    while (station != nullptr) {
      const int8_t blockedIndex = findBlockedIndexByMac(station->bssid);
      if (blockedIndex >= 0) {
        blockedConnected[blockedIndex] = true;
      }

      const int8_t index = findLeaseIndexByMac(station->bssid);

      if (index >= 0) {
        // Existing client.
        clientLeases[index].lastSeenMs = now;
        connected[index] = true;

        if (!clientLeases[index].blocked && wifi_config::kSessionDurationMs != 0 &&
            (now - clientLeases[index].leaseStartMs) > wifi_config::kSessionDurationMs) {
          clientLeases[index].blocked = true;
          debug_logs::leaseLogging("Session expired for %s; blocked until disconnect", stationMacToString(station->bssid).c_str());
        }
      } else if (blockedIndex < 0) {
        // New client, and not waiting out a reconnect block either.
        newLeaseIndex result = addLease(station->bssid, now);
        if (result.success && result.index >= 0) { connected[result.index] = true; }
      }
      // else: reconnected mid-block.

      station = STAILQ_NEXT(station, next);
    }
    wifi_softap_free_station_info();

    // --- Pass 2: Tick blocked entries (only while disconnected). ---
    for (uint8_t i = 0; i < wifi_config::kMaxBlockedEntries; i++) {
      if (!blockedEntries[i].inUse) { continue; }

      const unsigned long elapsed = now - blockedEntries[i].lastTickMs;
      blockedEntries[i].lastTickMs = now;

      if (!blockedConnected[i]) {
        blockedEntries[i].remainingMs = elapsed >= blockedEntries[i].remainingMs
            ? 0
            : blockedEntries[i].remainingMs - elapsed;
      }

      if (blockedEntries[i].remainingMs == 0) {
        debug_logs::leaseLogging("Reconnect block cleared for %s", stationMacToString(blockedEntries[i].mac).c_str());
        blockedEntries[i] = {};
        if (numberOfBlocked > 0) { numberOfBlocked--; }
      }
    }

    // --- Pass 3: Remove leases for clients that have actually disconnected. ---
    for (uint8_t i = 0; i < wifi_config::kMaxClientLeases; i++) {
      if (!clientLeases[i].inUse) { continue; }

      const bool stale = !connected[i] && ((now - clientLeases[i].lastSeenMs) > wifi_config::kLeaseStaleMs);
      if (!stale) { continue; }

      if (clientLeases[i].blocked) {
        addBlockedEntry(clientLeases[i].mac, now);
      }

      debug_logs::leaseLogging(
        "Removing %s (duration=%lu ms, lastSeen=%lu ms ago)",
        stationMacToString(clientLeases[i].mac).c_str(),
        now - clientLeases[i].leaseStartMs,
        now - clientLeases[i].lastSeenMs);

      removeLease(i);
    }

    if (millis() - nowLoop >= debug_config::kWiFiLoopDelay) {
      debug_logs::leaseLogging("Finished updating leases.");
      debug_logs::leaseLogging("Number of active leases: %u", getLeaseCount());
      nowLoop = millis();
    }
}
/** @} */ // end of Public
