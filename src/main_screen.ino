#include <TFT_eSPI.h>
#include <SPI.h>
#include <ArduinoJson.h>
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

// ====== Button Pins ======
#define BTN_SCROLL 21
#define BTN_ENTER  16

int footerSelection = 1;  // 0 = SOS, 1 = Run Test, 2 = Bitchat
unsigned long lastPress = 0;

bool inTestMenu = false;
int testSelection = 0; // 0=Heart, 1=Fitness, 2=Stability, 3=Dexterity, 4=Back


// Layout constants
const int BAR_WIDTH = 20;     // Vertical health bar width
const int HEADER_H = 24;      // Header height
const int FOOTER_H = 24;      // Footer height
const int ICON_SIZE = 24;     // Icon placeholder size

// Embedded JSON string (your test.json content)
const char* testJson = R"rawliteral(
{
  "categories": [
    {
      "name": "Heart",
      "tests": [
        {
          "name": "Heart Rate",
          "instruction": "Place your thumb gently on the green light. Stay still.",
          "duration": 60,
          "sensor": "Pulse Sensor (green LED + photodiode)",
          "resultFormat": "Heart rate: {value} BPM"
        },
        {
          "name": "Heart Rate Variability",
          "instruction": "Keep your thumb gently on the green light. Breathe normally.",
          "duration": 60,
          "sensor": "Pulse Sensor",
          "resultFormat": "HRV score: {value}"
        },
        {
          "name": "Pulse Alert",
          "instruction": "Background monitoring, no action needed.",
          "duration": 0,
          "sensor": "Pulse Sensor",
          "resultFormat": "Alert if outside safe range"
        },
        {
          "name": "Exercise Zones",
          "instruction": "During workout, system shows zone.",
          "duration": 0,
          "sensor": "Pulse Sensor + Activity",
          "resultFormat": "Zone: {zone} ({bpm} BPM)"
        },
        {
          "name": "Resting Heart Report",
          "instruction": "Passive test while resting.",
          "duration": 0,
          "sensor": "Pulse Sensor",
          "resultFormat": "Resting heart rate: {value} BPM"
        }
      ]
    },
    {
      "name": "Fitness",
      "tests": [
        {
          "name": "Step Counter",
          "instruction": "Counts automatically, no action needed.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Steps today: {steps}"
        },
        {
          "name": "Activity Level",
          "instruction": "Passive monitoring of activity.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Activity level: {level}"
        },
        {
          "name": "Posture Monitor",
          "instruction": "Sit upright. Device will buzz if you slouch.",
          "duration": 0,
          "sensor": "MPU6050 + Vibration",
          "resultFormat": "Posture: {status}"
        },
        {
          "name": "Workout Test",
          "instruction": "Walk for 2 minutes with device. Keep moving.",
          "duration": 120,
          "sensor": "MPU6050 + Pulse Sensor",
          "resultFormat": "Calories burned: {calories}, HR: {bpm} BPM"
        },
        {
          "name": "High-Impact / Sudden Movement",
          "instruction": "Device checks if your movement is safe.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Impact: {status}"
        },
        {
          "name": "Walking Balance (Gait Check)",
          "instruction": "Walk for 30 seconds. Device will check your steps.",
          "duration": 30,
          "sensor": "MPU6050",
          "resultFormat": "Walking: {status}"
        }
      ]
    },
    {
      "name": "Stability",
      "tests": [
        {
          "name": "Fall Detection",
          "instruction": "Background monitoring for falls.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Fall detected: {status}"
        },
        {
          "name": "Balance Test",
          "instruction": "Stand still for 10 seconds.",
          "duration": 10,
          "sensor": "MPU6050",
          "resultFormat": "Balance: {status}"
        },
        {
          "name": "Impact Alert",
          "instruction": "Strong impact detected.",
          "duration": 0,
          "sensor": "MPU6050 + Vibration",
          "resultFormat": "Impact alert: {status}"
        },
        {
          "name": "Sleep Movement",
          "instruction": "Passive overnight monitoring.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Toss/turn count: {value}"
        }
      ]
    },
    {
      "name": "Dexterity",
      "tests": [
        {
          "name": "Finger Mobility",
          "instruction": "Tap the pad as fast as you can, 10 times.",
          "duration": 0,
          "sensor": "TTP223 Touch Sensor",
          "resultFormat": "Tap speed: {ms} ms avg"
        },
        {
          "name": "Hand Coordination",
          "instruction": "When you feel vibration, tap the pad quickly.",
          "duration": 0,
          "sensor": "TTP223 + Vibration",
          "resultFormat": "Reaction time: {ms}"
        },
        {
          "name": "Grip Strength",
          "instruction": "Press and hold the pad. Release when you can’t hold anymore.",
          "duration": 0,
          "sensor": "TTP223",
          "resultFormat": "Grip duration: {seconds} s"
        }
      ]
    }
  ]
}
)rawliteral";

