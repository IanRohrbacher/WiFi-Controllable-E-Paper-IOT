/**
 * @headerfile clear_button.h "src/io/clear_button.h"
 *
 */

#pragma once

#include "generic_io.h"

/** @brief A physical button read on @c io_config::kClearButtonPin. */
class Clear_button : public Generic_io
{
public:
    Clear_button() = default;
    ~Clear_button() override = default;

    /** @brief See Generic_io::begin(). */
    void begin() override;

    /** @brief See Generic_io::isReady(). */
    bool isReady() override;

    /** @brief See Generic_io::action(). */
    void action() override;

    /** @brief See Generic_io::isHeld(). True while the button is physically pressed (its pin reads LOW, see the INPUT_PULLUP wiring in begin()). */
    bool isHeld() override;

    /** @brief Edge and level state of this button's pin. */
    IO_State state = {};
};
