/**
 * @file idisplay_bwr_driver.h
 *
 * @brief Hardware-agnostic interface for a two-plane (black/red) e-paper display backend.
 * 
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Logical colors supported by a black/white/red panel.
 *
 * @details
 * The physical panel has two planes of pixels, one for black and one for red.
 * The two planes are mutually exclusive; a pixel cannot be both black and red
 * at the same time.
 *
 * Bit polarity: at the physical panel, bit=1 means "ink this pixel" and bit=0
 * means "leave it white" - that is the wire-level convention you'd expect from
 * a datasheet. But @c blackPlane() / @c redPlane() (and the bytes the browser
 * uploads, see data/js/bitmap.js's @c packFrame()) are the OPPOSITE: bit=0 =
 * colored, bit=1 = white.
 *
 * This is intentional, not a mismatch: the vendor's @c EPD_3IN52B_Display()
 * (lib/waveshare-epaper/EPD_3in52b.cpp) unconditionally inverts every byte
 * (`SendData(~blackimage[i])`, `SendData(~ryimage[i])`) before it goes out
 * over SPI. Since that inversion is fixed, unmodifiable vendor code, the
 * buffers on our side of it must hold the pre-inverted (opposite) polarity so
 * the two inversions cancel out and the physical result is correct.
 * 
 */
enum class DisplayColor : uint8_t
{
    White = 0,
    Black = 1,
    Red   = 2
};

/**
 * @brief Abstract backend for a black/white/red e-paper panel.
 *
 * @details
 * Implementations own the panel's frame buffers as fixed-size static storage
 * and are responsible only for hardware bring-up, teardown, and blitting
 * whatever bytes are currently in the plane buffers to the physical panel. No
 * drawing/decoding logic belongs here; callers write pixels directly into the
 * buffers returned by @c blackPlane() / @c redPlane().
 * 
 */
class IDisplayBWRDriver
{
public:
    virtual ~IDisplayBWRDriver() = default;

    /**
     * @brief Bring up the underlying hardware (GPIO, panel init).
     *
     * @retval true Hardware initialized successfully.
     * @retval false Hardware initialization failed.
     */
    virtual bool begin() = 0;

    /**
     * @brief Put the panel to sleep and release the hardware.
     */
    virtual void end() = 0;

    /**
     * @brief Fill both planes with a single solid color.
     *
     * @param color Color to fill both planes with.
     */
    virtual void clear(DisplayColor color) = 0;

    /**
     * @brief Blit the current contents of both planes to the panel.
     *
     * @retval true The panel was refreshed.
     * @retval false The driver has not been successfully started.
     */
    virtual bool present() = 0;

    /**
     * @brief Direct, writable pointer to the black plane buffer.
     */
    virtual uint8_t* blackPlane() = 0;

    /**
     * @brief Direct, writable pointer to the red plane buffer.
     */
    virtual uint8_t* redPlane() = 0;

    /**
     * @brief Size, in bytes, of a single plane buffer.
     */
    virtual size_t planeSize() const = 0;

    /**
     * @brief Native panel width, in pixels.
     */
    virtual uint16_t width() const = 0;

    /**
     * @brief Native panel height, in pixels.
     */
    virtual uint16_t height() const = 0;
};
