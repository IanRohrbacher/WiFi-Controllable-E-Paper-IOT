/**
 * @file wifi_lease.cpp
 *
 * @brief Implementation of per-client lease bookkeeping.
 *
 * @details
 * Tracks each connected client in a fixed-size @c clientLeases table (MAC,
 * start time, last-seen time, blocked flag) and enforces post-expiry reconnect
 * cooldowns via a separate @c blockedEntries table.
 *
 * @see updateLeases()
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

/** @brief Result of a new lease addition attempt. */
struct NewLeaseIndex {
    /** @brief Whether a lease was actually added. */
    bool success;
    /** @brief Index of the new lease in clientLeases, or -1 on failure. */
    int8_t index;
};

/** @brief One connected client's lease record. */
struct ClientLease {
  /** @brief Whether this slot holds a live lease. */
  bool inUse;
  /** @brief Station MAC address this lease belongs to. */
  uint8 mac[6];
  /** @brief millis() timestamp the session started at. */
  unsigned long leaseStartMs;
  /** @brief millis() timestamp this client was last seen connected. */
  unsigned long lastSeenMs;
  /** @brief Whether the session has expired while still connected. */
  bool blocked;
};
/** @brief Array to store all active client leases */
static ClientLease clientLeases[wifi_config::kMaxClientLeases] = {};
/** @brief Counter for the number of active leases */
static uint8_t numberOfLeases = 0;

/** @brief A reconnect block in progress for a MAC no longer connected. */
struct BlockedEntry {
  /** @brief Whether this slot holds a live blocked entry. */
  bool inUse;
  /** @brief Station MAC address this block applies to. */
  uint8 mac[6];
  /** @brief Milliseconds left to wait before this MAC may reconnect. */
  unsigned long remainingMs;
  /** @brief millis() timestamp remainingMs was last ticked down at. */
  unsigned long lastTickMs;
};
/** @brief Holding space for MACs serving a post-block reconnect wait. */
static BlockedEntry blockedEntries[wifi_config::kMaxBlockedEntries] = {};
/** @brief Counter for the number of blocked leases */
static uint8_t numberOfBlocked = 0;

/**
 * @brief Format a 6-byte MAC address as "XX:XX:XX:XX:XX:XX".
 *
 * @param mac Pointer to the 6-byte MAC address.
 *
 * @return The formatted address.
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
 * Logs on a miss, since a miss here means the connecting client is new (or was
 * removed as stale) and is about to get a fresh lease.
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
 * @brief Find the index of a free lease slot in the @c clientLeases array.
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
 * @details
 * Unlike @c findLeaseIndexByMac(), this does not log on a miss. It is probed
 * for every connected station on every tick (see @c updateLeases()), and most
 * stations are not blocked, so logging misses here would just be noise.
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
 * @brief Find the index of a free blocked-entry slot in the @c blockedEntries array.
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
 * @brief Find the in-use blocked entry with the least remaining time.
 *
 * @details
 * Used to make room in a full @c blockedEntries table by evicting this entry,
 * the least disruptive choice since it was closest to expiring naturally.
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
 * A no-op if @c wifi_config::kBlockedDurationMs is 0. If the table is full
 * and @p mac doesn't already have an entry, evicts whichever entry is soonest
 * to clear naturally (see @c findSoonestToClearBlockedIndex()) rather than
 * rejecting the new block.
 *
 * @param mac Pointer to the 6-byte MAC address.
 * @param now The current time in milliseconds.
 * 
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
 * @param mac Pointer to the 6-byte MAC address of the client to add a lease for.
 * @param now The current time in milliseconds, used as the lease's start and last-seen time.
 *
 * @return A @c NewLeaseIndex with @c success = true and @c index set to the
 * new lease's slot in @c clientLeases, or @c success = false and @c index = -1
 * if the lease table is full.
 *
 */
NewLeaseIndex addLease(const uint8* mac, unsigned long now) {
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
 * @param index The index of the lease to remove from the @c clientLeases array.
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

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the wifi lease module, declared in wifi_lease.h.
 * @{
 */
uint8_t getLeaseCount() {
  return numberOfLeases;
}

uint8_t getBlockedCount() {
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

    // Pass 1, walk the current station list.
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
        NewLeaseIndex result = addLease(station->bssid, now);
        if (result.success && result.index >= 0) { connected[result.index] = true; }
      }
      // else: reconnected mid-block.

      station = STAILQ_NEXT(station, next);
    }
    wifi_softap_free_station_info();

    // Pass 2, tick blocked entries (only while disconnected).
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

    // Pass 3, remove leases for clients that have actually disconnected.
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
