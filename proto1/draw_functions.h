#ifndef DRAW_FUNCTIONS_H
#define DRAW_FUNCTIONS_H

#include <TFT_eSPI.h>
#include "fonts.h"

// Function declarations
void drawCard(int x, int y, int w, int h, const char* title, const char* value, 
             const uint16_t* icon, uint16_t color);
void updateMainScreen();
void drawHeader();
void drawFooter();
void drawHealthBar();
void drawSplashScreen();

extern TFT_eSPI tft;
extern bool needsRedraw;

// Screen dimensions
#define SCREEN_W 320
#define SCREEN_H 170

// Layout constants
#define HEADER_H 32
#define FOOTER_H 40
#define HEALTH_BAR_W 24
#define PADDING 12
#define CARD_RADIUS 8

// Modern color scheme
const uint16_t COLOR_BG = 0x1082;       // Almost black
const uint16_t COLOR_CARD = 0x2104;     // Dark gray for cards
const uint16_t COLOR_TEXT = 0xFFFF;     // White text
const uint16_t COLOR_SUBTEXT = 0x8410;  // Light gray
const uint16_t COLOR_ACCENT = 0x05FF;   // Accent blue
const uint16_t COLOR_SUCCESS = 0x0680;  // Success green
const uint16_t COLOR_WARNING = 0xFD00;  // Warning orange
const uint16_t COLOR_DANGER = 0xE8A5;   // Error red

#endif
