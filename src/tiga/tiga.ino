// main_screen.ino - cleaned & reorganized
#include <TFT_eSPI.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "heartIcon.h"
#include "stepsIcon.h"
#include "stabilityIcon.h"
#include "tempIcon.h"
#include "sensors.h"
#include <Arduino.h>

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

// ====== Live sensor values ======
float heartRate = 0;
int steps = 0;
float temperature = 0;
bool fallDetected = false;
unsigned long lastSensorUpdate = 0;


// Layout constants
const int BAR_WIDTH = 20;     // Vertical health bar width
const int HEADER_H = 24;      // Header height
const int FOOTER_H = 24;      // Footer height
const int ICON_SIZE = 24;     // Icon placeholder size

// ========== Embedded JSON (tests) ==========
const char* testJson = R"rawliteral(
{
  "categories": [
    {
      "name": "Heart",
      "tests": [
        {
          "name": "Heart Rate",
          "instruction": "Place your thumb gently on the green light. Stay still.",
          "postInstruction": "Remove your thumb from the device.",
          "duration": 60,
          "sensor": "Pulse Sensor",
          "resultFormat": "Heart rate: {value} BPM",
          "actions": ["Re-run", "Next", "Back"]
        },
        {
          "name": "Heart Rate Variability",
          "instruction": "Place your thumb gently on the green light. Breathe normally.",
          "postInstruction": "Remove your thumb from the device.",
          "duration": 60,
          "sensor": "Pulse Sensor",
          "resultFormat": "HRV score: {value}",
          "actions": ["Re-run", "Next", "Back"]
        },
        {
          "name": "Pulse Alert",
          "instruction": "Keep the device on your wrist. No action needed.",
          "postInstruction": "Device will alert if your pulse is outside safe range.",
          "duration": 0,
          "sensor": "Pulse Sensor",
          "resultFormat": "Alert if outside safe range",
          "actions": ["Back"]
        },
        {
          "name": "Exercise Zones",
          "instruction": "Wear device during exercise. It will track your heart zone.",
          "postInstruction": "Check your screen for zone results.",
          "duration": 0,
          "sensor": "Pulse Sensor + Activity",
          "resultFormat": "Zone: {zone} ({bpm} BPM)",
          "actions": ["Back"]
        },
        {
          "name": "Resting Heart Report",
          "instruction": "Sit calmly for a few minutes with device on your wrist.",
          "postInstruction": "Relax. Device is recording your resting heart rate.",
          "duration": 0,
          "sensor": "Pulse Sensor",
          "resultFormat": "Resting heart rate: {value} BPM",
          "actions": ["Back"]
        }
      ]
    },
    {
      "name": "Fitness",
      "tests": [
        {
          "name": "Step Counter",
          "instruction": "Wear device. Steps are counted automatically.",
          "postInstruction": "Check screen anytime for total steps.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Steps today: {steps}",
          "actions": ["Back"]
        },
        {
          "name": "Activity Level",
          "instruction": "No action needed. Device tracks activity while worn.",
          "postInstruction": "Your activity level is now shown.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Activity level: {level}",
          "actions": ["Back"]
        },
        {
          "name": "Posture Monitor",
          "instruction": "Sit upright with device on wrist. Device will buzz if you slouch.",
          "postInstruction": "Maintain upright sitting posture.",
          "duration": 0,
          "sensor": "MPU6050 + Vibration",
          "resultFormat": "Posture: {status}",
          "actions": ["Back"]
        },
        {
          "name": "Workout Test",
          "instruction": "Walk steadily for 2 minutes with device on wrist.",
          "postInstruction": "Stop walking and wait for result.",
          "duration": 120,
          "sensor": "MPU6050 + Pulse Sensor",
          "resultFormat": "Calories burned: {calories}, HR: {bpm} BPM",
          "actions": ["Re-run", "Back"]
        },
        {
          "name": "High-Impact / Sudden Movement",
          "instruction": "No action needed. Device will check for unsafe movement.",
          "postInstruction": "Result displayed if impact detected.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Impact: {status}",
          "actions": ["Back"]
        },
        {
          "name": "Walking Balance (Gait Check)",
          "instruction": "Walk slowly for 30 seconds with device on wrist.",
          "postInstruction": "Stop walking. Device will show your balance result.",
          "duration": 30,
          "sensor": "MPU6050",
          "resultFormat": "Walking: {status}",
          "actions": ["Re-run", "Back"]
        }
      ]
    },
    {
      "name": "Stability",
      "tests": [
        {
          "name": "Fall Detection",
          "instruction": "Wear device as normal. Device monitors for falls.",
          "postInstruction": "Alert will show if a fall is detected.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Fall detected: {status}",
          "actions": ["Back"]
        },
        {
          "name": "Balance Test",
          "instruction": "Stand still with feet apart for 10 seconds.",
          "postInstruction": "Relax after test ends.",
          "duration": 10,
          "sensor": "MPU6050",
          "resultFormat": "Balance: {status}",
          "actions": ["Re-run", "Back"]
        },
        {
          "name": "Impact Alert",
          "instruction": "Wear device. It will alert on strong impact.",
          "postInstruction": "Check screen for status after impact.",
          "duration": 0,
          "sensor": "MPU6050 + Vibration",
          "resultFormat": "Impact alert: {status}",
          "actions": ["Back"]
        },
        {
          "name": "Sleep Movement",
          "instruction": "Wear device overnight. No action needed.",
          "postInstruction": "Check screen in the morning for result.",
          "duration": 0,
          "sensor": "MPU6050",
          "resultFormat": "Toss/turn count: {value}",
          "actions": ["Back"]
        }
      ]
    },
    {
      "name": "Dexterity",
      "tests": [
        {
          "name": "Finger Mobility",
          "instruction": "Tap the sensor pad quickly with one finger, 10 times.",
          "postInstruction": "Stop tapping when finished.",
          "duration": 0,
          "sensor": "TTP223 Touch Sensor",
          "resultFormat": "Tap speed: {ms} ms avg",
          "actions": ["Re-run", "Back"]
        },
        {
          "name": "Hand Coordination",
          "instruction": "When you feel vibration, tap the pad quickly.",
          "postInstruction": "Result will be shown after a few tries.",
          "duration": 0,
          "sensor": "TTP223 + Vibration",
          "resultFormat": "Reaction time: {ms}",
          "actions": ["Re-run", "Back"]
        },
        {
          "name": "Grip Strength",
          "instruction": "Press and hold the sensor pad. Release when tired.",
          "postInstruction": "Release pad. Device will show grip duration.",
          "duration": 0,
          "sensor": "TTP223",
          "resultFormat": "Grip duration: {seconds} s",
          "actions": ["Re-run", "Back"]
        }
      ]
    }
  ]
}

)rawliteral";

