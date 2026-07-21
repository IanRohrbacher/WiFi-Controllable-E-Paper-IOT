/**
 * @file display_protocol.h
 *
 * @brief Wire format and result codes for the display module.
 *
 * @details
 * This defines the bitmap upload format (magic/version/encoding/header) and
 * the status codes returned by the display module - the contract shared by
 * the browser, @c display.cpp, and any hardware backend, regardless of which
 * panel is attached.
 * 
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Current bitmap format version.
 */
constexpr uint8_t DISPLAY_BITMAP_VERSION = 1;

/**
 * @brief Magic number identifying a display bitmap.
 *
 * ASCII:
 *      'E'
 *      'P'
 */
constexpr uint16_t DISPLAY_BITMAP_MAGIC = 0x4550;

/**
 * @brief Bitmap encoding formats.
 */
enum class BitmapEncoding : uint8_t
{
    Packed2Bit = 0
};

/**
 * @brief Result codes returned by the display module.
 */
enum class DisplayStatus : uint8_t
{
    Success,
    InvalidArgument,
    InvalidHeader,
    InvalidDimensions,
    InvalidEncoding,
    InvalidVersion,
    BufferTooSmall,
    NotInitialized,
    HardwareFailure,
    InvalidChecksum
};

/**
 * @brief Header found at the beginning of every uploaded bitmap.
 *
 * Pixel data immediately follows this structure.
 */
struct BitmapHeader
{
    uint16_t magic;
    uint8_t version;
    BitmapEncoding encoding;
    uint16_t width;
    uint16_t height;
    uint32_t crc32;
};

static_assert(sizeof(BitmapHeader) == 12, "Unexpected BitmapHeader size.");