// Parsed JSON document
DynamicJsonDocument doc(8192);

void setup() {
  tft.init();
  tft.setRotation(1); // Landscape

  pinMode(BTN_SCROLL, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);
  
  DeserializationError error = deserializeJson(doc, testJson);
  if (error) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("JSON Load Error!", tft.width()/2, tft.height()/2, 2);
    while(1); // stop
  }

  drawSplashScreen();
  delay(4000);
  drawMainScreen();
}

// ====== Loop ======
void loop() {
  if (digitalRead(BTN_SCROLL) == LOW && millis() - lastPress > 200) {
    if (inTestMenu) {
      testSelection = (testSelection + 1) % 5; // cycle through 5 options
      drawTestMenu();
    } else {
      footerSelection = (footerSelection + 1) % 3;
      drawFooter(footerSelection);
    }
    lastPress = millis();
  }

  if (digitalRead(BTN_ENTER) == LOW && millis() - lastPress > 300) {
    if (inTestMenu) {
      handleTestSelection(testSelection);
    } else {
      handleSelection(footerSelection);
    }
    lastPress = millis();
  }
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

  // finally call footer after rows
  drawFooter(footerSelection);
}   // <-- CLOSE drawMainScreen properly here

// --- Footer ---
void drawFooter(int selection) {
  int footerY = tft.height() - FOOTER_H;

// Fill footer background first
tft.fillRect(BAR_WIDTH, footerY, tft.width() - BAR_WIDTH, FOOTER_H, FOOTER_BG);

// Draw fine separator line just above footer
tft.drawFastHLine(BAR_WIDTH, footerY, tft.width() - BAR_WIDTH, HEADER_FOOTER_GREY);

// SOS (left aligned)
  tft.setTextDatum(TL_DATUM);
  String sosText = (selection == 0) ? "[SOS Help]" : "SOS Help";
  tft.setTextColor(selection == 0 ? TFT_WHITE : HEADER_FOOTER_GREY, FOOTER_BG);
  tft.drawString(sosText, BAR_WIDTH + 8, footerY + 4, 2);

  // Run Test (center)
  tft.setTextDatum(TC_DATUM);
  String runTestText = (selection == 1) ? "[Run Test]" : "Run Test";
  tft.setTextColor(selection == 1 ? TFT_WHITE : HEADER_FOOTER_GREY, FOOTER_BG);
  tft.drawString(runTestText, tft.width() / 2, footerY + 4, 2);

  // Bitchat (right aligned, green highlight when active)
  tft.setTextDatum(TR_DATUM);
  String bitchatText = (selection == 2) ? "[Bitchat]" : "Bitchat";
  tft.setTextColor(selection == 2 ? TFT_WHITE : BITCHAT_GREEN, FOOTER_BG);
  tft.drawString(bitchatText, tft.width() - 8, footerY + 4, 2);
}