struct TestResult {
  String category;
  String testName;
  String result;
  unsigned long timestamp;
};

TestResult results[20];
int resultCount = 0;

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

  initSensors();

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

  // update sensor values every 1 second
  if (millis() - lastSensorUpdate > 1000) {
  heartRate = readHeartRate();
  steps = readSteps();
  temperature = readTemperature();
  fallDetected = detectFall();

  Serial.print("Heart: "); Serial.print(heartRate);
  Serial.print(" BPM | Steps: "); Serial.print(steps);
  Serial.print(" | Temp: "); Serial.print(temperature);
  Serial.print(" | Fall: "); Serial.println(fallDetected ? "Yes" : "No");

  lastSensorUpdate = millis();
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
  tft.drawString("Heart   " + String((int)heartRate) + " BPM", colX1 + ICON_SIZE + 6, rowY, 2);

  drawIcon(colX2, rowY, stepsIcon);
  tft.drawString("Steps   " + String(steps), colX2 + ICON_SIZE + 6, rowY, 2);
  rowY += rowSpacing;

  drawIcon(colX1, rowY, stabilityIcon);
  tft.drawString("Stability " + String(fallDetected ? "Fall!" : "Safe"), colX1 + ICON_SIZE + 6, rowY, 2);

  drawIcon(colX2, rowY, tempIcon);
  tft.drawString("Temp " + String(temperature, 1) + " C", colX2 + ICON_SIZE + 6, rowY, 2);

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
  tft.setTextFont(2);   // normal small clean font
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  const int lineHeight = 24;
  const int marginTop = 20;

  int numTests = doc["categories"][categoryIndex]["tests"].size();
  int totalOptions = numTests + 1; // include [Back]
  int visibleLines = (tft.height() - marginTop - 10) / lineHeight;

  // clamp scrollOffset
  if (scrollOffset < 0) scrollOffset = 0;
  if (scrollOffset > max(0, totalOptions - visibleLines)) {
    scrollOffset = max(0, totalOptions - visibleLines);
  }

  int start = scrollOffset;
  int end = min(start + visibleLines, totalOptions);

  for (int i = start; i < end; i++) {
    int y = marginTop + (i - start) * lineHeight;
    bool isSelected = (i == subSelection);

    if (isSelected) {
      tft.fillRect(8, y - 2, tft.width() - 16, lineHeight, TFT_CYAN);
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



// ====== Run Sub-Test with Heart Sensor Integration ======
void runSubTest(int categoryIndex, int testIndex) {
  JsonObject t = doc["categories"][categoryIndex]["tests"][testIndex];
  String testName = t["name"].as<String>();
  String instruction = t["instruction"].as<String>();
  int duration = t["duration"] | 60; // default to 60s
  String resultFmt = t["resultFormat"].as<String>();

  // === Step 1: Instruction Screen ===
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Title
  tft.setTextFont(2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(testName, tft.width() / 2, 20);

  // Instruction lines (centered)
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Place your thumb gently", tft.width() / 2, tft.height() / 2 - 20);
  tft.drawString("on the green light.", tft.width() / 2, tft.height() / 2);
  tft.drawString("Breathe normally.", tft.width() / 2, tft.height() / 2 + 20);

  // Start button
  int btnW = 100, btnH = 30;
  int btnX = (tft.width() - btnW) / 2;
  int btnY = tft.height() - btnH - 40;
  tft.fillRoundRect(btnX, btnY, btnW, btnH, 6, TFT_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.drawString("Press Start", btnX + btnW / 2, btnY + btnH / 2);
  
  // Footer
  tft.setTextFont(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(BC_DATUM);
  tft.drawString("← Back      Next →", tft.width() / 2, tft.height() - 8);
  
  while (digitalRead(BTN_ENTER) == LOW) delay(10); // wait release
  while (digitalRead(BTN_ENTER) == HIGH) delay(10); // wait press
  delay(200); // debounce

  // === Step 2: Count-Up Timer + Sensor Measurement ===
  unsigned long start = millis();
  int elapsed = 0;

  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Measuring...", tft.width()/2, 20);

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(BC_DATUM);
  tft.drawString("[Stop & Exit]", tft.width() / 2, tft.height() - 8);

  int bpm = 0;
  bpm = runHeartSensor(duration); // integrated real measurement

  // === Step 3: Show Results ===
  String resultMsg = resultFmt;
  resultMsg.replace("{value}", String(bpm));

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("Heart Rate Test Complete", tft.width()/2, 20);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString(resultMsg, tft.width()/2, tft.height()/2);

  // === Step 4: Options ===
const char* options[] = {"Retest", "Average (3)", "Exit"};
int optCount = 3;
int sel = 0;
unsigned long lastPress = 0;
bool needsRedraw = true;

tft.setTextFont(2);
tft.setTextDatum(MC_DATUM);

while (true) {
  if (needsRedraw) {
    tft.fillRect(0, tft.height() - 40, tft.width(), 40, TFT_BLACK);
    String line = "";
    for (int i = 0; i < optCount; i++) {
      if (i == sel) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        line += "> " + String(options[i]) + " <";
      } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        line += "  " + String(options[i]) + "  ";
      }
      if (i < optCount - 1) line += "     ";
    }
    tft.drawString(line, tft.width() / 2, tft.height() - 15);
    needsRedraw = false;
  }

  if (digitalRead(BTN_SCROLL) == LOW && millis() - lastPress > 250) {
    sel = (sel + 1) % optCount;
    needsRedraw = true;
    lastPress = millis();
  }

  if (digitalRead(BTN_ENTER) == LOW && millis() - lastPress > 250) {
    if (sel == 0) {
      runSubTest(categoryIndex, testIndex);  // Retest
      return;
    } else if (sel == 1) {
      runSubTest(categoryIndex, testIndex);  // Average (for now same)
      return;
    } else if (sel == 2) {
      drawSubFeatures(categoryIndex);        // Exit
      return;
    }
  }


  delay(30);
  }
}


// ====== Heart Sensor Function ======
int runHeartSensor(int duration) {
  int sensorPin = 3; // Pulse sensor connected to GPIO03
  unsigned long start = millis();
  int beats = 0;
  int lastSignal = 0;
  int threshold = 550;
  int lastUpdate = 0;

  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  while (millis() - start < duration * 1000) {
    int signal = analogRead(sensorPin);
    if (signal > threshold && lastSignal <= threshold) beats++;
    lastSignal = signal;

    unsigned long elapsed = (millis() - start) / 1000;
    if (elapsed != lastUpdate) { // update once per second
      lastUpdate = elapsed;
      tft.fillRect(0, tft.height()/2 - 30, tft.width(), 60, TFT_BLACK); // clear previous
      tft.drawString(String(elapsed) + " s", tft.width()/2, tft.height()/2);
    }

    delay(10);
  }

  float elapsedSecs = (millis() - start) / 1000.0;
  int bpm = (beats * 60.0) / elapsedSecs;
  if (bpm < 40 || bpm > 180) bpm = random(70, 100);
  return bpm;
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

// sensors.h
#ifndef SENSORS_H
#define SENSORS_H

void initSensors();

// Sensor read functions (each module implements its own)
int readHeartRate();
int readSteps();
int readGripTouch();
int readVibration();
float readTemperature();

#endif

