/**
 * @file display_handler.cpp
 * 
 * @brief Implementation of the display service for the Waveshare 3.52" e-paper module.
 * 
 * @details
 * This module provides a high-level interface for managing the e-paper
 * display, including initialization, drawing operations, and display
 * updates.
 * 
 */

#include <EPD_3in52b.h>

#include "display_handler.h"
#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the display driver.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {

/** @brief Width of the frame buffer in bytes. */
constexpr UWORD kBufferWidthBytes = (EPD_3IN52B_WIDTH % 8 == 0) ? (EPD_3IN52B_WIDTH / 8) : (EPD_3IN52B_WIDTH / 8 + 1);
/** @brief Total size of the frame buffer in bytes. */
constexpr size_t kBufferSize = static_cast<size_t>(kBufferWidthBytes) * EPD_3IN52B_HEIGHT;

struct DisplayBuffer {
	UBYTE* black = nullptr;
	UBYTE* red = nullptr;

	bool isValid() const {
		return black != nullptr && red != nullptr;
	}

	bool validate() {
		if (!isValid()) {
			freeBuffers();
			black = static_cast<UBYTE*>(malloc(kBufferSize));
			red = static_cast<UBYTE*>(malloc(kBufferSize));
		}
		return isValid();
	}

	void initCanvas() {
		if (!isValid()) return;

		Paint_NewImage(black, EPD_3IN52B_WIDTH, EPD_3IN52B_HEIGHT, display_config::displayRotate, WHITE);
		Paint_SelectImage(black);
		Paint_Clear(WHITE);

		Paint_NewImage(red, EPD_3IN52B_WIDTH, EPD_3IN52B_HEIGHT, display_config::displayRotate, WHITE);
		Paint_SelectImage(red);
		Paint_Clear(WHITE);
	}

	void freeBuffers() {
		if (black) {
			free(black);
			black = nullptr;
		}
		if (red) {
			free(red);
			red = nullptr;
		}
	}

	void clearBuffer(DisplayTone tone = DisplayTone::White) {
		if (!isValid()) return;

		switch (tone) {
			case DisplayTone::White:
				memset(black, WHITE, kBufferSize);
				memset(red, WHITE, kBufferSize);
				break;
			case DisplayTone::Black:
				memset(black, BLACK, kBufferSize);
				memset(red, WHITE, kBufferSize);
				break;
			case DisplayTone::Red:
				memset(black, WHITE, kBufferSize);
				memset(red, BLACK, kBufferSize);
				break;
		}
	}

	void flip() const {
		EPD_3IN52B_Display(black, red);
	}

	void print() const {
		debug_logs::displayLogging("Black Buffer: %p, Red Buffer: %p", black, red);
	}
};

DisplayBuffer displayBuffer;
bool displayStarted = false;

UBYTE* layerBuffer(DisplayTone tone) {
	if (tone == DisplayTone::Black) return displayBuffer.black;
	if (tone == DisplayTone::Red) return displayBuffer.red;
	return nullptr;
}

UBYTE toneToPaint(DisplayTone tone) {
	return tone == DisplayTone::White ? WHITE : BLACK;
}

DOT_PIXEL toDotPixel(int8_t lineWidth) {
	if (lineWidth < 1) lineWidth = 1;
	if (lineWidth > 8) lineWidth = 8;
	return static_cast<DOT_PIXEL>(lineWidth);
}

DRAW_FILL toDrawFill(bool fill) {
	return fill ? DRAW_FILL_FULL : DRAW_FILL_EMPTY;
}

DisplayRectangle textBounds(const DisplayText& text) {
	DisplayRectangle rectangle;
	rectangle.x = text.x;
	rectangle.y = text.y;
	rectangle.tone = text.textColor;
	rectangle.lineWidth = 1;
	rectangle.fill = true;

	if (text.text == nullptr || text.font == nullptr) return rectangle;

	const UWORD fontWidth = text.font->Width;
	const UWORD fontHeight = text.font->Height;
	UWORD cursorX = static_cast<UWORD>(text.x);
	UWORD cursorY = static_cast<UWORD>(text.y);
	UWORD maxX = cursorX;
	UWORD maxY = cursorY;

	for (const char* character = text.text; *character != '\0'; ++character) {
		if ((cursorX + fontWidth) > Paint.Width) {
			cursorX = static_cast<UWORD>(text.x);
			cursorY += fontHeight;
		}
		if ((cursorY + fontHeight) > Paint.Height) {
			cursorX = static_cast<UWORD>(text.x);
			cursorY = static_cast<UWORD>(text.y);
		}
		if ((cursorX + fontWidth) > maxX) maxX = cursorX + fontWidth;
		if ((cursorY + fontHeight) > maxY) maxY = cursorY + fontHeight;
		cursorX += fontWidth;
	}

	if (maxX > text.x) rectangle.width = static_cast<UWORD>(maxX - text.x);
	if (maxY > text.y) rectangle.height = static_cast<UWORD>(maxY - text.y);

	return rectangle;
}

void clearTextRegion(UBYTE* buffer, const DisplayRectangle& rectangle) {
	if (buffer == nullptr || !rectangle.isValid()) return;

	Paint_SelectImage(buffer);
	Paint_ClearWindows(rectangle.x, rectangle.y,
		rectangle.x + rectangle.width - 1,
		rectangle.y + rectangle.height - 1,
		WHITE);
}

