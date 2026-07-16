/**
 * @headerfile display_handler.h "src/display/display_handler.h"
 * 
 */

#pragma once

#include <fonts.h>

#include "GUI_Paint.h"

/** @brief Color tone used by the display wrapper. */
enum class DisplayTone : uint8_t {
	White = 0,
	Black = 1,
	Red = 2,
};

/** @brief Text description structure. */
struct DisplayText {
    int16_t x = 0;
    int16_t y = 0;
    const char* text = "";
	sFONT* font = &Font8; // Valid fonts; Font8, Font12, Font16, Font20, Font24
    DisplayTone textColor = DisplayTone::Black;
    DisplayTone backgroundColor = DisplayTone::White;

    bool isValid() const {
        return text != nullptr && font != nullptr && x >= 0 && y >= 0;
    }
};

/** @brief Rectangle shape description. */
struct DisplayRectangle {
	int16_t x = 0;
	int16_t y = 0;
	UWORD width = 0;
	UWORD height = 0;
	DisplayTone tone = DisplayTone::Black;
    int8_t lineWidth = 1; // valid range: 1-8, default is 1
	bool fill = false;
	// DOT_PIXEL lineWidth = DOT_PIXEL_1X1;
	// DRAW_FILL fill = DRAW_FILL_EMPTY;

    bool isValid() const {
        return width > 0 && height > 0 && x >= 0 && y >= 0 && lineWidth >= 1 && lineWidth <= 8;
    }
};

/** @brief Circle shape description. */
struct DisplayCircle {
	int16_t x = 0;
	int16_t y = 0;
	UWORD radius = 0;
	DisplayTone tone = DisplayTone::Black;
    int8_t lineWidth = 1; // valid range: 1-8, default is 1
	bool fill = false;
	// DOT_PIXEL lineWidth = DOT_PIXEL_1X1;
	// DRAW_FILL fill = DRAW_FILL_EMPTY;
    bool isValid() const {
        return radius > 0 && x >= 0 && y >= 0 && lineWidth >= 1 && lineWidth <= 8;
    }
};

/**
 * @brief Update the display with the contents of the cached buffers.
 * 
 * @details
 * This function sends the contents of the black and red frame buffers to the
 * e-paper module. The buffers are cleared after the display update to prepare
 * for the next drawing operations.
 * 
 * @return The status of the display update attempt.
 * @retval true The display was updated successfully.
 * @retval false The display update failed.
 * 
 */
bool flipDisplay(DisplayTone tone);

/**
 * @brief Clear the display to a specified tone (white, black, or red).
 * 
 * @details
 * This function clears the display to the specified tone, setting all pixels
 * to a color. The display is updated immediately after clearing, and the
 * buffers are cleared to prepare for the next drawing operations.
 * 
 * @param tone The tone to clear the display to.
 * 
 * @returns The status of the display clear attempt.
 * @retval true The display was cleared successfully.
 * @retval false The display clear operation failed.
 * 
 */
bool clearDisplay(DisplayTone tone);

/**
 * @brief Start the e-paper service and allocate drawing buffers.
 *
 * @details
 * This initializes the Waveshare module, creates cached black and red frame
 * buffers, and clears the panel to a white background by default.
 *
 * @param rotate Display rotation.
 * @param clearScreen When true, the display is cleared immediately after startup.
 *
 * @return The status of the display service startup attempt.
 * @retval true The display service was started successfully.
 * @retval false The display service failed to start, possibly due to a configuration issue or hardware problem.
 * 
 */
bool startDisplayService(UWORD rotate, bool clearScreen);

/**
 * @brief Stop the display service and release the module.
 * 
 * @details
 * This function puts the e-paper module into sleep mode, frees the allocated
 * buffers, and releases the module resources.
 *
 * @return The status of the display service shutdown attempt.
 * @retval true The display service was stopped successfully.
 * @retval false The display service failed to stop.
 * 
 */
bool stopDisplayService();


/**
 * @brief Draw text into the cached black or red layer.
 * 
 * @details
 * This function draws the specified text at the given coordinates using the
 * provided font, test color, and background color. The text is drawn into the
 * cached buffer for the specified tone.
 *
 * @param x Left offset position.
 * @param y Top offset position.
 * @param text Null-terminated text to draw.
 * @param font Font definition from Waveshare fonts.
 * @param tone Text tone.
 * @param background Background tone.
 * 
 * @par Returns
 * Nothing.
 * 
 */
void drawText(UWORD x, UWORD y, const char* text, sFONT* font, DisplayTone tone, DisplayTone background);

/**
 * @brief Draw a rectangle using the provided shape description.
 * 
 * @details
 * This function draws a rectangle at the specified coordinates found in the
 * DisplayRectangle structure. The rectangle is drawn into the cached buffer
 * for the specified tone. 
 * 
 * @param rectangle The DisplayRectangle structure containing the rectangle's properties.
 * 
 * @par Returns
 * Nothing.
 * 
 */
void drawRectangle(const DisplayRectangle& rectangle);

/**
 * @brief Draw a circle using the provided shape description.
 * 
 * @details
 * This function draws a circle at the specified coordinates found in the
 * DisplayCircle structure. The circle is drawn into the cached buffer for the
 * specified tone.
 *
 * @param circle The DisplayCircle structure containing the circle's properties.
 *
 * @par Returns
 * Nothing.
 * 
 */
void drawCircle(const DisplayCircle& circle);

// /**
//  * @brief Draw a bitmap image into the selected layer.
//  *
//  * @param imageBuffer Source bitmap buffer.
//  * @param x Left offset position.
//  * @param y Top offset position.
//  * @param width Image width in pixels.
//  * @param height Image height in pixels.
//  * @param tone Target tone/layer.
//  *
//  * @par Returns
//  * Nothing.
//  *
//  */
// void drawImage(const unsigned char* imageBuffer, UWORD x, UWORD y, UWORD width, UWORD height, DisplayTone tone);
