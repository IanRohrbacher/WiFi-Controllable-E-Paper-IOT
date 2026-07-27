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
    /** @brief No record of this client yet, a lease will be created for them. */
    Unknown,
    /** @brief Actively within their session, @c remainingMs counts down. */
    Active,
    /** @brief Session expired (still connected) or waiting out the reconnect
     * block after disconnecting. @c remainingMs is how long until they may
     * reconnect, frozen while they remain connected. */
    Blocked,
    /** @brief Session length is disabled (@c wifi_config::kSessionDurationMs == 0), never blocks. */
    Unlimited
};

/**
 * @brief Snapshot of a client's lease state for a single IP address.
 */
struct LeaseStatus {
    /** @brief Current session/block state. */
    LeaseState state;
    /** @brief Milliseconds remaining, meaning depends on state. */
    unsigned long remainingMs;
};

/**
 * @brief Current number of active client leases.
 *
 * @par Parameters
 * None.
 *
 * @return The number of active client leases.
 *
 */
uint8_t getLeaseCount();

/**
 * @brief Current number of MACs waiting out a reconnect block.
 *
 * @par Parameters
 * None.
 *
 * @return The number of active blocked entries.
 *
 */
uint8_t getBlockedCount();

/**
 * @brief Current number of MACs with banked, unused session time.
 *
 * @details
 * Counts clients who disconnected before their session expired and are
 * carrying leftover session time forward to their next reconnect.
 *
 * @par Parameters
 * None.
 *
 * @return The number of active stale entries.
 *
 */
uint8_t getStaleCount();

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
 * Convenience wrapper over @c getLeaseStatus() for callers that only need a
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
 * Call regularly from the main loop. Walks the live station list to add new
 * leases and resume any banked session time for returning client, if any, and
 * refresh @c lastSeenMs, ticks blocked-entry and banked-time timers for MACs
 * that are currently disconnected, then removes any lease that has gone stale,
 * either starting a reconnect block (session had already expired) or banking
 * the unused remainder of its session time (session had not).
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 * @see wifi_config::kLeaseStaleMs
 *
 */
void updateLeases();
