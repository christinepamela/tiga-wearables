#include <TFT_eSPI.h>
#include <SPI.h>
#include "heartIcon.h"
#include "stepsIcon.h"
#include "stabilityIcon.h"
#include "tempIcon.h"


TFT_eSPI tft = TFT_eSPI();

// Colors
#define BITCHAT_GREEN 0x07E0
#define HEADER_BG     TFT_BLACK
#define FOOTER_BG     TFT_BLACK
#define HEADER_FOOTER_GREY tft.color565(180, 180, 180)  // RGB 180,180,180

// Layout constants
const int BAR_WIDTH = 20;     // Vertical health bar width
const int HEADER_H = 24;      // Header height
const int FOOTER_H = 24;      // Footer height
const int ICON_SIZE = 24;     // Icon placeholder size

void setup() {
  tft.init();
  tft.setRotation(1); // Landscape
  
  drawSplashScreen();  // Show splash screen
  delay(4000);         // Display for 4 seconds

  drawMainScreen();
}

void loop() {
  // later: update live data
}

// ============ Splash Screen ============
void drawSplashScreen() {
  tft.fillScreen(TFT_BLACK);

  // Big Date
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); // Middle-center
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.drawString("25 Sep 2025", tft.width() / 2, tft.height() / 4);

  // Day (smaller font, lighter)
  tft.setFreeFont(&FreeSans12pt7b);
  tft.drawString("Thursday", tft.width() / 2, tft.height() / 4 + 30);

  // Logo (custom block font)
  tft.setFreeFont(&FreeMonoBold18pt7b);
  tft.drawString("tiga", tft.width() / 2, tft.height() * 3 / 4);
}


// ============ Main Screen ============
void drawMainScreen() {
  tft.fillScreen(TFT_BLACK);

  // --- Vertical Health Bar ---
  drawHealthBar(70); // Example: 70%

  // --- Header ---
  tft.fillRect(BAR_WIDTH, 0, tft.width() - BAR_WIDTH, HEADER_H, HEADER_BG);
  tft.setTextColor(HEADER_FOOTER_GREY, HEADER_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("12:34", BAR_WIDTH + 8, 4, 2); // Left
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Thu 25 Sep", tft.width() / 2, 4, 2); // Center
  
    // Battery indicator (▮▮▮▮ 100%)
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(BITCHAT_GREEN, HEADER_BG);
  tft.drawString("||||||", tft.width() - 48, 4, 2);  // shift left so "100%" fits
  tft.setTextColor(TFT_WHITE, HEADER_BG);
  tft.drawString("100%", tft.width() - 4, 4, 2); // Right (battery placeholder)

  // --- Thin white line under header ---
  tft.drawFastHLine(BAR_WIDTH, HEADER_H - 1, tft.width() - BAR_WIDTH, TFT_WHITE);

  // --- Body rows ---
  int rowY = HEADER_H + 30;
  int rowSpacing = 40;
  int colX1 = BAR_WIDTH + 16;         // left col start
  int colX2 = tft.width() / 2 + 16;   // right col start

  // Row 1
  drawIcon(colX1, rowY, heartIcon);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Heart   82 BPM", colX1 + ICON_SIZE + 6, rowY, 2);

  drawIcon(colX2, rowY, stepsIcon);
  tft.drawString("Steps   1243", colX2 + ICON_SIZE + 6, rowY, 2);
  rowY += rowSpacing;

  // Row 2
  drawIcon(colX1, rowY, stabilityIcon);
  tft.drawString("Stability Safe", colX1 + ICON_SIZE + 6, rowY, 2);

  drawIcon(colX2, rowY, tempIcon);
  tft.drawString("Temp 26.4 C", colX2 + ICON_SIZE + 6, rowY, 2);

// --- Footer ---
int footerY = tft.height() - FOOTER_H;

// Fill footer background first
tft.fillRect(BAR_WIDTH, footerY, tft.width() - BAR_WIDTH, FOOTER_H, FOOTER_BG);

// Draw fine separator line just above footer
tft.drawFastHLine(BAR_WIDTH, footerY, tft.width() - BAR_WIDTH, HEADER_FOOTER_GREY);

// Footer text
tft.setTextDatum(TL_DATUM);
tft.setTextColor(HEADER_FOOTER_GREY, FOOTER_BG);
tft.drawString("SOS: Hold", BAR_WIDTH + 8, footerY + 4, 2);

tft.setTextDatum(TC_DATUM);
tft.setTextColor(HEADER_FOOTER_GREY, FOOTER_BG);
tft.drawString("[Run Test]", tft.width() / 2, footerY + 4, 2);

tft.setTextDatum(TR_DATUM);
tft.setTextColor(BITCHAT_GREEN, FOOTER_BG);
tft.drawString("Bitchat ON", tft.width() - 8, footerY + 4, 2);

}

// ============ Health Bar ============
void drawHealthBar(int percent) {
  int barHeight = tft.height();
  int x = 0;
  int y = 0;

  // Outline
  tft.drawRect(x, y, BAR_WIDTH, barHeight, TFT_WHITE);

  // Fill height
  int fillHeight = (barHeight - 2) * percent / 100;

  for (int i = 0; i < fillHeight; i++) {
    uint16_t color = tft.color565(
      map(i, 0, barHeight, 255, 0),   // R fades
      map(i, 0, barHeight, 0, 255),   // G rises
      0
    );
    tft.drawFastHLine(x + 1, y + barHeight - 1 - i, BAR_WIDTH - 2, color);
  }
}

// ============ Placeholder icon ============
void drawIcon(int x, int y, const uint16_t *icon) {
  tft.pushImage(x, y, ICON_SIZE, ICON_SIZE, icon);
}