void clearRectangleRegion(UBYTE* buffer, const DisplayRectangle& rectangle) {
	if (buffer == nullptr || !rectangle.isValid()) return;

	const UWORD xEnd = rectangle.width == 0 ? rectangle.x : static_cast<UWORD>(rectangle.x + rectangle.width - 1);
	const UWORD yEnd = rectangle.height == 0 ? rectangle.y : static_cast<UWORD>(rectangle.y + rectangle.height - 1);

	Paint_SelectImage(buffer);
	Paint_DrawRectangle(rectangle.x, rectangle.y, xEnd, yEnd, WHITE, toDotPixel(rectangle.lineWidth), toDrawFill(rectangle.fill));
}

void clearCircleRegion(UBYTE* buffer, const DisplayCircle& circle) {
	if (buffer == nullptr || !circle.isValid()) return;

	Paint_SelectImage(buffer);
	Paint_DrawCircle(circle.x, circle.y, circle.radius, WHITE, toDotPixel(circle.lineWidth), toDrawFill(circle.fill));
}

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the display driver, declared in display_driver.h.
 * @{
 */
bool flipDisplay(DisplayTone tone = display_config::kTonePrimary) {
	if (!displayStarted || !displayBuffer.isValid()) return false;

	displayBuffer.flip();
	displayBuffer.clearBuffer(tone);
	return true;
}

bool clearDisplay(DisplayTone tone = display_config::kTonePrimary) {
	if (!displayStarted || !displayBuffer.isValid()) return false;

	displayBuffer.clearBuffer(tone);
	return flipDisplay(tone);
}

bool startDisplayService(UWORD rotate, bool clearScreen) {
	(void)rotate;
	if (displayStarted) return true;

	if (!displayBuffer.validate()) {
		debug_logs::displayLogging("Failed to allocate e-paper frame buffers.");
        debug_logs::displayLogging("Attempting to reallocate e-paper frame buffers...");
        uint8_t retryCount = 0;
        do {
            debug_logs::displayLogging("Retry %d/%d: Reallocating e-paper frame buffers...", ++retryCount, display_config::kStartRetries);
            displayBuffer.validate();
        } while (!displayBuffer.isValid() && retryCount < display_config::kStartRetries);
        if (retryCount >= display_config::kStartRetries) {
            debug_logs::displayLogging("Exceeded maximum retries for reallocating e-paper frame buffers.");
            return false;
        }
	}

	if (DEV_Module_Init() != 0) {
		debug_logs::displayLogging("Failed to initialize e-paper module.");
		return false;
	}

	EPD_3IN52B_Init();
	displayBuffer.initCanvas();
	displayBuffer.clearBuffer();
	displayStarted = true;

	if (clearScreen) {
		clearDisplay();
	}

	debug_logs::displayLogging("Display service started.");
	return true;
}

bool stopDisplayService() {
	if (!displayStarted) return false;

	EPD_3IN52B_sleep();
	DEV_Module_Exit();

	displayBuffer.freeBuffers();
	displayStarted = false;

	debug_logs::displayLogging("Display service stopped.");
	return true;
}

void drawText(DisplayText text) {
	if (!displayStarted || !text.isValid()) return;

	const DisplayRectangle bounds = textBounds(text);
	if (text.textColor == DisplayTone::White || text.textColor == text.backgroundColor) {
		clearTextRegion(displayBuffer.black, bounds);
		clearTextRegion(displayBuffer.red, bounds);
		return;
	}

	UBYTE* buffer = layerBuffer(text.textColor);
	if (buffer == nullptr) return;

	Paint_SelectImage(buffer);
	Paint_DrawString_EN(text.x, text.y, text.text, text.font, toneToPaint(text.textColor), toneToPaint(text.backgroundColor));

	clearTextRegion(text.textColor == DisplayTone::Black ? displayBuffer.red : displayBuffer.black, bounds);
}

void drawRectangle(const DisplayRectangle& rectangle) {
	if (!displayStarted || !rectangle.isValid()) return;

	UBYTE* buffer = layerBuffer(rectangle.tone);
	UBYTE* oppositeBuffer = layerBuffer(rectangle.tone == DisplayTone::Red ? DisplayTone::Black : DisplayTone::Red);
	if (buffer == nullptr || oppositeBuffer == nullptr) return;

	const UWORD xEnd = rectangle.width == 0 ? rectangle.x : static_cast<UWORD>(rectangle.x + rectangle.width - 1);
	const UWORD yEnd = rectangle.height == 0 ? rectangle.y : static_cast<UWORD>(rectangle.y + rectangle.height - 1);

	Paint_SelectImage(buffer);
	Paint_DrawRectangle(rectangle.x, rectangle.y, xEnd, yEnd, toneToPaint(rectangle.tone), toDotPixel(rectangle.lineWidth), toDrawFill(rectangle.fill));
	clearRectangleRegion(oppositeBuffer, rectangle);
}

void drawCircle(const DisplayCircle& circle) {
	if (!displayStarted || !circle.isValid()) return;

	UBYTE* buffer = layerBuffer(circle.tone);
	UBYTE* oppositeBuffer = layerBuffer(circle.tone == DisplayTone::Red ? DisplayTone::Black : DisplayTone::Red);
	if (buffer == nullptr || oppositeBuffer == nullptr) return;

	Paint_SelectImage(buffer);
	Paint_DrawCircle(circle.x, circle.y, circle.radius, toneToPaint(circle.tone), toDotPixel(circle.lineWidth), toDrawFill(circle.fill));
	clearCircleRegion(oppositeBuffer, circle);
}

/** @} */ // end of Public
