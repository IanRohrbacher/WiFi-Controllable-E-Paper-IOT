/**
 * @headerfile wifi_lease.h "src/wifi-service/wifi_lease.h"
 * 
 */

#pragma once

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
 * @return The status of the lease update attempt.
 * @retval true The leases were updated successfully.
 * @retval false An error occurred while updating leases, which may affect client management.
 * 
 */
bool updateLeases();
