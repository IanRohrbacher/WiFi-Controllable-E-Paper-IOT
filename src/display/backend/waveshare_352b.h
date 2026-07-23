/**
 * @headerfile waveshare_352b.h "src/display/backend/waveshare_352b.h"
 *
 */

#pragma once

#include <EPD_3in52b.h>

#include "idisplay_bwr_driver.h"

/**
 * @brief Drives the Waveshare 3.52 inch (EPD_3IN52B) panel through the vendor's low level DEV_Config/EPD_3in52b routines.
 *
 * @details
 * Frame storage is two fixed-size plane buffers, sized directly from the
 * vendor header's @c EPD_3IN52B_WIDTH/HEIGHT, the single source of truth for
 * this panel's resolution, and @c flip() hands them straight to @c
 * EPD_3IN52B_Display().
 *
 */
class Waveshare352bDriver : public IDisplayBWRDriver
{
public:
    /** @brief See @c IDisplayBWRDriver::begin(). */
    bool begin() override;
    /** @brief See @c IDisplayBWRDriver::end(). */
    void end() override;
    /** @brief See @c IDisplayBWRDriver::clear(). */
    void clear(DisplayColor color) override;
    /** @brief See @c IDisplayBWRDriver::flip(). */
    bool flip() override;
    /** @brief See @c IDisplayBWRDriver::blackPlane(). */
    uint8_t* blackPlane() override;
    /** @brief See @c IDisplayBWRDriver::redPlane(). */
    uint8_t* redPlane() override;
    /** @brief See @c IDisplayBWRDriver::planeSize(). */
    size_t planeSize() const override;
    /** @brief See @c IDisplayBWRDriver::width(). */
    uint16_t width() const override;
    /** @brief See @c IDisplayBWRDriver::height(). */
    uint16_t height() const override;

private:
    /** @brief Bytes per pixel row, rounded up to a whole byte. */
    static constexpr size_t kRowBytes = (EPD_3IN52B_WIDTH + 7) / 8;
    /** @brief Total bytes in one plane buffer. */
    static constexpr size_t kPlaneSize = kRowBytes * EPD_3IN52B_HEIGHT;

    /** @brief Static storage for the black plane, no heap allocation. */
    uint8_t blackPlane_[kPlaneSize];
    /** @brief Static storage for the red plane, no heap allocation. */
    uint8_t redPlane_[kPlaneSize];
    /** @brief Whether @c begin() has succeeded and @c end() has not yet run. */
    bool started_ = false;
};
