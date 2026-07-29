/**
 * @headerfile display.h "src/display/display.h"
 *
 * @details
 * An uploaded frame is RLE-compressed straight into a fixed-size ring buffer
 * of queue slots as it streams in, and @c updateDisplayModule() uploads the
 * next queued frame. This queue is then updated at a controlled rate by @c
 * display_config::kDisplayCooldownMs to showcase the current frame for a
 * minimum duration before moving on to the next one.
 * 
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
 * @brief Start the display module, bringing up the panel hardware.
 *
 * @details
 * Retries the backend's @c begin() up to @c
 * display_config::kDriverInitAttempts times before giving up.
 *
 * @param clearScreen When true, the panel is cleared to white immediately
 * after a successful start.
 *
 * @return Whether the panel is ready to use.
 * @retval true The panel was initialized successfully.
 * @retval false Hardware initialization failed.
 * 
 */
bool startDisplayModule(bool clearScreen = true);

/**
 * @brief Stop the display module, sleeping the panel and releasing the hardware.
 *
 * @par Parameters
 * None.
 *
 * @return Whether the module was stopped.
 * @retval true The module was running and has been stopped.
 * @retval false The module was not running.
 * 
 */
bool stopDisplayModule();

/**
 * @brief Push the current contents of both plane buffers to the panel.
 *
 * @par Parameters
 * None.
 *
 * @return Whether the panel was refreshed.
 * @retval true The panel was refreshed.
 * @retval false The module has not been started.
 * 
 */
bool refreshDisplay();

/**
 * @brief Fill both plane buffers with a single solid color and refresh.
 *
 * @param color Color to fill the panel with.
 *
 * @return Whether the panel was cleared and refreshed.
 * @retval true The panel was cleared and refreshed.
 * @retval false The module has not been started.
 * 
 */
bool clearDisplay(DisplayColor color);

/**
 * @brief Native width, in pixels, of the active backend's panel.
 *
 * @par Parameters
 * None.
 *
 * @return Native panel width, in pixels.
 * 
 */
uint16_t displayWidth();

/**
 * @brief Native height, in pixels, of the active backend's panel.
 *
 * @par Parameters
 * None.
 *
 * @return Native panel height, in pixels.
 * 
 */
uint16_t displayHeight();

/**
 * @brief Check whether the frame queue currently has room for a new upload.
 *
 * @details
 * Callers should check this before accepting any bytes for a new upload, see
 * @c website.cpp's @c /api/display/frame handler.
 *
 * @par Parameters
 * None.
 *
 * @return Whether a new frame may be queued right now.
 * @retval DisplayStatus::Success There is a free queue slot.
 * @retval DisplayStatus::Busy Every queue slot is currently in use.
 *
 */
DisplayStatus displayQueueStatus();

/**
 * @brief Check whether the frame queue is currently empty.
 *
 * @par Parameters
 * None.
 *
 * @return Whether the frame queue is empty.
 * @retval true The frame queue is empty.
 * @retval false There is at least one frame queued for upload.
 *
 */
bool isDisplayQueueEmpty();

/**
 * @brief Milliseconds until the panel is next eligible to update.
 *
 * @details
 * A read only peek at the same cooldown timer @c updateDisplayModule()
 * ticks, useful for telling a caller rejected by @c displayQueueStatus()
 * roughly how long until a queue slot frees up, since freeing a slot and
 * the panel updating happen together.
 *
 * @par Parameters
 * None.
 *
 * @return Milliseconds remaining, or 0 if the panel is already eligible
 * to update right now.
 *
 */
unsigned long displayNextUpdateMs();

/**
 * @brief Override the panel update cooldown timer directly.
 *
 * @details
 * Lets a caller outside the normal upload flow force the current queued
 * frame's timing. For example an IO action that wants the panel to
 * update immediately (pass 0) regardless of @c
 * display_config::kDisplayCooldownMs.
 *
 * @param cooldownMs Milliseconds until the panel is next eligible to update.
 * 0 makes it eligible on the very next @c updateDisplayModule() tick.
 *
 * @par Returns
 * Nothing.
 *
 */
