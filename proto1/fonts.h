#ifndef FONTS_H
#define FONTS_H

#include <TFT_eSPI.h>

// Modern font sizes
#define FONT_LARGE 4    // For main values
#define FONT_MEDIUM 2   // For titles
#define FONT_SMALL 1    // For hints

// Simple 7-segment style "tiga" logo
const unsigned char TIGA_LOGO[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x7C, 0x10, 0x10, 0x10, 0x10, // t
    0x00, 0x40, 0x7C, 0x40, 0x00, // i
    0x7C, 0x44, 0x44, 0x44, 0x7C, // g
    0x7C, 0x44, 0x44, 0x44, 0x7C  // a
};

// Draw the tiga logo
void drawTigaLogo(TFT_eSPI& tft, int x, int y, uint16_t color) {
    // Draw each character pixel by pixel
    tft.fillRect(x, y, 100, 40, color); // Background
    tft.setTextColor(color);
    tft.setCursor(x + 10, y + 10);
    tft.setTextSize(3);
    tft.print("tiga");
}

// Helper function to convert float to char*
void floatToString(char* buffer, float value, int decimals) {
    if (decimals == 0) {
        sprintf(buffer, "%d", (int)value);
    } else {
        sprintf(buffer, "%.*f", decimals, value);
    }
}

// Helper function to convert int to char*
void intToString(char* buffer, int value) {
    sprintf(buffer, "%d", value);
}

#endif
