/**
 * @file display.cpp
 *
 * @brief Implementation of the display module.
 *
 * @details
 * Owns the single active @c IDisplayBWRDriver backend, the streaming frame
 * upload parser (header, black plane, red plane), the compressed frame queue,
 * and the queue update timer. The actual panel update runs on its own
 * cooperative thread, ticked independently of the web server's thread, see
 * @c updateDisplayModule().
 *
 */

#include <string.h>
#include <thread.h>

#include "display.h"
#include "backend/waveshare_352b.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the display module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {

/** @brief The single active hardware backend. */
Waveshare352bDriver waveshareDriver;
/** @brief View of waveshareDriver through the abstract interface, so a different backend can be swapped in without touching the code below. */
IDisplayBWRDriver& activeDriver = waveshareDriver;

/** @brief States of the streaming frame-upload parser. */
enum class UploadState : uint8_t {
    Idle,
    Header,
    BlackPlane,
    RedPlane,
    Complete,
    Failed
};

/** @brief Current stage of the streaming frame upload parser. */
UploadState uploadState = UploadState::Idle;

/** @brief Accumulator for the fixed-size header, filled a few bytes at a time. */
uint8_t headerBytes[sizeof(BitmapHeader)];
/** @brief Number of bytes already written into headerBytes. */
size_t headerOffset = 0;

/** @brief Header parsed out of headerBytes once fully received. */
BitmapHeader pendingHeader;

/** @brief Raw offset within the plane currently being received. */
size_t planeOffset = 0;

/** @brief Active backend's plane size, cached for the duration of one upload. */
size_t expectedPlaneSize = 0;

/** @brief Running (not-yet-finalized) CRC32 over the raw plane bytes seen so far. */
uint32_t runningCrc = 0xFFFFFFFFu;

/** @brief One compressed frame used for queuing. */
struct QueuedFrame {
    /** @brief Whether this slot holds a queued frame. */
    bool inUse;
    /** @brief RLE-compressed black plane immediately followed by the RLE-compressed red plane. */
    uint8_t compressed[display_config::kDisplayQueueSlotCapacity];
    /** @brief Compressed length, in bytes, of the black-plane segment. */
    size_t blackLength;
    /** @brief Compressed length, in bytes, of the red-plane segment. */
    size_t redLength;
};

/** @brief Ring buffer of frames waiting to update the panel. */
QueuedFrame frameQueue[display_config::kDisplayQueueSlots] = {};
/** @brief Index of the oldest queued frame, the next one to update the panel. */
uint8_t queueHead = 0;
/** @brief Number of frames currently queued. */
uint8_t queueCount = 0;
/** @brief Index of the slot reserved for the upload currently in progress. */
uint8_t uploadSlotIndex = 0;

/** @brief Write offset within the in-progress upload's reserved compressed buffer. */
size_t compressedOffset = 0;
/** @brief Byte value of the RLE run currently being accumulated. */
uint8_t runByte = 0;
/** @brief Length of the RLE run currently being accumulated, 0 means no run is open. */
uint8_t runLength = 0;

/** @brief Milliseconds remaining before the panel may be updated, held at 0 when idle. */
unsigned long updateRemainingMs = 0;
/** @brief millis() timestamp updateRemainingMs was last ticked from. */
unsigned long updateLastTickMs = 0;

/**
 * @brief Update a running CRC32 (IEEE 802.3 polynomial) with new bytes.
 *
 * @details
 * Bit by bit implementation, no lookup table, integer only and trivially
 * small, at the cost of 8 shifts per byte.
 *
 * @param crc Current running CRC32 state.
 * @param data Bytes to fold into the CRC.
 * @param length Number of bytes at @p data.
 *
 * @return The updated running CRC32 state.
 *
 */
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

/**
 * @brief Validate a parsed header against the format this module accepts.
 *
 * @param header Header to validate, already copied out of headerBytes.
 *
 * @return Whether header is acceptable.
 * @retval DisplayStatus::Success The header is acceptable.
 * @retval DisplayStatus::InvalidHeader The magic number did not match.
 * @retval DisplayStatus::InvalidVersion The format version is not supported.
 * @retval DisplayStatus::InvalidEncoding The encoding is not supported.
 * @retval DisplayStatus::InvalidDimensions The width or height does not match the active backend's panel.
 *
 */
DisplayStatus validateHeader(const BitmapHeader& header)
{
    if (header.magic != DISPLAY_BITMAP_MAGIC) return DisplayStatus::InvalidHeader;
    if (header.version != DISPLAY_BITMAP_VERSION) return DisplayStatus::InvalidVersion;
    if (header.encoding != BitmapEncoding::Packed2Bit) return DisplayStatus::InvalidEncoding;
    if (header.width != activeDriver.width() || header.height != activeDriver.height()) return DisplayStatus::InvalidDimensions;
    return DisplayStatus::Success;
}

