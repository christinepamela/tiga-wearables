#ifndef ICONS_H
#define ICONS_H

#include "heartIcon.h"
#include "stepsIcon.h"
#include "stabilityIcon.h"
#include "tempIcon.h"

// Modern, minimalist icon sizes
#define ICON_SIZE_SMALL 24
#define ICON_SIZE_MEDIUM 32
#define ICON_SIZE_LARGE 48

// Icon colors (using the modern theme)
#define ICON_COLOR_HEART 0xE453     // Soft red
#define ICON_COLOR_STEPS 0x34A8     // Teal
#define ICON_COLOR_STABILITY 0x4C9F  // Blue-green
#define ICON_COLOR_TEMP 0x7BEF      // Light blue
#define ICON_COLOR_CHAT 0x2D1F      // Mint green

// Bitchat specific icons
const unsigned char ICON_WIFI[] = {
    0x00, 0x0F, 0x00,
    0x0F, 0x00, 0x0F,
    0x00, 0x0F, 0x00,
    0x00, 0x04, 0x00,
    0x00, 0x04, 0x00
};

const unsigned char ICON_MESSAGE[] = {
    0x1F, 0x1F, 0x1F,
    0x11, 0x11, 0x11,
    0x1F, 0x1F, 0x1F,
    0x00, 0x04, 0x00,
    0x00, 0x00, 0x00
};

// Helper function to draw icons with proper scaling
void drawIcon(TFT_eSPI& tft, const uint16_t* icon, int x, int y, int size, uint16_t color = TFT_WHITE) {
    if (size == ICON_SIZE_SMALL) {
        tft.pushImage(x, y, 24, 24, icon);
    } else if (size == ICON_SIZE_MEDIUM) {
        // Scale up and draw
        for (int py = 0; py < 32; py++) {
            for (int px = 0; px < 32; px++) {
                int sourceX = px * 24 / 32;
                int sourceY = py * 24 / 32;
                tft.drawPixel(x + px, y + py, icon[sourceY * 24 + sourceX]);
            }
        }
    }
}

#endif
