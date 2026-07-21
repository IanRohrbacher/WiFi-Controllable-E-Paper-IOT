/**
 * @file waveshare_352b.h
 *
 * @brief IDisplayDriver backend for the Waveshare 3.52" e-paper panel.
 */

#pragma once

#include <EPD_3in52b.h>

#include "idisplay_bwr_driver.h"

/**
 * @brief Drives the Waveshare 3.52" (EPD_3IN52B) panel directly through the vendor's low-level DEV_Config/EPD_3in52b routines.
 *
 * @details
 * No GUI_Paint code is used anywhere in this class. Frame storage is two
 * fixed-size plane buffers, sized directly from the vendor header's @c
 * EPD_3IN52B_WIDTH/HEIGHT - the single source of truth for this panel's
 * resolution - and @c present() hands them straight to @c EPD_3IN52B_Display().
 *
 */
class Waveshare352bDriver : public IDisplayBWRDriver
{
public:
    bool begin() override;
    void end() override;
    void clear(DisplayColor color) override;
    bool present() override;
    uint8_t* blackPlane() override;
    uint8_t* redPlane() override;
    size_t planeSize() const override;
    uint16_t width() const override;
    uint16_t height() const override;

private:
    static constexpr size_t kRowBytes = (EPD_3IN52B_WIDTH + 7) / 8;
    static constexpr size_t kPlaneSize = kRowBytes * EPD_3IN52B_HEIGHT;

    uint8_t blackPlane_[kPlaneSize];
    uint8_t redPlane_[kPlaneSize];
    bool started_ = false;
};
