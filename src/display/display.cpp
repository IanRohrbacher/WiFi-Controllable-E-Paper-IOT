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
#include <stdlib.h>
#include <thread.h>
#include <LittleFS.h>

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

/** @brief Timestamp for adding debug logs for the display loop. */
unsigned long nowLoop = 0;

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

/** @brief Marker written at the end of a fully, successfully written frame file. */
constexpr uint32_t kFrameSlotMagic = 0x53544C31u; // "1LTS", distinct from DISPLAY_BITMAP_MAGIC

/** @brief Fixed trailer written after both compressed planes in a frame file. */
struct FrameSlotTrailer {
    /** @brief Format-version guard, see kFrameSlotMagic. */
    uint32_t magic;
    /** @brief Compressed length, in bytes, of the black-plane segment. */
    uint32_t blackLength;
    /** @brief Compressed length, in bytes, of the red-plane segment. */
    uint32_t redLength;
    /** @brief MAC address of the client that uploaded this frame, see beginFrameUpload(). */
    uint8_t ownerMac[6];
};

/** @brief Sequence number of the oldest queued frame, the next one to update the panel. */
uint32_t queueHeadSequence = 0;
/** @brief Number of frames currently queued. */
uint32_t queueCount = 0;
/** @brief Sequence number the next successfully completed upload will commit as. */
uint32_t nextSequence = 0;

/** @brief MAC address of the client uploading the in-progress frame, set by beginFrameUpload(). */
uint8_t pendingOwnerMac[6] = {};

/** @brief LittleFS handle for the frame file currently being written or read. */
File slotFile;

/** @brief Fixed-size buffer batching compressed bytes before they are flushed to flash. */
constexpr size_t kScratchSize = 256;
/** @brief Staging buffer for bytes being written to slotFile. */
uint8_t writeScratch[kScratchSize];
/** @brief Number of bytes currently held in writeScratch. */
size_t writeScratchOffset = 0;
/** @brief Staging buffer for bytes being read from slotFile. */
uint8_t readScratch[kScratchSize];

/** @brief Total compressed bytes written so far for the in-progress upload. */
size_t compressedOffset = 0;
/** @brief Compressed length of the black-plane segment, set once it finishes. */
size_t pendingBlackLength = 0;
/** @brief Compressed length of the red-plane segment, set once it finishes. */
size_t pendingRedLength = 0;
/** @brief Byte value of the RLE run currently being accumulated. */
uint8_t runByte = 0;
/** @brief Length of the RLE run currently being accumulated, 0 means no run is open. */
uint8_t runLength = 0;

/**
 * @brief Build the LittleFS path for a queued frame's committed file.
 *
 * @param sequence Sequence number to build the path for.
 * @param buffer Destination buffer for the path.
 * @param bufferSize Size, in bytes, of @p buffer.
 *
 * @par Returns
 * Nothing.
 *
 */
void framePath(uint32_t sequence, char* buffer, size_t bufferSize)
{
    snprintf(buffer, bufferSize, "%s%lu.bin", display_config::kFramesDir, static_cast<unsigned long>(sequence));
}

/**
 * @brief Whether LittleFS has enough free space to accept one more worst-case frame.
 *
 * @details
 * A frame's compressed size can never exceed 4 * activeDriver.planeSize()
 * (both planes, each at most 2x their raw size under RLE), plus the trailer.
 * Reserves display_config::kMinFreeFlashBytes on top of that so normal
 * filesystem operation never runs flash to exactly 0 free.
 *
 * @par Parameters
 * None.
 *
 * @return Whether a new upload may be accepted right now.
 * @retval true There is enough free space for one more worst-case frame.
 * @retval false Free space is too low, or LittleFS.info() failed.
 *
 */
bool hasFreeSpaceForFrame()
{
    FSInfo info;
    if (!LittleFS.info(info)) return false;

    const size_t worstCaseFrameSize = 4 * activeDriver.planeSize() + sizeof(FrameSlotTrailer);
    const size_t freeBytes = info.totalBytes - info.usedBytes;
    return freeBytes >= worstCaseFrameSize + display_config::kMinFreeFlashBytes;
}

