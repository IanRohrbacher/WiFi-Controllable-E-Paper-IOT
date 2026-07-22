/**
 * @headerfile wifi_lease.h "src/wifi/wifi_lease.h"
 *
 */

#pragma once

#include <IPAddress.h>

/**
 * @brief A connected client's session/block state, as reported to the web
 * layer for display and enforcement.
 */
enum class LeaseState : uint8_t {
    /** No record of this client yet; a lease will be created for them. */
    Unknown,
    /** Actively within their session; @c remainingMs counts down. */
    Active,
    /** Session expired (still connected) or waiting out the reconnect block
     * after disconnecting; @c remainingMs is how long until they may
     * reconnect, frozen while they remain connected. */
    Blocked,
    /** Session length is disabled (kSessionDurationMs == 0); never blocks. */
    Unlimited
};

/**
 * @brief Snapshot of a client's lease state for a single IP address.
 */
struct LeaseStatus {
    LeaseState state;
    unsigned long remainingMs;
};

/**
 * @brief Get the current number of active client leases.
 *
 * @details
 * This function returns the number of client leases that are currently active.
 * This can be used to monitor how many clients are currently connected to the
 * access point and have active leases.
 *
 * @par Parameters
 * None.
 *
 * @return The number of active client leases.
 *
 */
uint8_t const getLeaseCount();

/**
 * @brief Get the current number of MACs waiting out a reconnect block.
 *
 * @details
 * This function returns the number of blocked-entry slots currently in use,
 * i.e. clients who timed out and have since disconnected but haven't waited
 * out the full reconnect block yet.
 *
 * @par Parameters
 * None.
 *
 * @return The number of active blocked entries.
 *
 */
uint8_t const getBlockedCount();

/**
 * @brief Get the session/block status for the client at the given IP.
 *
 * @details
 * Resolves the IP to its station MAC address (via the live AP station list)
 * and reports whether that client is actively within its session, blocked
 * (still serving out, or waiting to serve, a reconnect block), or has no
 * record yet.
 *
 * @param ip IP address of the requesting client.
 *
 * @return The client's current lease status.
 *
 */
LeaseStatus getLeaseStatus(const IPAddress& ip);

/**
 * @brief Check whether the client at the given IP is currently blocked.
 *
 * @details
 * Convenience wrapper over getLeaseStatus() for callers that only need a
 * yes/no answer, e.g. to gate an API endpoint.
 *
 * @param ip IP address of the requesting client.
 *
 * @retval true The client's session has expired or they are waiting out a
 * reconnect block.
 * @retval false The client is unblocked (active session, unlimited
 * sessions, or no record yet).
 *
 */
bool isBlocked(const IPAddress& ip);

/**
 * @brief Update the status of all client leases.
 *
 * @details
 * This function checks the status of all active client leases and updates
 * their information accordingly. It should be called regularly (e.g., from
 * the main loop) to ensure that leases are properly maintained and that
 * expired leases are cleaned up. The function handles both the addition of new
 * leases for newly connected clients and the removal of leases that have
 * expired or become stale.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing
 *
 */
void updateLeases();
