/**
 * @headerfile generic_io.h "src/io/generic_io.h"
 *
 */

#pragma once

/** @brief Edge and level state of one @c Generic_io's digital pin. */
struct IO_State
{
    /** @brief True for exactly the tick the pin transitions LOW to HIGH. */
    bool risingEdge;
    /** @brief True for exactly the tick the pin transitions HIGH to LOW. */
    bool fallingEdge;
    /** @brief True for every tick the pin currently reads HIGH. */
    bool whileHigh;
    /** @brief True for every tick the pin currently reads LOW. */
    bool whileLow;
};

/**
 * @brief Update an @c IO_State for a new tick's digital level.
 *
 * @details
 * Compares @p currentlyHigh against the previous tick's @c whileHigh to
 * compute the edge flags, then overwrites @c whileHigh/@c whileLow with the
 * new level for the next call. Callers only need to feed in the raw pin read,
 * this is what turns that into rising/falling/level state.
 *
 * @param state State to update in place.
 * @param currentlyHigh Whether the pin reads HIGH this tick.
 *
 * @par Returns
 * Nothing.
 *
 */
inline void updateIoState(IO_State& state, bool currentlyHigh)
{
    state.risingEdge = currentlyHigh && !state.whileHigh;
    state.fallingEdge = !currentlyHigh && state.whileHigh;
    state.whileHigh = currentlyHigh;
    state.whileLow = !currentlyHigh;
}

/**
 * @brief Seed an @c IO_State's level from an initial pin read.
 *
 * @details
 * Call once from @c begin(), right after the initial digitalRead(), so the
 * first @c updateIoState() call afterward computes edges relative to the pin's
 * actual starting level instead of the implicit LOW a freshly zeroed @c
 * IO_State would otherwise be compared against.
 *
 * @param state State to seed.
 * @param currentlyHigh Whether the pin reads HIGH at startup.
 *
 * @par Returns
 * Nothing.
 *
 */
inline void seedIoState(IO_State& state, bool currentlyHigh)
{
    state = IO_State{};
    state.whileHigh = currentlyHigh;
    state.whileLow = !currentlyHigh;
}

/**
 * @brief Whether state's pin transitioned LOW to HIGH this tick.
 *
 * @param state State to read.
 *
 * @return Whether this is the rising edge tick.
 *
 */
inline bool onRisingEdge(const IO_State& state) { return state.risingEdge; }

/**
 * @brief Whether state's pin transitioned HIGH to LOW this tick.
 *
 * @param state State to read.
 *
 * @return Whether this is the falling edge tick.
 *
 */
inline bool onFallingEdge(const IO_State& state) { return state.fallingEdge; }

/**
 * @brief Whether state's pin currently reads HIGH.
 *
 * @param state State to read.
 *
 * @return Whether the pin is currently HIGH.
 *
 */
inline bool whileTrue(const IO_State& state) { return state.whileHigh; }

/**
 * @brief Whether state's pin currently reads LOW.
 *
 * @param state State to read.
 *
 * @return Whether the pin is currently LOW.
 *
 */
inline bool whileFalse(const IO_State& state) { return state.whileLow; }

/**
 * @brief Interface for one scheduler-managed IO
 *
 * @details
 * Implementations own one pin (or group of pins) and decide, in @c
 * isReady(), whether their @c action() should run this tick. The
 * scheduler that ticks these (see @c io_scheduler.h) calls @c begin()
 * once at startup, then @c isReady() every tick, calling @c action()
 * only when @c isReady() returns true.
 *
 */
class Generic_io
{
public:
    virtual ~Generic_io() = default;

    /**
     * @brief Configure this IO's pin mode.
     *
     * @par Parameters
     * None.
     *
     * @par Returns
     * Nothing.
     *
     */
    virtual void begin() = 0;

    /**
     * @brief Read this IO's pin and decide whether its action should run.
     *
     * @details
     * Implementations should read the pin, feed it through @c
     * updateIoState(), then answer using whichever of @c onRisingEdge(),
     * @c onFallingEdge(), @c whileTrue(), or @c whileFalse() matches the
     * trigger this IO wants, so the edge/level logic itself never needs
     * to be reimplemented or hand mixed per IO.
     *
     * @par Parameters
     * None.
     *
     * @return Whether @c action() should run this tick.
     *
     */
    virtual bool isReady() = 0;

    /**
     * @brief Run this IO's action for the current tick.
     *
     * @par Parameters
     * None.
     *
     * @par Returns
     * Nothing.
     *
     */
    virtual void action() = 0;
};