/**
 * @brief Parse the sequence number out of a committed frame file's name.
 *
 * @param name File name to parse, with or without a directory prefix.
 * @param sequence Destination that receives the parsed sequence number on success.
 *
 * @return Whether name matched the "{sequence}.bin" pattern.
 * @retval true sequence was filled in.
 * @retval false name did not parse as a plain decimal number followed by ".bin".
 *
 */
bool parseFrameFileName(const char* name, uint32_t& sequence)
{
    const char* slash = strrchr(name, '/');
    const char* base = slash ? slash + 1 : name;

    char* end = nullptr;
    const unsigned long value = strtoul(base, &end, 10);
    if (end == base || strcmp(end, ".bin") != 0) return false;

    sequence = static_cast<uint32_t>(value);
    return true;
}

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
 * @brief Flush any bytes currently staged in writeScratch to slotFile.
 *
 * @par Parameters
 * None.
 *
 * @return Whether every staged byte was written successfully.
 * @retval true All staged bytes (if any) were written.
 * @retval false The write to flash did not consume every staged byte.
 *
 */
bool flushWriteScratch()
{
    if (writeScratchOffset == 0) return true;
    const size_t pending = writeScratchOffset;
    const size_t written = slotFile.write(writeScratch, pending);
    writeScratchOffset = 0;
    return written == pending;
}

/**
 * @brief Stage one byte for slotFile, flushing writeScratch first if it is full.
 *
 * @param value Byte to stage.
 *
 * @return Whether the byte was staged successfully.
 * @retval true The byte was staged, flushing writeScratch first if needed.
 * @retval false writeScratch needed to flush and the flash write failed.
 *
 */
