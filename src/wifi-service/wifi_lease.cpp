/**
 * @file wifi_lease.cpp
 * 
 * @brief Implementation of per-client lease bookkeeping.
 * 
 * @details
 * This module manages client leases for the WiFi access point. It keeps track
 * of connected clients, their MAC addresses, and the duration of their leases.
 * The module provides functions to add new leases when clients connect,
 * update existing leases, and remove leases when clients disconnect or when
 * leases expire. It uses the ESP8266 WiFi library to retrieve information
 * about connected stations and integrates with the main WiFi controller to
 * ensure that client management is handled effectively.
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
 * Member variables/functions used internally by the wifi lease service.
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
};

/** @brief Array to store all active client leases */
static ClientLease clientLeases[wifi_config::kMaxClientLeases] = {};
/** @brief Counter for the number of active leases */
static uint8_t numberOfLeases = 0;

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

}  // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the wifi lease service, declared in wifi_lease.h.
 * @{
 */
uint8_t const getLeaseCount() {
  return numberOfLeases;
}

bool updateLeases() {
    const unsigned long now = millis();
    bool connected[wifi_config::kMaxClientLeases] = {};

    // --- Pass 1: Walk current station list ---
    try {
      station_info* station = wifi_softap_get_station_info();
      
      while (station != nullptr) {
        int8_t index = findLeaseIndexByMac(station->bssid);
        
        if (index >= 0) {
          // Existing client.
          clientLeases[index].lastSeenMs = now;
          connected[index] = true;
        } else {
          // New client.
          newLeaseIndex result = addLease(station->bssid, now);
          if (result.success && result.index >= 0) { connected[result.index] = true; }
        }
        
        station = STAILQ_NEXT(station, next);
      }
      wifi_softap_free_station_info();
    } catch (const std::exception& e) {
      debug_logs::leaseLogging("Error updating leases: %s", e.what());
      return false;
    }

    // --- Pass 2: Remove expired leases. ---
    try {
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
    } catch (const std::exception& e) {
      debug_logs::leaseLogging("Error during lease cleanup: %s", e.what());
      return false;
    }
    return true;
}
/** @} */ // end of Public
