/**
 * @headerfile display.h "src/display/display.h"
 *
 * @details
 * An uploaded frame is RLE-compressed straight into a LittleFS-backed queue as
 * it streams in, one file per frame under @c display_config::kFramesDir, named
 * by a monotonic sequence number, and @c updateDisplayModule() uploads the
 * next queued frame. A new upload is accepted whenever there is enough free
 * flash space left for one worst-case frame, see @c displayQueueStatus().
 * During the next power cycle the queue is recovered from flash the next time
 * @c startDisplayModule() runs. This queue is then updated at a controlled
 * rate by @c display_config::kDisplayCooldownMs to showcase the current frame
 * for a minimum duration before moving on to the next one.
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
 * display_config::kDriverInitAttempts times before giving up. Also recovers
 * the frame queue from @c display_config::kFramesDir, restoring whatever
 * frames were still queued before the last power cycle.
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
 * @c website.cpp's @c /api/display/frame handler. Room means enough free
 * LittleFS space for one worst-case compressed frame.
 *
 * @par Parameters
 * None.
 *
 * @return Whether a new frame may be queued right now.
 * @retval DisplayStatus::Success There is enough free flash space.
 * @retval DisplayStatus::Busy Not enough free flash space remains.
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
 * @brief Check whether the given client already has a frame queued.
 *
 * @details
 * Each queued frame's trailer records the MAC address it was uploaded by
 * (see @c beginFrameUpload()), so this scans the queue's file names, checking
 * each frame file's trailer for a match. Used by @c website.cpp to decide
 * whether a client's submission needs to be confirmed as an override of their
 * own previous frame, rather than an addition to the queue.
 *
 * @param mac Pointer to the 6-byte MAC address to look for.
 *
 * @retval true A queued frame belongs to mac.
 * @retval false No queued frame belongs to mac.
 *
 * @see removeQueuedFrameForMac()
 *
 */
bool displayQueueHasFrameForMac(const uint8_t* mac);

/**
 * @brief Remove the given client's currently queued frame, if any.
 *
 * @details
 * Frees the flash space for a given client's mac immediately. If the removed
 * frame was the queue head, the head is advanced to the next surviving
 * sequence number (or left pointing past the end if the queue is now empty).
 *
 * @param mac Pointer to the 6-byte MAC address whose queued frame should be removed.
 *
 * @retval true A queued frame belonging to mac was found and removed.
 * @retval false No queued frame belongs to mac; nothing was changed.
 *
 * @see displayQueueHasFrameForMac()
 *
 */
bool removeQueuedFrameForMac(const uint8_t* mac);

/**
 * @brief Milliseconds until the panel is next eligible to update.
 *
 * @details
 * A read only peek at the same cooldown timer @c updateDisplayModule()
 * ticks, useful for telling a caller rejected by @c displayQueueStatus()
 * roughly how long until flash space frees up, since a frame being consumed
 * and the panel updating happen together.
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
 * Resets the streaming parser's internal state and opens a staging file at
 * @c display_config::kUploadTmpPath for it. Must be called once before the
 * first call to @c writeFrameChunk() for a given upload. @p ownerMac is
 * recorded and written into the committed frame's trailer by @c
 * finishFrameUpload(), see @c displayQueueHasFrameForMac().
 *
 * @param ownerMac Pointer to the 6-byte MAC address of the uploading client.
 *
 * @see displayQueueStatus()
 *
 * @return Whether a staging file was opened for this upload.
 * @retval DisplayStatus::Success The upload may proceed.
 * @retval DisplayStatus::Busy Not enough free flash space remains, this upload was rejected.
 *
 */
DisplayStatus beginFrameUpload(const uint8_t* ownerMac);

/**
 * @brief Feed the next chunk of an in-progress frame upload.
 *
 * @details
 * The first @c sizeof(BitmapHeader) bytes received are parsed and validated as
 * a @c BitmapHeader. Every byte after that is RLE-compressed on the fly and
 * written to the upload's staging file. A running CRC32 is accumulated over
 * the raw plane bytes for later verification in @c finishFrameUpload().
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
 * @retval DisplayStatus::HardwareFailure Writing the compressed data to
 * flash failed.
 *
 */
DisplayStatus writeFrameChunk(const uint8_t* data, size_t length);

/**
 * @brief Finalize an in-progress frame upload.
 *
 * @details
 * Verifies that a complete frame was received and that its CRC32 matches the
 * header, then commits the staging file into the queue by renaming it to its
 * final, sequence-numbered path. @c updateDisplayModule() pushes it to the
 * panel on a later tick, once its turn comes up.
 *
 * @par Parameters
 * None.
 *
 * @return Whether a complete, verified frame was received and queued.
 * @retval DisplayStatus::Success The frame was received, verified, and queued.
 * @retval DisplayStatus::Busy @c beginFrameUpload() was never called for this
 * upload (most likely rejected as busy at @c UPLOAD_FILE_START) and free
 * flash space is still tight.
 * @retval DisplayStatus::InvalidDimensions Fewer bytes were received
 * than the frame requires, or no upload was in progress and space is fine.
 * @retval DisplayStatus::InvalidChecksum The received CRC32 did not
 * match the header's crc32 field.
 * @retval DisplayStatus::HardwareFailure Committing the staging file failed.
 *
 */
DisplayStatus finishFrameUpload();

/**
 * @brief Discard an in-progress or completed-but-uncommitted frame upload.
 *
 * @details
 * Closes the upload's staging file and deletes @c
 * display_config::kUploadTmpPath without committing it to the queue,
 * regardless of whether a complete, valid frame had already been received. For
 * use when an upload must be abandoned for a reason unrelated to the frame's
 * own validity, for example the uploading client became blocked while its
 * frame was still streaming in. Does nothing if no upload is in progress.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void abortFrameUpload();

/**
 * @brief Delete every file in @c display_config::kFramesDir.
 *
 * @details
 * Aborts an in-progress upload if one exists, then removes every file found
 * under @c display_config::kFramesDir and resets the queue to empty.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void clearFrameQueue();

/**
 * @brief Tick the display update timer, and if it reaches 0, decompress the oldest queued frame to the panel.
 * 
 * @details
 * Call regularly from the main loop. Runs on its own interval, see @c
 * display_config::kThreadRefreshIntervalMs, independent of the web server's
 * thread. Ticks @c display_config::kDisplayCooldownMs down toward 0 each
 * time it runs, then, once it reaches 0 and at least one frame is queued,
 * streams the oldest queued frame's file from flash, decompressing it into
 * the backend's plane buffers, flips it to the panel, deletes the
 * now-consumed file, and restarts the cooldown. Also logs the current queue
 * depth and time to next update every @c debug_config::kDisplayLoopDelay.
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
