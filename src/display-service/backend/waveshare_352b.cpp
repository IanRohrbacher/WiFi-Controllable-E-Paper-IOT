/**
 * @file waveshare_352b.cpp
 *
 * @brief Implementation of the Waveshare 3.52" IDisplayDriver backend.
 */

#include <string.h>

#include <DEV_Config.h>

#include "waveshare_352b.h"
#include "logger.h"

bool Waveshare352bDriver::begin()
{
    if (started_) return true;

    if (DEV_Module_Init() != 0) {
        debug_logs::displayLogging("Failed to initialize e-paper GPIO module.");
        return false;
    }

    EPD_3IN52B_Init();
    clear(DisplayColor::White);
    started_ = true;

    debug_logs::displayLogging("Waveshare 3.52in driver started.");
    return true;
}

void Waveshare352bDriver::end()
{
    if (!started_) return;

    EPD_3IN52B_sleep();
    DEV_Module_Exit();
    started_ = false;

    debug_logs::displayLogging("Waveshare 3.52in driver stopped.");
}

void Waveshare352bDriver::clear(DisplayColor color)
{
    // bit=1 -> white, bit=0 -> colored; see the DisplayColor doc comment in
    // idisplay_bwr_driver.h for why this is inverted from the panel's own
    // wire-level convention.
    switch (color) {
        case DisplayColor::Black:
            memset(blackPlane_, 0x00, kPlaneSize);
            memset(redPlane_, 0xFF, kPlaneSize);
            break;
        case DisplayColor::Red:
            memset(blackPlane_, 0xFF, kPlaneSize);
            memset(redPlane_, 0x00, kPlaneSize);
            break;
        case DisplayColor::White:
        default:
            memset(blackPlane_, 0xFF, kPlaneSize);
            memset(redPlane_, 0xFF, kPlaneSize);
            break;
    }
}

bool Waveshare352bDriver::present()
{
    if (!started_) return false;

    EPD_3IN52B_Display(blackPlane_, redPlane_);
    return true;
}

uint8_t* Waveshare352bDriver::blackPlane()
{
    return blackPlane_;
}

uint8_t* Waveshare352bDriver::redPlane()
{
    return redPlane_;
}

size_t Waveshare352bDriver::planeSize() const
{
    return kPlaneSize;
}

uint16_t Waveshare352bDriver::width() const
{
    return EPD_3IN52B_WIDTH;
}

uint16_t Waveshare352bDriver::height() const
{
    return EPD_3IN52B_HEIGHT;
}