/**
 * @brief Emit the RLE run currently being accumulated as one compressed token.
 *
 * @details
 * A token is two bytes, a run length from 1 to 255 followed by the repeated
 * byte value. Does nothing if no run is open.
 *
 * @par Parameters
 * None.
 *
 * @return Whether the token fit, or there was nothing to flush.
 * @retval true The pending run was written, or there was no pending run.
 * @retval false The token would overflow the reserved slot's compressed buffer.
 *
 */
bool flushRun()
{
    if (runLength == 0) return true;
    if (compressedOffset + 2 > display_config::kDisplayQueueSlotCapacity) return false;

    frameQueue[uploadSlotIndex].compressed[compressedOffset++] = runLength;
    frameQueue[uploadSlotIndex].compressed[compressedOffset++] = runByte;
    runLength = 0;
    return true;
}

/**
 * @brief Fold one raw plane byte into the streaming RLE encoder.
 *
 * @param value Raw byte to encode.
 *
 * @return Whether the byte was accepted.
 * @retval true The byte extended the current run or started a new one.
 * @retval false The reserved slot's compressed buffer is full.
 *
 */
bool encodeByte(uint8_t value)
{
    if (runLength > 0 && value == runByte && runLength < 255) {
        runLength++;
        return true;
    }
    if (!flushRun()) return false;
    runByte = value;
    runLength = 1;
    return true;
}

/**
 * @brief Decompress one RLE-compressed plane directly into a destination buffer.
 *
 * @param compressed Pointer to the compressed token stream.
 * @param compressedLength Number of compressed bytes at @p compressed.
 * @param destination Buffer to decompress into, sized to hold the raw plane.
 *
 * @par Returns
 * Nothing.
 *
 */
void decodePlane(const uint8_t* compressed, size_t compressedLength, uint8_t* destination)
{
    size_t i = 0;
    size_t o = 0;
    while (i < compressedLength) {
        const uint8_t length = compressed[i++];
        const uint8_t value = compressed[i++];
        memset(destination + o, value, length);
        o += length;
    }
}

