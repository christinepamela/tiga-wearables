// main_screen.ino - cleaned & reorganized
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

// ====== Global UI / state ======
int footerSelection = 1;  // 0 = SOS, 1 = Run Test, 2 = Bitchat
unsigned long lastPress = 0;

// Test menu state
bool inTestMenu = false;    // true when on test menu (4 categories)
int testSelection = 0;      // highlighted category (0..3, 4 = Back)

// Sub-feature state
bool inSubFeature = false;  // true when viewing sub-tests for a category
int currentCategory = 0;    // which category's sub-tests are shown
int subSelection = 0;       // highlighted sub-test index or Back (numTests == Back)
int scrollOffset = 0;       // top visible index for sub-tests

// Layout constants
const int BAR_WIDTH = 20;     // Vertical health bar width
const int HEADER_H = 24;      // Header height
const int FOOTER_H = 24;      // Footer height
const int ICON_SIZE = 24;     // Icon placeholder size

// ========== Embedded JSON (tests) ==========
const char* testJson = R"rawliteral(
{
  "categories": [
    { "name":"Heart", "tests":[
        {"name":"Heart Rate","instruction":"Place your thumb gently on the green light. Stay still.","duration":60,"sensor":"Pulse Sensor (green LED + photodiode)","resultFormat":"Heart rate: {value} BPM"},
        {"name":"Heart Rate Variability","instruction":"Keep your thumb gently on the green light. Breathe normally.","duration":60,"sensor":"Pulse Sensor","resultFormat":"HRV score: {value}"},
        {"name":"Pulse Alert","instruction":"Background monitoring, no action needed.","duration":0,"sensor":"Pulse Sensor","resultFormat":"Alert if outside safe range"},
        {"name":"Exercise Zones","instruction":"During workout, system shows zone.","duration":0,"sensor":"Pulse Sensor + Activity","resultFormat":"Zone: {zone} ({bpm} BPM)"},
        {"name":"Resting Heart Report","instruction":"Passive test while resting.","duration":0,"sensor":"Pulse Sensor","resultFormat":"Resting heart rate: {value} BPM"}
      ]
    },
    { "name":"Fitness","tests":[
        {"name":"Step Counter","instruction":"Counts automatically, no action needed.","duration":0,"sensor":"MPU6050","resultFormat":"Steps today: {steps}"},
        {"name":"Activity Level","instruction":"Passive monitoring of activity.","duration":0,"sensor":"MPU6050","resultFormat":"Activity level: {level}"},
        {"name":"Posture Monitor","instruction":"Sit upright. Device will buzz if you slouch.","duration":0,"sensor":"MPU6050 + Vibration","resultFormat":"Posture: {status}"},
        {"name":"Workout Test","instruction":"Walk for 2 minutes with device. Keep moving.","duration":120,"sensor":"MPU6050 + Pulse Sensor","resultFormat":"Calories burned: {calories}, HR: {bpm} BPM"},
        {"name":"High-Impact / Sudden Movement","instruction":"Device checks if your movement is safe.","duration":0,"sensor":"MPU6050","resultFormat":"Impact: {status}"},
        {"name":"Walking Balance (Gait Check)","instruction":"Walk for 30 seconds. Device will check your steps.","duration":30,"sensor":"MPU6050","resultFormat":"Walking: {status}"}
      ]
    },
    { "name":"Stability","tests":[
        {"name":"Fall Detection","instruction":"Background monitoring for falls.","duration":0,"sensor":"MPU6050","resultFormat":"Fall detected: {status}"},
        {"name":"Balance Test","instruction":"Stand still for 10 seconds.","duration":10,"sensor":"MPU6050","resultFormat":"Balance: {status}"},
        {"name":"Impact Alert","instruction":"Strong impact detected.","duration":0,"sensor":"MPU6050 + Vibration","resultFormat":"Impact alert: {status}"},
        {"name":"Sleep Movement","instruction":"Passive overnight monitoring.","duration":0,"sensor":"MPU6050","resultFormat":"Toss/turn count: {value}"}
      ]
    },
    { "name":"Dexterity","tests":[
        {"name":"Finger Mobility","instruction":"Tap the pad as fast as you can, 10 times.","duration":0,"sensor":"TTP223 Touch Sensor","resultFormat":"Tap speed: {ms} ms avg"},
        {"name":"Hand Coordination","instruction":"When you feel vibration, tap the pad quickly.","duration":0,"sensor":"TTP223 + Vibration","resultFormat":"Reaction time: {ms}"},
        {"name":"Grip Strength","instruction":"Press and hold the pad. Release when you can’t hold anymore.","duration":0,"sensor":"TTP223","resultFormat":"Grip duration: {seconds} s"}
      ]
    }
  ]
}
)rawliteral";