void setNextUpdateCooldownMs(unsigned long cooldownMs);

/**
 * @brief Begin accepting a new uploaded frame.
 *
 * @details
 * Resets the streaming parser's internal state and reserves the next free
 * queue slot for it. Must be called once before the first call to @c
 * writeFrameChunk() for a given upload.
 *
 * @see displayQueueStatus()
 *
 * @par Parameters
 * None.
 *
 * @return Whether a slot was reserved for this upload.
 * @retval DisplayStatus::Success A slot was reserved.
 * @retval DisplayStatus::Busy Every queue slot is currently in use, this upload was rejected.
 *
 */
DisplayStatus beginFrameUpload();

/**
 * @brief Feed the next chunk of an in-progress frame upload.
 *
 * @details
 * The first @c sizeof(BitmapHeader) bytes received are parsed and validated as
 * a @c BitmapHeader. Every byte after that is RLE-compressed on the fly into
 * the reserved queue slot. A running CRC32 is accumulated over the raw plane
 * bytes for later verification in @c finishFrameUpload().
 *
 * @param data Pointer to the chunk's bytes.
 * @param length Number of bytes available at @p data.
 *
 * @return Whether this chunk was consumed successfully.
 * @retval DisplayStatus::Success The chunk was consumed successfully.
 * @retval DisplayStatus::NotInitialized @c beginFrameUpload() was not called
 * first (or the upload already failed).
 * @retval DisplayStatus::InvalidHeader The magic number did not match.
 * @retval DisplayStatus::InvalidVersion The bitmap format version is not
 * supported.
 * @retval DisplayStatus::InvalidEncoding The bitmap encoding is not supported.
 * @retval DisplayStatus::InvalidDimensions The header's width/height do not
 * match the panel.
 * @retval DisplayStatus::BufferTooSmall The frame's compressed size exceeds
 * @c display_config::kDisplayQueueSlotCapacity.
 *
 */
DisplayStatus writeFrameChunk(const uint8_t* data, size_t length);

/**
 * @brief Finalize an in-progress frame upload.
 *
 * @details
 * Verifies that a complete frame was received and that its CRC32 matches the
 * header, then marks the reserved queue slot as ready to dispatch. @c
 * updateDisplayModule() pushes it to the panel on a later tick, once its turn
 * comes up.
 *
 * @par Parameters
 * None.
 *
 * @return Whether a complete, verified frame was received and queued.
 * @retval DisplayStatus::Success The frame was received, verified, and queued.
 * @retval DisplayStatus::InvalidDimensions Fewer bytes were received
 * than the frame requires.
 * @retval DisplayStatus::InvalidChecksum The received CRC32 did not
 * match the header's crc32 field.
 *
 */
DisplayStatus finishFrameUpload();

/**
 * @brief Tick the display update timer, and if it reaches 0, decompress the oldest queued frame to the panel.
 * 
 * @details
 * Call regularly from the main loop. Runs on its own interval, see @c
 * display_config::kThreadRefreshIntervalMs, independent of the web server's
 * thread. Ticks @c display_config::kDisplayCooldownMs down toward 0 each
 * time it runs, then, once it reaches 0 and at least one frame is queued,
 * decompresses the oldest queued frame into the backend's plane buffers,
 * flips it to the panel, and restarts the cooldown.
 * 
 * @note
 * This runs on its own thread, independent of the main thread.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateDisplayModule();

/**
 * @brief Human readable message for a @c DisplayStatus value.
 *
 * @param status Status to describe.
 *
 * @return A statically allocated, null terminated description string.
 *
 */
const char* displayStatusMessage(DisplayStatus status);

/** @} */ // end of Public