// --- SOS Popup ---
void showSOSPopup() {
  int popupWidth = tft.width() * 2 / 3;
  int popupHeight = tft.height() / 3;
  int popupX = (tft.width() - popupWidth) / 2;
  int popupY = (tft.height() - popupHeight) / 2;

  // Draw popup background (red with white border)
  tft.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 6, TFT_RED);
  tft.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 6, TFT_WHITE);

  // Draw text in the middle of popup
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString("Contacting\nemergency\ncontacts...", tft.width()/2, tft.height()/2, 2);

  delay(2000);
  drawMainScreen();
}

// ====== Selection Actions ======
void handleSelection(int sel) {
  if (sel == 0) {
    // SOS smaller pop-up in the center (about 1/3 of screen height)
    int popupWidth = tft.width() * 2 / 3;
    int popupHeight = tft.height() / 3;
    int popupX = (tft.width() - popupWidth) / 2;
    int popupY = (tft.height() - popupHeight) / 2;

    // Draw popup box
    tft.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 6, TFT_BLACK);
    tft.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 6, TFT_RED);

    // Message inside popup
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Contacting emergency\ncontacts...", tft.width()/2, tft.height()/2, 2);

    delay(2000);
    drawMainScreen();

  } else if (sel == 1) {
    inTestMenu = true;
    testSelection = 0;
    drawTestMenu();

  } else if (sel == 2) {
    // Bitchat placeholder
    int popupWidth = tft.width() * 2 / 3;
    int popupHeight = tft.height() / 3;
    int popupX = (tft.width() - popupWidth) / 2;
    int popupY = (tft.height() - popupHeight) / 2;

    tft.fillRoundRect(popupX, popupY, popupWidth, popupHeight, 6, TFT_BLACK);
    tft.drawRoundRect(popupX, popupY, popupWidth, popupHeight, 6, BITCHAT_GREEN);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(BITCHAT_GREEN, TFT_BLACK);
    tft.drawString("Bitchat coming soon...", tft.width()/2, tft.height()/2, 2);
  
    delay(2000);
    drawMainScreen();

    } else if (sel == 3) {
    // Back from Test Menu
    drawMainScreen();
  }
}

// ====== Handle Test Selection (JSON dynamic) ======

void handleTestSelection(int sel) {
  if (sel >= 0 && sel <= 3) { // Categories 0–3
    drawSubFeatures(sel);
  } else if (sel == 4) { // Back from Test Menu
    inTestMenu = false;
    drawMainScreen();
  }
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

// ====== Draw Test Menu (JSON dynamic) ======
void drawTestMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  int midX = tft.width() / 2;
  int midY = tft.height() / 2;
  int boxW = tft.width() / 2;
  int boxH = (tft.height() - 40) / 2;

  // Draw the 4 category boxes
  auto drawBox = [&](int idx, int x, int y, String label) {
    uint16_t border = (testSelection == idx) ? TFT_CYAN : TFT_WHITE;
    uint16_t fill   = (testSelection == idx) ? TFT_DARKGREY : TFT_BLACK;

    tft.fillRoundRect(x, y, boxW, boxH, 4, fill);
    tft.drawRoundRect(x, y, boxW, boxH, 4, border);
    tft.setTextColor(TFT_WHITE, fill);
    tft.drawString(label, x + boxW / 2, y + boxH / 2, 2);
  };

  drawBox(0, 0, 0, "Heart");
  drawBox(1, boxW, 0, "Fitness");
  drawBox(2, 0, boxH, "Stability");
  drawBox(3, boxW, boxH, "Dexterity");

  // Back button
  uint16_t backColor = (testSelection == 4) ? TFT_RED : TFT_CYAN;
  tft.setTextColor(backColor, TFT_BLACK);
  tft.drawString("[Back]", midX, tft.height() - 20, 2);
}

// ====== Draw Sub-Features ======

int subSelection = 0;
int scrollOffset = 0;
bool inSubFeature = false;

