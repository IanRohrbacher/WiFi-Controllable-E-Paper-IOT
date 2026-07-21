/**
 * @file display.h
 *
 * @brief Public API of the display module.
 *
 * @details
 * The display module owns exactly one hardware backend and exposes:
 *  - lifecycle control (start/stop/refresh/clear), and
 *  - a streaming parser that accepts an uploaded bitmap (header +
 *    black plane + red plane) in arbitrarily-sized chunks and writes
 *    each chunk directly into the backend's frame buffers.
 *
 * No drawing primitives are provided here; all pixel content is
 * produced by the browser (see web-interface/js/bitmap.js) and uploaded as
 * already-packed 1bpp bitplanes.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "display_protocol.h"
#include "backend/idisplay_bwr_driver.h"

/**
 * @defgroup Public
 * Public API for the display module.
 * @{
 */

/**
 * @brief Start the display module: bring up the panel hardware.
 *
 * @param clearScreen When true, the panel is cleared to white immediately
 * after a successful start.
 *
 * @retval true The panel was initialized successfully.
 * @retval false Hardware initialization failed.
 */
bool startDisplayModule(bool clearScreen = true);

/**
 * @brief Stop the display module: sleep the panel and release the hardware.
 *
 * @retval true The module was running and has been stopped.
 * @retval false The module was not running.
 */
bool stopDisplayModule();

/**
 * @brief Push the current contents of both plane buffers to the panel.
 *
 * @retval true The panel was refreshed.
 * @retval false The module has not been started.
 */
bool refreshDisplay();

/**
 * @brief Fill both plane buffers with a single solid color and refresh.
 *
 * @param color Color to fill the panel with.
 *
 * @retval true The panel was cleared and refreshed.
 * @retval false The module has not been started.
 */
bool clearDisplay(DisplayColor color);

/**
 * @brief Native width, in pixels, of the active backend's panel.
 */
uint16_t displayWidth();

/**
 * @brief Native height, in pixels, of the active backend's panel.
 */
uint16_t displayHeight();

/**
 * @brief Begin accepting a new uploaded frame.
 *
 * @details
 * Resets the streaming parser's internal state. Must be called once
 * before the first call to @c writeFrameChunk() for a given upload.
 *
 * @return Always @c DisplayStatus::Success.
 */
DisplayStatus beginFrameUpload();

/**
 * @brief Feed the next chunk of an in-progress frame upload.
 *
 * @details
 * The first @c sizeof(BitmapHeader) bytes received (across however
 * many calls it takes to accumulate them) are parsed and validated as
 * a @c BitmapHeader. Every byte after that is written directly into
 * the active backend's black or red plane buffer as it arrives -
 * there is no intermediate full-frame staging buffer. A running CRC32
 * is accumulated over the plane bytes for later verification in
 * @c finishFrameUpload().
 *
 * @param data Pointer to the chunk's bytes.
 * @param length Number of bytes available at @p data.
 *
 * @retval DisplayStatus::Success The chunk was consumed successfully.
 * @retval DisplayStatus::NotInitialized @c beginFrameUpload() was not
 * called first (or the upload already failed).
 * @retval DisplayStatus::InvalidHeader The magic number did not match.
 * @retval DisplayStatus::InvalidVersion The bitmap format version is
 * not supported.
 * @retval DisplayStatus::InvalidEncoding The bitmap encoding is not
 * supported.
 * @retval DisplayStatus::InvalidDimensions The header's width/height
 * do not match the panel.
 * @retval DisplayStatus::BufferTooSmall More bytes were received than
 * the frame requires.
 */
DisplayStatus writeFrameChunk(const uint8_t* data, size_t length);

/**
 * @brief Finalize an in-progress frame upload.
 *
 * @details
 * Verifies that a complete frame was received and that its CRC32
 * matches the header, then leaves the result already sitting in the
 * backend's plane buffers (call @c refreshDisplay() to push it to the
 * panel).
 *
 * @retval DisplayStatus::Success The frame was received and verified.
 * @retval DisplayStatus::InvalidDimensions Fewer bytes were received
 * than the frame requires.
 * @retval DisplayStatus::InvalidChecksum The received CRC32 did not
 * match the header's crc32 field.
 */
DisplayStatus finishFrameUpload();

/**
 * @brief Human-readable message for a @c DisplayStatus value.
 *
 * @param status Status to describe.
 * @return A statically-allocated, null-terminated description string.
 */
const char* displayStatusMessage(DisplayStatus status);

/** @} */ // end of Public
