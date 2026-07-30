/**
 * @file display_protocol.h
 * @headerfile display_protocol.h "src/display/display_protocol.h"
 *
 * @brief Wire format and result codes for the display module.
 *
 * @details
 * This defines the bitmap upload format (magic, version, encoding, header) and
 * the status codes returned by the display module. It is the contract shared
 * by the browser, @c display.cpp, and any hardware backend, regardless of
 * which panel is attached.
 *
 */

#pragma once

#include <Arduino.h>

/** @brief Current bitmap format version. */
constexpr uint8_t DISPLAY_BITMAP_VERSION = 1;

/** @brief Magic number identifying a display bitmap. */
constexpr uint16_t DISPLAY_BITMAP_MAGIC = 0x4550;

/** @brief Bitmap encoding formats. */
enum class BitmapEncoding : uint8_t
{
    /** @brief Two 1bpp planes (black and red), MSB first, no compression. */
    Packed2Bit = 0
};

/** @brief Result codes returned by the display module. */
enum class DisplayStatus : uint8_t
{
    /** @brief The operation completed successfully. */
    Success,
    /** @brief A caller supplied argument was invalid. */
    InvalidArgument,
    /** @brief The bitmap header's magic number did not match. */
    InvalidHeader,
    /** @brief The bitmap header's width or height does not match the panel. */
    InvalidDimensions,
    /** @brief The bitmap header's encoding is not supported. */
    InvalidEncoding,
    /** @brief The bitmap header's format version is not supported. */
    InvalidVersion,
    /** @brief More bytes were received than the frame requires. */
    BufferTooSmall,
    /** @brief The upload was not started before data arrived. */
    NotInitialized,
    /** @brief The display hardware failed to respond. */
    HardwareFailure,
    /** @brief The received CRC32 did not match the header's crc32 field. */
    InvalidChecksum,
    /** @brief Not enough free flash space to queue this frame, it was rejected outright. */
    Busy
};

/**
 * @brief Header found at the beginning of every uploaded bitmap.
 *
 * @details
 * Pixel data immediately follows this structure, as a black plane followed by
 * a red plane, each sized rowBytes multiplied by height.
 */
struct BitmapHeader
{
    /** @brief Must equal @c DISPLAY_BITMAP_MAGIC. */
    uint16_t magic;
    /** @brief Must equal @c DISPLAY_BITMAP_VERSION. */
    uint8_t version;
    /** @brief Encoding used for the pixel data that follows. */
    BitmapEncoding encoding;
    /** @brief Bitmap width in pixels, must match the panel's native width. */
    uint16_t width;
    /** @brief Bitmap height in pixels, must match the panel's native height. */
    uint16_t height;
    /** @brief CRC32 (IEEE 802.3) over the black plane then the red plane. */
    uint32_t crc32;
};

static_assert(sizeof(BitmapHeader) == 12, "Unexpected BitmapHeader size.");