// Parsed JSON document
DynamicJsonDocument doc(8192);

// ----------------- Forward declarations (optional) -----------------
void drawSplashScreen();
void drawMainScreen();
void drawFooter(int selection);
void drawTestMenu();
void drawSubFeatures(int categoryIndex);
void runSubTest(int categoryIndex, int testIndex);
void handleSelection(int sel);
void handleTestSelection(int sel);
void handleMainInput();
void handleTestMenuInput();
void handleSubFeatureInput();
void drawHealthBar(int percent);
void drawIcon(int x, int y, const uint16_t *icon);

// ============ setup ============
void setup() {
  tft.init();
  tft.setRotation(1); // Landscape

  pinMode(BTN_SCROLL, INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);

  // load JSON
  DeserializationError err = deserializeJson(doc, testJson);
  if (err) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("JSON load error", tft.width()/2, tft.height()/2);
    while (1) delay(1000);
  }

  drawSplashScreen();
  delay(2000);
  drawMainScreen();
}

// ============ main loop ========
void loop() {
  // Route input depending on current mode
  if (inSubFeature) {
    handleSubFeatureInput();
  } else if (inTestMenu) {
    handleTestMenuInput();
  } else {
    handleMainInput();
  }

  // tiny idle
  delay(10);
}

// ====== Input handlers ======
void handleMainInput() {
  // footer navigation
  if (digitalRead(BTN_SCROLL) == LOW && millis() - lastPress > 200) {
    footerSelection = (footerSelection + 1) % 3;
    drawFooter(footerSelection);
    lastPress = millis();
  }

  if (digitalRead(BTN_ENTER) == LOW && millis() - lastPress > 300) {
    handleSelection(footerSelection);
    lastPress = millis();
  }
}

void handleTestMenuInput() {
  // scroll through 5 options (4 categories + Back)
  int total = 5;
  if (digitalRead(BTN_SCROLL) == LOW && millis() - lastPress > 200) {
    testSelection = (testSelection + 1) % total;
    drawTestMenu();
    lastPress = millis();
  }

  if (digitalRead(BTN_ENTER) == LOW && millis() - lastPress > 300) {
    handleTestSelection(testSelection);
    lastPress = millis();
  }
}

void handleSubFeatureInput() {
  if (!inSubFeature) return;

  int numTests = doc["categories"][currentCategory]["tests"].size();
  int totalOptions = numTests + 1; // last = [Back]
  const int lineHeight = 16; // must match drawSubFeatures
  int visibleLines = (tft.height() - 20 - 10) / lineHeight; // marginTop=20 in drawSubFeatures

  if (digitalRead(BTN_SCROLL) == LOW && millis() - lastPress > 200) {
    subSelection = (subSelection + 1) % totalOptions;
    // adjust scroll window
    if (subSelection >= scrollOffset + visibleLines) scrollOffset = subSelection - visibleLines + 1;
    if (subSelection < scrollOffset) scrollOffset = subSelection;
    drawSubFeatures(currentCategory);
    lastPress = millis();
  }

  if (digitalRead(BTN_ENTER) == LOW && millis() - lastPress > 300) {
    if (subSelection == numTests) { // Back selected
      inSubFeature = false;
      drawTestMenu();
    } else {
      runSubTest(currentCategory, subSelection);
    }
    lastPress = millis();
  }
}

// ======= UI drawing functions =======

void drawSplashScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("25 Sep 2025", tft.width() / 2, tft.height() / 4);
  tft.setTextFont(1);
  tft.drawString("Thursday", tft.width() / 2, tft.height() / 4 + 30);
  tft.setTextFont(2);
  tft.drawString("tiga", tft.width() / 2, tft.height() * 3 / 4);
}