/**
 * @brief Tick the display update timer, and if it reaches 0, decompress the oldest queued frame to the panel.
 *
 * @details
 * Ticks @c display_config::kDisplayCooldownMs down toward 0, then, once it
 * reaches 0 and at least one frame is queued, decompresses the oldest queued
 * frame into the backend's plane buffers, flips it to the panel, and restarts
 * the cooldown.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void displayTick()
{
    const unsigned long now = millis();
    const unsigned long elapsed = now - updateLastTickMs;
    updateLastTickMs = now;

    updateRemainingMs = elapsed >= updateRemainingMs ? 0 : updateRemainingMs - elapsed;

    if (updateRemainingMs > 0 || queueCount == 0) return;

    QueuedFrame& frame = frameQueue[queueHead];
    decodePlane(frame.compressed, frame.blackLength, activeDriver.blackPlane());
    decodePlane(frame.compressed + frame.blackLength, frame.redLength, activeDriver.redPlane());
    frame.inUse = false;
    queueHead = (queueHead + 1) % display_config::kDisplayQueueSlots;
    queueCount--;

    if (activeDriver.flip()) {
        debug_logs::displayLogging("Updated the panel with a queued frame, %u remaining", queueCount);
    } else {
        debug_logs::displayLogging("Queued frame ready but the display driver has not been started");
    }
    updateRemainingMs = display_config::kDisplayCooldownMs;
}

/** @brief Thread for ticking the display update timer. */
Thread displayThread = Thread([]() { displayTick(); });

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the display module, declared in display.h.
 * @{
 */
bool startDisplayModule(bool clearScreen)
{
    uint8_t attempts = 0;
    while (!activeDriver.begin() && attempts < display_config::kDriverInitAttempts) {
        debug_logs::displayLogging("Failed to initialize display driver, attempt %d", ++attempts);
        delay(display_config::kDriverInitIntervalMs);
    }
    if (attempts == display_config::kDriverInitAttempts) {
        debug_logs::displayLogging("Failed to initialize display driver after %d attempts, giving up", display_config::kDriverInitAttempts);
        return false;
    }

    displayThread.setInterval(display_config::kThreadRefreshIntervalMs);

    if (clearScreen) return clearDisplay(DisplayColor::White);
    return true;
}

bool stopDisplayModule()
{
    activeDriver.end();
    return true;
}

bool refreshDisplay()
{
    return activeDriver.flip();
}

bool clearDisplay(DisplayColor color)
{
    activeDriver.clear(color);
    return refreshDisplay();
}

uint16_t displayWidth()
{
    return activeDriver.width();
}

uint16_t displayHeight()
{
    return activeDriver.height();
}

DisplayStatus displayQueueStatus()
{
    return (queueCount >= display_config::kDisplayQueueSlots) ? DisplayStatus::Busy : DisplayStatus::Success;
}

DisplayStatus beginFrameUpload()
{
    if (queueCount >= display_config::kDisplayQueueSlots) {
        debug_logs::displayLogging("Rejected frame upload: queue is full");
        return DisplayStatus::Busy;
    }

    uploadSlotIndex = (queueHead + queueCount) % display_config::kDisplayQueueSlots;
    uploadState = UploadState::Header;
    headerOffset = 0;
    planeOffset = 0;
    expectedPlaneSize = activeDriver.planeSize();
    runningCrc = 0xFFFFFFFFu;
    compressedOffset = 0;
    runLength = 0;
    return DisplayStatus::Success;
}

DisplayStatus writeFrameChunk(const uint8_t* data, size_t length)
{
    if (uploadState == UploadState::Idle || uploadState == UploadState::Failed) {
        return DisplayStatus::NotInitialized;
    }

    size_t consumed = 0;
    while (consumed < length) {
        if (uploadState == UploadState::Header) {
            const size_t need = sizeof(BitmapHeader) - headerOffset;
            const size_t take = (length - consumed < need) ? (length - consumed) : need;

            memcpy(headerBytes + headerOffset, data + consumed, take);
            headerOffset += take;
            consumed += take;

            if (headerOffset == sizeof(BitmapHeader)) {
                memcpy(&pendingHeader, headerBytes, sizeof(BitmapHeader));
                const DisplayStatus status = validateHeader(pendingHeader);
                if (status != DisplayStatus::Success) {
                    uploadState = UploadState::Failed;
                    debug_logs::displayLogging("Rejected frame upload: %s", displayStatusMessage(status));
                    return status;
                }
                uploadState = UploadState::BlackPlane;
                planeOffset = 0;
            }
        } else if (uploadState == UploadState::BlackPlane || uploadState == UploadState::RedPlane) {
            for (; consumed < length && planeOffset < expectedPlaneSize; ++consumed, ++planeOffset) {
                const uint8_t rawByte = data[consumed];
                runningCrc = crc32Update(runningCrc, &rawByte, 1);
                if (!encodeByte(rawByte)) {
                    uploadState = UploadState::Failed;
                    debug_logs::displayLogging("Rejected frame upload: image too complex to compress");
                    return DisplayStatus::BufferTooSmall;
                }
            }

            if (planeOffset == expectedPlaneSize) {
                if (!flushRun()) {
                    uploadState = UploadState::Failed;
                    debug_logs::displayLogging("Rejected frame upload: image too complex to compress");
                    return DisplayStatus::BufferTooSmall;
                }

                if (uploadState == UploadState::BlackPlane) {
                    frameQueue[uploadSlotIndex].blackLength = compressedOffset;
                    uploadState = UploadState::RedPlane;
                    planeOffset = 0;
                } else {
                    frameQueue[uploadSlotIndex].redLength = compressedOffset - frameQueue[uploadSlotIndex].blackLength;
                    uploadState = UploadState::Complete;
                }
            }
        } else {
            // Complete (or otherwise not accepting bytes) but more arrived.
            uploadState = UploadState::Failed;
            debug_logs::displayLogging("Rejected frame upload: unexpected trailing bytes");
            return DisplayStatus::BufferTooSmall;
        }
    }

    return DisplayStatus::Success;
}

DisplayStatus finishFrameUpload()
{
    if (uploadState != UploadState::Complete) {
        uploadState = UploadState::Idle;
        return DisplayStatus::InvalidDimensions;
    }

    const uint32_t finalCrc = runningCrc ^ 0xFFFFFFFFu;
    uploadState = UploadState::Idle;

    if (finalCrc != pendingHeader.crc32) {
        debug_logs::displayLogging("Rejected frame upload: CRC32 mismatch");
        return DisplayStatus::InvalidChecksum;
    }

    frameQueue[uploadSlotIndex].inUse = true;
    queueCount++;
    debug_logs::displayLogging("Queued frame upload, %u of %u slots now used", queueCount, display_config::kDisplayQueueSlots);
    return DisplayStatus::Success;
}

void updateDisplayModule()
{
    if (displayThread.shouldRun()) displayThread.run();
}

const char* displayStatusMessage(DisplayStatus status)
{
    switch (status) {
        case DisplayStatus::Success: return "success";
        case DisplayStatus::InvalidArgument: return "invalid argument";
        case DisplayStatus::InvalidHeader: return "invalid bitmap header";
        case DisplayStatus::InvalidDimensions: return "frame dimensions do not match the panel";
        case DisplayStatus::InvalidEncoding: return "unsupported bitmap encoding";
        case DisplayStatus::InvalidVersion: return "unsupported bitmap version";
        case DisplayStatus::BufferTooSmall: return "frame data too large";
        case DisplayStatus::NotInitialized: return "upload was not started";
        case DisplayStatus::HardwareFailure: return "display hardware failure";
        case DisplayStatus::InvalidChecksum: return "frame checksum mismatch";
        case DisplayStatus::Busy: return "display queue is full";
        default: return "unknown error";
    }
}
/** @} */ // end of Public
