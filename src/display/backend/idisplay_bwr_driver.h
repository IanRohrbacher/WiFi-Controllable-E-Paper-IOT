/**
 * @file idisplay_bwr_driver.h
 * @headerfile idisplay_bwr_driver.h "src/display/backend/idisplay_bwr_driver.h"
 *
 * @brief Hardware agnostic interface for a two plane (black/red) e-paper display backend.
 *
 * @details
 * This is a header only module, the interface declared here has no
 * implementation of its own. Each concrete panel backend (such as @c
 * Waveshare352bDriver) implements it directly against this contract.
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
 * The two planes are mutually exclusive, a pixel cannot be both black and
 * red at the same time.
 *
 * @warning
 * At the physical panel, bit=1 means "ink this pixel" and bit=0 means "leave
 * it white", the wire level convention you would expect from a datasheet. But
 * @c blackPlane() / @c redPlane() (and the bytes the browser uploads, see
 * website-files/js/bitmap.js's @c packFrame()) are the opposite, bit=0 is
 * colored and bit=1 is white. Do not "correct" this if it looks backwards, it
 * is intentional.
 *
 * @note
 * The vendor's @c EPD_3IN52B_Display() (lib/waveshare-epaper/EPD_3in52b.cpp)
 * unconditionally inverts every byte (`SendData(~blackimage[i])`,
 * `SendData(~ryimage[i])`) before it goes out over SPI. Since that inversion
 * is fixed, unmodifiable vendor code, the buffers on our side of it must hold
 * the pre inverted, opposite, polarity so the two inversions cancel out and
 * the physical result is correct.
 *
 */
enum class DisplayColor : uint8_t
{
    /** @brief Neither plane is inked for this pixel. */
    White = 0,
    /** @brief The black plane is inked for this pixel. */
    Black = 1,
    /** @brief The red plane is inked for this pixel. */
    Red   = 2
};

/**
 * @brief Abstract backend for a black/white/red e-paper panel.
 *
 * @details
 * Implementations own the panel's frame buffers as fixed-size static storage
 * and are responsible only for hardware bring up, teardown, and blitting
 * whatever bytes are currently in the plane buffers to the physical panel.
 * Pixel content always originates from the browser ( @see
 * website-files/js/bitmap.js), which uploads already packed plane bytes, so
 * no backend ever draws or decodes anything itself, it only stores and blits.
 *
 * @warning
 * @c begin() must succeed before @c clear(), @c flip(), @c blackPlane(), or @c
 * redPlane() are used. Implementations are not required to guard every one of
 * those calls against a never started driver.
 *
 * @note
 * @c blackPlane() / @c redPlane() return raw pointers with no bounds checking
 * of their own, callers must never write past @c planeSize() bytes from either
 * pointer.
 *
 */
class IDisplayBWRDriver
{
public:
    virtual ~IDisplayBWRDriver() = default;

    /**
     * @brief Bring up the underlying hardware (GPIO, panel init).
     *
     * @par Parameters
     * None.
     *
     * @return Whether hardware initialization succeeded.
     * @retval true Hardware initialized successfully.
     * @retval false Hardware initialization failed.
     * 
     */
    virtual bool begin() = 0;

    /**
     * @brief Put the panel to sleep and release the hardware.
     *
     * @par Parameters
     * None.
     *
     * @par Returns
     * Nothing.
     * 
     */
    virtual void end() = 0;

    /**
     * @brief Fill both planes with a single solid color.
     *
     * @param color Color to fill both planes with.
     *
     * @par Returns
     * Nothing.
     * 
     */
    virtual void clear(DisplayColor color) = 0;

    /**
     * @brief Blit the current contents of both planes to the panel.
     *
     * @par Parameters
     * None.
     *
     * @return Whether the panel was refreshed.
     * @retval true The panel was refreshed.
     * @retval false The driver has not been successfully started.
     * 
     */
    virtual bool flip() = 0;

    /**
     * @brief Direct, writable pointer to the black plane buffer.
     *
     * @par Parameters
     * None.
     *
     * @return Pointer to the start of the black plane buffer.
     * 
     */
    virtual uint8_t* blackPlane() = 0;

    /**
     * @brief Direct, writable pointer to the red plane buffer.
     *
     * @par Parameters
     * None.
     *
     * @return Pointer to the start of the red plane buffer.
     * 
     */
    virtual uint8_t* redPlane() = 0;

    /**
     * @brief Size, in bytes, of a single plane buffer.
     *
     * @par Parameters
     * None.
     *
     * @return Size, in bytes, of one plane buffer.
     * 
     */
    virtual size_t planeSize() const = 0;

    /**
     * @brief Native panel width, in pixels.
     *
     * @par Parameters
     * None.
     *
     * @return Native panel width, in pixels.
     * 
     */
    virtual uint16_t width() const = 0;

    /**
     * @brief Native panel height, in pixels.
     *
     * @par Parameters
     * None.
     *
     * @return Native panel height, in pixels.
     * 
     */
    virtual uint16_t height() const = 0;
};