void drawMainScreen() {
  tft.fillScreen(TFT_BLACK);

  // health bar
  drawHealthBar(70);

  // header
  tft.fillRect(BAR_WIDTH, 0, tft.width() - BAR_WIDTH, HEADER_H, HEADER_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(HEADER_FOOTER_GREY, HEADER_BG);
  tft.drawString("12:34", BAR_WIDTH + 8, 4, 2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Thu 25 Sep", tft.width() / 2, 4, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(BITCHAT_GREEN, HEADER_BG);
  tft.drawString("||||||", tft.width() - 48, 4, 2);
  tft.setTextColor(TFT_WHITE, HEADER_BG);
  tft.drawString("100%", tft.width() - 4, 4, 2);
  tft.drawFastHLine(BAR_WIDTH, HEADER_H - 1, tft.width() - BAR_WIDTH, TFT_WHITE);

  // body rows
  int rowY = HEADER_H + 30;
  int rowSpacing = 40;
  int colX1 = BAR_WIDTH + 16;
  int colX2 = tft.width() / 2 + 16;

  drawIcon(colX1, rowY, heartIcon);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Heart   82 BPM", colX1 + ICON_SIZE + 6, rowY, 2);

  drawIcon(colX2, rowY, stepsIcon);
  tft.drawString("Steps   1243", colX2 + ICON_SIZE + 6, rowY, 2);
  rowY += rowSpacing;

  drawIcon(colX1, rowY, stabilityIcon);
  tft.drawString("Stability Safe", colX1 + ICON_SIZE + 6, rowY, 2);

  drawIcon(colX2, rowY, tempIcon);
  tft.drawString("Temp 26.4 C", colX2 + ICON_SIZE + 6, rowY, 2);

  // footer
  drawFooter(footerSelection);
}

void drawFooter(int selection) {
  int footerY = tft.height() - FOOTER_H;
  tft.fillRect(BAR_WIDTH, footerY, tft.width() - BAR_WIDTH, FOOTER_H, FOOTER_BG);
  tft.drawFastHLine(BAR_WIDTH, footerY, tft.width() - BAR_WIDTH, HEADER_FOOTER_GREY);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(selection == 0 ? TFT_WHITE : HEADER_FOOTER_GREY, FOOTER_BG);
  tft.drawString(selection == 0 ? "[SOS Help]" : "SOS Help", BAR_WIDTH + 8, footerY + 4, 2);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(selection == 1 ? TFT_WHITE : HEADER_FOOTER_GREY, FOOTER_BG);
  tft.drawString(selection == 1 ? "[Run Test]" : "Run Test", tft.width() / 2, footerY + 4, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(selection == 2 ? TFT_WHITE : BITCHAT_GREEN, FOOTER_BG);
  tft.drawString(selection == 2 ? "[Bitchat]" : "Bitchat", tft.width() - 8, footerY + 4, 2);
}

void drawTestMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  int boxW = tft.width() / 2;
  int boxH = (tft.height() - 40) / 2;

  auto drawBox = [&](int idx, int x, int y, const char* label) {
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

  tft.setTextColor(testSelection == 4 ? TFT_RED : TFT_CYAN, TFT_BLACK);
  tft.drawString("[Back]", tft.width()/2, tft.height() - 20, 2);
}

// draw sub-tests with normal text-size and simple spacing
void drawSubFeatures(int categoryIndex) {
  inSubFeature = true;
  currentCategory = categoryIndex;

  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  const int lineHeight = 16;
  const int marginTop = 20;

  int numTests = doc["categories"][categoryIndex]["tests"].size();
  int totalOptions = numTests + 1; // include [Back]
  int visibleLines = (tft.height() - marginTop - 10) / lineHeight;

  // clamp scrollOffset
  if (scrollOffset < 0) scrollOffset = 0;
  if (scrollOffset > max(0, totalOptions - visibleLines)) scrollOffset = max(0, totalOptions - visibleLines);

  int start = scrollOffset;
  int end = min(start + visibleLines, totalOptions);

  for (int i = start; i < end; i++) {
    int y = marginTop + (i - start) * lineHeight;
    bool isSelected = (i == subSelection);

    if (isSelected) {
      tft.fillRect(8, y - 1, tft.width() - 16, lineHeight, TFT_CYAN);
      tft.setTextColor(TFT_BLACK, TFT_CYAN);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    if (i < numTests) {
      String testName = doc["categories"][categoryIndex]["tests"][i]["name"].as<String>();
      tft.drawString(testName, 12, y);
    } else {
      tft.drawString("[Back]", 12, y);
    }
  }
}

// runSubTest - show instruction and simulate result (placeholder for real sensor code)
void runSubTest(int categoryIndex, int testIndex) {
  tft.fillScreen(TFT_BLACK);

  String testName = doc["categories"][categoryIndex]["tests"][testIndex]["name"].as<String>();
  String instruction = doc["categories"][categoryIndex]["tests"][testIndex]["instruction"].as<String>();
  int duration = doc["categories"][categoryIndex]["tests"][testIndex]["duration"];
  String sensor = doc["categories"][categoryIndex]["tests"][testIndex]["sensor"].as<String>();

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.drawString(testName, tft.width()/2, tft.height()/2 - 18);
  tft.setTextFont(1);
  tft.drawString(instruction, tft.width()/2, tft.height()/2 + 6);

  // Placeholder sensor branch - insert real sensor calls here later
  if (sensor.indexOf("Pulse Sensor") >= 0) {
    // TODO: read pulse sensor
  } else if (sensor.indexOf("MPU6050") >= 0) {
    // TODO: read accelerometer
  } else if (sensor.indexOf("TTP223") >= 0) {
    // TODO: read touch sensor
  }

  // simulate duration (short for dev)
  if (duration > 0) delay(min(5000, duration * 1000)); // cap in dev to avoid long hang
  else delay(1500);

  // show dummy result
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.drawString("Test Complete!", tft.width()/2, tft.height()/2 - 8);
  tft.setTextFont(1);
  tft.drawString("Result: 123 (dummy)", tft.width()/2, tft.height()/2 + 12);
  delay(1400);

  // back to sub-feature list (keep selection on executed item)
  drawSubFeatures(categoryIndex);
}

// ========== selection action (footer) ==========
void handleSelection(int sel) {
  if (sel == 0) {
    // SOS - small centered popup
    int popupW = tft.width() * 2 / 3;
    int popupH = tft.height() / 3;
    int popupX = (tft.width() - popupW) / 2;
    int popupY = (tft.height() - popupH) / 2;

    tft.fillRoundRect(popupX, popupY, popupW, popupH, 6, TFT_RED);
    tft.drawRoundRect(popupX, popupY, popupW, popupH, 6, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawString("Contacting emergency\ncontacts...", tft.width()/2, tft.height()/2);
    delay(1600);
    drawMainScreen();
  }
  else if (sel == 1) {
    // Enter Test Menu
    inTestMenu = true;
    testSelection = 0;
    drawTestMenu();
  }
  else if (sel == 2) {
    // Bitchat placeholder popup
    int popupW = tft.width() * 2 / 3;
    int popupH = tft.height() / 3;
    int popupX = (tft.width() - popupW) / 2;
    int popupY = (tft.height() - popupH) / 2;

    tft.fillRoundRect(popupX, popupY, popupW, popupH, 6, TFT_BLACK);
    tft.drawRoundRect(popupX, popupY, popupW, popupH, 6, BITCHAT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(BITCHAT_GREEN, TFT_BLACK);
    tft.drawString("Bitchat coming soon...", tft.width()/2, tft.height()/2);
    delay(1200);
    drawMainScreen();
  }
}

// handle selection inside Test Menu (4 categories + back)
void handleTestSelection(int sel) {
  if (sel >= 0 && sel <= 3) {
    // Enter that category's sub-tests
    currentCategory = sel;
    subSelection = 0;
    scrollOffset = 0;
    inSubFeature = true;
    drawSubFeatures(currentCategory);
  } else if (sel == 4) {
    // Back from test menu to main screen
    inTestMenu = false;
    drawMainScreen();
  }
}

// ============ Health Bar & icons ============
void drawHealthBar(int percent) {
  int barHeight = tft.height();
  int x = 0;
  int y = 0;
  tft.drawRect(x, y, BAR_WIDTH, barHeight, TFT_WHITE);
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

void drawIcon(int x, int y, const uint16_t *icon) {
  tft.pushImage(x, y, ICON_SIZE, ICON_SIZE, icon);
}
