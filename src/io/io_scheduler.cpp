/**
 * @file io_scheduler.cpp
 *
 * @brief Implementation of the IO scheduler.
 *
 * @details
 * A command scheduler for @c Generic_io instances. Every registered IO gets
 * its @c isReady() polled once per tick, and @c action() run when it returns
 * true, on the scheduler's own cooperative thread so IO handling stays
 * independent of the web server and display threads.
 *
 */

#include <thread.h>

#include "io_scheduler.h"
#include "generic_io.h"
#include "clear_button.h"
#include "../configs.h"
#include "../logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the IO scheduler.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {

/** @brief The clear button IO. */
Clear_button clearButton;

/** @brief Every IO the scheduler ticks, in registration order. */
Generic_io* const ios[] = {
    &clearButton,
};

/** @brief Number of entries in @c ios. */
constexpr size_t kIoCount = sizeof(ios) / sizeof(ios[0]);

/**
 * @brief One cooperative thread tick, polling every registered IO.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void ioTick() {
    for (size_t i = 0; i < kIoCount; ++i) {
        if (ios[i]->isReady()) {
            debug_logs::ioLogging("IO %u action triggered", i);
            ios[i]->action();
        }
    }
}

/** @brief Thread for ticking the IO scheduler. */
Thread ioThread = Thread([]() {
    ioTick();
});

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the IO scheduler, declared in io_scheduler.h.
 * @{
 */
void startIoModule() {
    for (size_t i = 0; i < kIoCount; ++i) {
        ios[i]->begin();
    }

    ioThread.setInterval(io_config::kThreadRefreshIntervalMs);

    debug_logs::ioLogging("Started IO scheduler with %u registered IOs", kIoCount);
}

void updateIoModule() {
    if (ioThread.shouldRun()) ioThread.run();
}
/** @} */ // end of Public
