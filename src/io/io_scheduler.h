/**
 * @headerfile io_scheduler.h "src/io/io_scheduler.h"
 *
 */

#pragma once

/**
 * @brief Start the IO scheduler, bringing up every registered IO.
 *
 * @details
 * Calls @c begin() on every registered @c Generic_io in order, then starts the
 * scheduler's own cooperative thread, see @c updateIoModule().
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void startIoModule();

/**
 * @brief Run the IO scheduler's own cooperative thread tick, if it is due.
 *
 * @details
 * Call regularly from the main loop. Runs on its own interval, see @c
 * io_config::kThreadRefreshIntervalMs, independent of the web server's thread.
 * Each tick, calls @c isReady() on every registered @c Generic_io in order,
 * and @c action() on whichever ones return true.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateIoModule();