void drawSubFeatures(int categoryIndex) {
  inSubFeature = true;
  subSelection = 0;
  scrollOffset = 0;

  const int lineHeight = 20; // spacing for readability
  const int visibleLines = (tft.height() - HEADER_H - FOOTER_H) / lineHeight - 1; // reserve space for back

  int numSubTests = doc["categories"][categoryIndex]["tests"].size();

  while (inSubFeature) {
    tft.fillScreen(TFT_BLACK);

    // Draw each visible sub-test
    for (int i = 0; i < visibleLines && i + scrollOffset < numSubTests; i++) {
      int idx = i + scrollOffset;
      String testName = doc["categories"][categoryIndex]["tests"][idx]["name"].as<String>();

      int y = HEADER_H + i * lineHeight + 10;
      if (idx == subSelection) {
        tft.fillRect(BAR_WIDTH, y - 2, tft.width() - BAR_WIDTH * 2, lineHeight, TFT_CYAN);
        tft.setTextColor(TFT_BLACK, TFT_CYAN);
      } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
      }

      tft.setTextDatum(TL_DATUM);
      tft.setFreeFont(&FreeSans12pt7b);
      tft.drawString(testName, BAR_WIDTH + 8, y);
    }

    // Draw "Back" option
    int backY = HEADER_H + visibleLines * lineHeight + 10;
    if (subSelection == numSubTests) {
      tft.fillRect(BAR_WIDTH, backY - 2, tft.width() - BAR_WIDTH * 2, lineHeight, TFT_RED);
      tft.setTextColor(TFT_WHITE, TFT_RED);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    tft.drawString("[Back]", BAR_WIDTH + 8, backY);

    // Wait for button input
    while (digitalRead(BTN_SCROLL) == HIGH && digitalRead(BTN_ENTER) == HIGH) {
      delay(10);
    }

    // Scroll button pressed
    if (digitalRead(BTN_SCROLL) == LOW) {
      subSelection++;
      if (subSelection > numSubTests) subSelection = 0;

      // Adjust scrollOffset if selection moves out of visible window
      if (subSelection - scrollOffset >= visibleLines) scrollOffset++;
      if (subSelection < scrollOffset) scrollOffset--;

      delay(200); // debounce
    }

    // Enter button pressed
    if (digitalRead(BTN_ENTER) == LOW) {
      if (subSelection == numSubTests) { // Back selected
        inSubFeature = false;
        drawTestMenu();
      } else {
        runSubTest(categoryIndex, subSelection);
      }
      delay(300); // debounce
    }
  }
}

// Run the selected sub-test (placeholder for actual sensor logic)
void runSubTest(int categoryIndex, int testIndex) {
  tft.fillScreen(TFT_BLACK);

  String testName = doc["categories"][categoryIndex]["tests"][testIndex]["name"].as<String>();
  String instruction = doc["categories"][categoryIndex]["tests"][testIndex]["instruction"].as<String>();
  int duration = doc["categories"][categoryIndex]["tests"][testIndex]["duration"];
  String sensor = doc["categories"][categoryIndex]["tests"][testIndex]["sensor"].as<String>();

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.drawString(testName + "\n\n" + instruction, tft.width()/2, tft.height()/2);

  // ---- Sensor logic placeholder ----
  // Example for pulse sensor:
  if (sensor.indexOf("Pulse Sensor") >= 0) {
    // call function readPulseSensor() or similar
  } else if (sensor.indexOf("MPU6050") >= 0) {
    // call accelerometer/motion reading
  } else if (sensor.indexOf("TTP223") >= 0) {
    // call touch sensor reading
  }

  // For now just wait for the duration or simulate result
  if (duration > 0) {
    delay(duration * 1000);
  } else {
    delay(2000);
  }

  // Optionally, show result placeholder (later replace with real data)
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Test Complete!\nResult: TBD", tft.width()/2, tft.height()/2, 2);
  delay(2000);

  // Return to sub-feature menu
  drawSubFeatures(categoryIndex);
}
