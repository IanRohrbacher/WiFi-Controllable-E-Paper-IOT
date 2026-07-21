/**
 * @file display.cpp
 *
 * @brief Implementation of the display module.
 */

#include <string.h>

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

/** @brief The single active hardware backend, addressed through the
 * abstract interface so a different panel backend can be swapped in
 * without touching any of the code below. */
Waveshare352bDriver waveshareDriver;
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

UploadState uploadState = UploadState::Idle;

/** @brief Accumulator for the fixed-size header, filled a few bytes at a time. */
uint8_t headerBytes[sizeof(BitmapHeader)];
size_t headerOffset = 0;

/** @brief Header parsed out of headerBytes once fully received. */
BitmapHeader pendingHeader;

/** @brief Offset within the plane currently being written. */
size_t planeOffset = 0;

/** @brief Active backend's plane size, cached for the duration of one upload. */
size_t expectedPlaneSize = 0;

/** @brief Running (not-yet-finalized) CRC32 over the plane bytes seen so far. */
uint32_t runningCrc = 0xFFFFFFFFu;

/**
 * @brief Update a running CRC32 (IEEE 802.3 polynomial) with new bytes.
 *
 * @details
 * Bit-by-bit implementation (no lookup table) - integer-only and
 * trivially small, at the cost of 8 shifts per byte.
 *
 * @param crc Current running CRC32 state.
 * @param data Bytes to fold into the CRC.
 * @param length Number of bytes at @p data.
 * @return The updated running CRC32 state.
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
 */
DisplayStatus validateHeader(const BitmapHeader& header)
{
    if (header.magic != DISPLAY_BITMAP_MAGIC) return DisplayStatus::InvalidHeader;
    if (header.version != DISPLAY_BITMAP_VERSION) return DisplayStatus::InvalidVersion;
    if (header.encoding != BitmapEncoding::Packed2Bit) return DisplayStatus::InvalidEncoding;
    if (header.width != activeDriver.width() || header.height != activeDriver.height()) return DisplayStatus::InvalidDimensions;
    return DisplayStatus::Success;
}

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
    return activeDriver.present();
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

DisplayStatus beginFrameUpload()
{
    uploadState = UploadState::Header;
    headerOffset = 0;
    planeOffset = 0;
    expectedPlaneSize = activeDriver.planeSize();
    runningCrc = 0xFFFFFFFFu;
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
            uint8_t* const plane = (uploadState == UploadState::BlackPlane)
                ? activeDriver.blackPlane()
                : activeDriver.redPlane();

            const size_t need = expectedPlaneSize - planeOffset;
            const size_t take = (length - consumed < need) ? (length - consumed) : need;

            memcpy(plane + planeOffset, data + consumed, take);
            runningCrc = crc32Update(runningCrc, data + consumed, take);
            planeOffset += take;
            consumed += take;

            if (planeOffset == expectedPlaneSize) {
                if (uploadState == UploadState::BlackPlane) {
                    uploadState = UploadState::RedPlane;
                    planeOffset = 0;
                } else {
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

    debug_logs::displayLogging("Accepted frame upload.");
    return DisplayStatus::Success;
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
        default: return "unknown error";
    }
}
/** @} */ // end of Public