bool writeScratchByte(uint8_t value)
{
    if (writeScratchOffset == kScratchSize && !flushWriteScratch()) return false;
    writeScratch[writeScratchOffset++] = value;
    return true;
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
 * @retval false The flash write failed.
 *
 */
bool flushRun()
{
    if (runLength == 0) return true;
    if (!writeScratchByte(runLength) || !writeScratchByte(runByte)) return false;

    compressedOffset += 2;
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
 * @retval false The flash write failed.
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
 * @brief Read and validate the trailer at the end of an open frame file.
 *
 * @details
 * Leaves the file's read position at 0 on success, ready for a caller to
 * stream-decode the planes that precede the trailer.
 *
 * @param file Open, readable frame file.
 * @param trailer Destination that receives the trailer on success.
 *
 * @return Whether a valid trailer was read.
 * @retval true trailer was filled in and file's position is at 0.
 * @retval false The file is too short, unreadable, or its magic did not match.
 *
 */
bool readTrailer(File& file, FrameSlotTrailer& trailer)
{
    if (file.size() < sizeof(trailer)) return false;
    file.seek(file.size() - sizeof(trailer));
    const bool ok = file.read(reinterpret_cast<uint8_t*>(&trailer), sizeof(trailer)) == sizeof(trailer)
        && trailer.magic == kFrameSlotMagic;
    file.seek(0);
    return ok;
}

/**
 * @brief Decompress one RLE-compressed plane by streaming it from an open file.
 *
 * @details
 * Reads compressedLength bytes starting at the file's current position, in
 * chunks of at most kScratchSize bytes. kScratchSize is even and every token
 * is exactly 2 bytes, so a chunk boundary never splits a token, as long as
 * every read returns exactly what was asked for. A short read aborts
 * immediately rather than risk desyncing the token stream and decoding garbage
 * into destination silently. Whether or not this returns true, destination may
 * already hold partially decoded data, callers must not flip() the panel using
 * it unless this returns true.
 *
 * @param file Open, readable frame file positioned at the start of the plane.
 * @param compressedLength Number of compressed bytes to read from file.
 * @param destination Buffer to decompress into, sized to hold the raw plane.
 *
 * @return Whether the entire plane was decoded successfully.
 * @retval true All compressedLength bytes were read and decoded.
 * @retval false A read returned fewer bytes than requested.
 *
 */
bool decodePlaneFromFile(File& file, size_t compressedLength, uint8_t* destination)
{
    size_t remaining = compressedLength;
    size_t o = 0;
    while (remaining > 0) {
        const size_t want = remaining < kScratchSize ? remaining : kScratchSize;
        const size_t got = file.read(readScratch, want);
        if (got != want) return false;
        remaining -= got;

        size_t i = 0;
        while (i < got) {
            const uint8_t length = readScratch[i++];
            const uint8_t value = readScratch[i++];
            memset(destination + o, value, length);
            o += length;
        }
    }
    return true;
}

/**
 * @brief Advance queueHeadSequence past any sequence numbers with no surviving file.
 *
 * @details
 * Only needed after removeQueuedFrameForMac() removes the current head out of
 * turn (an override replacing the oldest queued frame); normal consumption
 * via dropHeadFrame() never creates a gap. Bounded by queueCount: the next
 * surviving file's sequence number is always >= the old head, so this always
 * terminates once it reaches it (or immediately, once the queue is empty).
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void advanceHeadPastGaps()
{
    char path[24];
    while (queueCount > 0) {
        framePath(queueHeadSequence, path, sizeof(path));
        if (LittleFS.exists(path)) return;
        queueHeadSequence++;
    }
}

/**
 * @brief Find the sequence number of the queued frame belonging to the given MAC address, if any.
 *
 * @details
 * Scans every file under display_config::kFramesDir, reading each one's
 * trailer to check its ownerMac. A linear scan is acceptable here since the
 * queue's size is inherently bounded by free flash space, not by a slot count.
 *
 * @param mac Pointer to the 6-byte MAC address to look for.
 * @param sequenceOut Destination that receives the matching frame's sequence number on success.
 *
 * @retval true A queued frame belongs to mac; sequenceOut was filled in.
 * @retval false No queued frame belongs to mac.
 *
 */
bool findQueuedSequenceForMac(const uint8_t* mac, uint32_t& sequenceOut)
{
    Dir dir = LittleFS.openDir(display_config::kFramesDir);
    while (dir.next()) {
        uint32_t sequence;
        if (!parseFrameFileName(dir.fileName().c_str(), sequence)) continue;

        char path[24];
        framePath(sequence, path, sizeof(path));
        File file = LittleFS.open(path, "r");
        if (!file) continue;

        FrameSlotTrailer trailer;
        const bool matches = readTrailer(file, trailer) && memcmp(trailer.ownerMac, mac, 6) == 0;
        file.close();

        if (matches) {
            sequenceOut = sequence;
            return true;
        }
    }
    return false;
}

/**
 * @brief Advance past the oldest queued frame without necessarily having displayed it.
 *
 * @details
 * Used both after a normal successful flip, and when the oldest queued frame's
 * file cannot be opened or its trailer fails to validate, a situation that
 * should not happen in normal operation but must not wedge the queue forever
 * if it does.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void dropHeadFrame()
{
    queueHeadSequence++;
    queueCount--;
    updateRemainingMs = display_config::kDisplayCooldownMs;
}

/**
 * @brief Tick the display update timer, and if it reaches 0, decompress the oldest queued frame to the panel.
 *
 * @details
 * Ticks @c display_config::kDisplayCooldownMs down toward 0, then, once it
 * reaches 0 and at least one frame is queued, decompresses the oldest queued
 * frame's file into the backend's plane buffers, flips it to the panel,
 * deletes the now-consumed file, and restarts the cooldown.
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

    char path[24];
    framePath(queueHeadSequence, path, sizeof(path));
    File file = LittleFS.open(path, "r");

    FrameSlotTrailer trailer;
    if (!file || !readTrailer(file, trailer)) {
        if (file) file.close();
        debug_logs::displayLogging("Failed to read queued frame %s, dropping it", path);
        dropHeadFrame();
        return;
    }

    const bool blackOk = decodePlaneFromFile(file, trailer.blackLength, activeDriver.blackPlane());
    const bool redOk = blackOk && decodePlaneFromFile(file, trailer.redLength, activeDriver.redPlane());
    file.close();
    LittleFS.remove(path);

    dropHeadFrame();

    if (!blackOk || !redOk) {
        debug_logs::displayLogging("Failed to decode queued frame %s, skipping panel update", path);
        return;
    }

    if (activeDriver.flip()) {
        debug_logs::displayLogging("Updated the panel with a queued frame, %u remaining", queueCount);
    } else {
        debug_logs::displayLogging("Queued frame ready but the display driver has not been started");
    }
}

/** @brief Thread for ticking the display update timer. */
Thread displayThread = Thread([]() { displayTick(); });

/**
 * @brief Reconstruct queue state from whatever frame files survived a reboot.
 *
 * @details
 * Ensures @c display_config::kFramesDir exists and discards a stray @c
 * display_config::kUploadTmpPath left behind by an upload that was in progress
 * at the last reboot. Every remaining file is committed only via an atomic
 * rename that happens after its trailer is fully written, so any file matching
 * the "{sequence}.bin" naming pattern is by construction complete, nothing
 * needs to be opened or read to trust it. Scans file names only, to find the
 * lowest and highest sequence number present and how many frames were found,
 * and rebuilds @c queueHeadSequence, @c queueCount, and @c nextSequence from
 * those.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void recoverQueueFromFlash()
{
    LittleFS.mkdir(display_config::kFramesDir);
    LittleFS.remove(display_config::kUploadTmpPath);

    uint32_t minSequence = 0;
    uint32_t maxSequence = 0;
    uint32_t count = 0;

    Dir dir = LittleFS.openDir(display_config::kFramesDir);
    while (dir.next()) {
        uint32_t sequence;
        if (!parseFrameFileName(dir.fileName().c_str(), sequence)) continue;

        if (count == 0 || sequence < minSequence) minSequence = sequence;
        if (count == 0 || sequence > maxSequence) maxSequence = sequence;
        count++;
    }

    queueHeadSequence = minSequence;
    queueCount = count;
    nextSequence = (count > 0) ? maxSequence + 1 : 0;

    if (count > 0) {
        debug_logs::displayLogging("Recovered %u queued frame(s) from flash", count);
    }
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

    displayThread.setInterval(display_config::kThreadRefreshIntervalMs);
    recoverQueueFromFlash();

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

uint16_t getDisplayWidth()
{
    return activeDriver.width();
}

uint16_t getDisplayHeight()
{
    return activeDriver.height();
}

DisplayStatus getDisplayQueueStatus()
{
    return hasFreeSpaceForFrame() ? DisplayStatus::Success : DisplayStatus::Busy;
}

bool isDisplayQueueEmpty()
{
    return queueCount == 0;
}

uint32_t getDisplayQueueCount()
{
    return queueCount;
}

bool displayQueueHasFrameForMac(const uint8_t* mac)
{
    uint32_t sequence;
    return findQueuedSequenceForMac(mac, sequence);
}

bool removeQueuedFrameForMac(const uint8_t* mac)
{
    uint32_t sequence;
    if (!findQueuedSequenceForMac(mac, sequence)) return false;

    char path[24];
    framePath(sequence, path, sizeof(path));
    LittleFS.remove(path);
    queueCount--;
    if (sequence == queueHeadSequence) advanceHeadPastGaps();

    debug_logs::displayLogging("Removed queued frame %s to allow an override", path);
    return true;
}

unsigned long getDisplayNextUpdateMs()
{
    const unsigned long elapsed = millis() - updateLastTickMs;
    return elapsed >= updateRemainingMs ? 0 : updateRemainingMs - elapsed;
}

void setNextUpdateCooldownMs(unsigned long cooldownMs)
{
    updateRemainingMs = cooldownMs;
    updateLastTickMs = millis();
}

DisplayStatus beginFrameUpload(const uint8_t* ownerMac)
{
    if (!hasFreeSpaceForFrame()) {
        debug_logs::displayLogging("Rejected frame upload: not enough free flash space");
        return DisplayStatus::Busy;
    }

    slotFile = LittleFS.open(display_config::kUploadTmpPath, "w");
    if (!slotFile) {
        debug_logs::displayLogging("Rejected frame upload: failed to open %s for writing", display_config::kUploadTmpPath);
        return DisplayStatus::HardwareFailure;
    }

    memcpy(pendingOwnerMac, ownerMac, 6);
    uploadState = UploadState::Header;
    headerOffset = 0;
    planeOffset = 0;
    expectedPlaneSize = activeDriver.planeSize();
    runningCrc = 0xFFFFFFFFu;
    compressedOffset = 0;
    writeScratchOffset = 0;
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
                    debug_logs::displayLogging("Rejected frame upload: failed writing compressed data to flash");
                    return DisplayStatus::HardwareFailure;
                }
            }

            if (planeOffset == expectedPlaneSize) {
                if (!flushRun()) {
                    uploadState = UploadState::Failed;
                    debug_logs::displayLogging("Rejected frame upload: failed writing compressed data to flash");
                    return DisplayStatus::HardwareFailure;
                }

                if (uploadState == UploadState::BlackPlane) {
                    pendingBlackLength = compressedOffset;
                    uploadState = UploadState::RedPlane;
                    planeOffset = 0;
                } else {
                    pendingRedLength = compressedOffset - pendingBlackLength;
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
    const UploadState finishedState = uploadState;
    uploadState = UploadState::Idle;

    if (finishedState == UploadState::Idle) {
        // No upload was in progress, nothing was opened this round to clean up.
        // Most likely beginFrameUpload() was skipped for being busy; report
        // that instead of InvalidDimensions if free space is still tight.
        return hasFreeSpaceForFrame() ? DisplayStatus::InvalidDimensions : DisplayStatus::Busy;
    }

    if (finishedState != UploadState::Complete) {
        if (slotFile) slotFile.close();
        LittleFS.remove(display_config::kUploadTmpPath);
        return DisplayStatus::InvalidDimensions;
    }

    const uint32_t finalCrc = runningCrc ^ 0xFFFFFFFFu;

    if (finalCrc != pendingHeader.crc32) {
        debug_logs::displayLogging("Rejected frame upload: CRC32 mismatch");
        slotFile.close();
        LittleFS.remove(display_config::kUploadTmpPath);
        return DisplayStatus::InvalidChecksum;
    }

    FrameSlotTrailer trailer{};
    trailer.magic = kFrameSlotMagic;
    trailer.blackLength = static_cast<uint32_t>(pendingBlackLength);
    trailer.redLength = static_cast<uint32_t>(pendingRedLength);
    memcpy(trailer.ownerMac, pendingOwnerMac, 6);
    flushWriteScratch();
    slotFile.write(reinterpret_cast<const uint8_t*>(&trailer), sizeof(trailer));
    slotFile.close();

    char finalPath[24];
    framePath(nextSequence, finalPath, sizeof(finalPath));
    if (!LittleFS.rename(display_config::kUploadTmpPath, finalPath)) {
        debug_logs::displayLogging("Rejected frame upload: failed to commit %s", finalPath);
        LittleFS.remove(display_config::kUploadTmpPath);
        return DisplayStatus::HardwareFailure;
    }

    if (queueCount == 0) queueHeadSequence = nextSequence;
    nextSequence++;
    queueCount++;
    debug_logs::displayLogging("Queued frame upload, %u frame(s) now queued", queueCount);
    return DisplayStatus::Success;
}

void abortFrameUpload()
{
    if (uploadState == UploadState::Idle) return;

    uploadState = UploadState::Idle;
    if (slotFile) slotFile.close();
    LittleFS.remove(display_config::kUploadTmpPath);
}

void clearFrameQueue()
{
    abortFrameUpload();

    Dir dir = LittleFS.openDir(display_config::kFramesDir);
    while (dir.next()) {
        char path[24];
        snprintf(path, sizeof(path), "%s%s", display_config::kFramesDir, dir.fileName().c_str());
        LittleFS.remove(path);
    }

    queueHeadSequence = 0;
    queueCount = 0;
    nextSequence = 0;

    debug_logs::displayLogging("Cleared the frame queue");
}

void updateDisplayModule()
{
    if (displayThread.shouldRun()) displayThread.run();

    if (millis() - nowLoop >= debug_config::kDisplayLoopDelay) {
        debug_logs::displayLogging("Queue: %u frame(s) queued, next update in %lu ms", queueCount, getDisplayNextUpdateMs());
        nowLoop = millis();
    }
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
        case DisplayStatus::Busy: return "not enough free flash space to queue a new frame";
        default: return "unknown error";
    }
}
/** @} */ // end of Public
