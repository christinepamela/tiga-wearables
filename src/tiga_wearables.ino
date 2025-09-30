#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();

// Buttons
const int btnScroll = 21; // scroll
const int btnSelect = 16; // select / enter

// Colors
#define TFT_HEADER   TFT_CYAN
#define TFT_HIGHLIGHT TFT_YELLOW
#define TFT_CATEGORY_SELECTED 0x07E0  // Bitchat green placeholder

// UI measurements
const int leftIndent = 20;
const int colWidth = 100;
const int colSpacing = 20;
const int rowHeight = 20;

// Category and Test State
int selectedCategory = 0;
bool inCategoryMenu = false;
int selectedTest = 0;
int currentPage = 0;

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
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  pinMode(btnScroll, INPUT_PULLUP);
  pinMode(btnSelect, INPUT_PULLUP);

  // Parse embedded JSON
  DeserializationError error = deserializeJson(doc, testJson);
  if (error) {
    Serial.print(F("JSON parse failed: "));
    Serial.println(error.c_str());
  } else {
    Serial.println("Loaded test categories:");
    for (JsonObject category : doc["categories"].as<JsonArray>()) {
      Serial.println(category["name"].as<const char*>());
    }
  }

  drawCategoryBar();
}

void loop() {
  if (!inCategoryMenu) {
    // Navigate categories left/right
    if (digitalRead(btnScroll) == LOW) {
      selectedCategory = (selectedCategory + 1) % doc["categories"].size();
      drawCategoryBar();
      delay(200);
    }
    if (digitalRead(btnSelect) == LOW) {
      inCategoryMenu = true;
      selectedTest = 0;
      currentPage = 0;
      drawTestMenu();
      delay(200);
    }
  } else {
    // Navigate tests
    JsonArray testsArray = doc["categories"][selectedCategory]["tests"].as<JsonArray>();
    int numTests = testsArray.size();
    int testsOnPage = min(6, numTests - currentPage*6);

    if (digitalRead(btnScroll) == LOW) {
      selectedTest++;
      if (selectedTest >= testsOnPage) selectedTest = 0;
      drawTestMenu();
      delay(200);
    }

    if (digitalRead(btnSelect) == LOW) {
      JsonObject testObj = testsArray[selectedTest + currentPage*6];
      runTest(testObj);
      drawTestMenu();
      delay(500);
    }
  }
}

void drawCategoryBar() {
  tft.fillRect(0, 100, 240, 35, TFT_BLACK);
  tft.drawLine(0, 100, 240, 100, TFT_DARKGREY);
  int x = leftIndent;

  for (int i = 0; i < doc["categories"].size(); i++) {
    if (i == selectedCategory) tft.fillRect(x-2, 102, 55, 20, TFT_CATEGORY_SELECTED);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(doc["categories"][i]["name"].as<const char*>(), x, 105, 2);
    x += 60;
  }
}

void drawTestMenu() {
  tft.fillRect(0, 22, 240, 75, TFT_BLACK); // clear area
  int yStart = 22;

  JsonArray testsArray = doc["categories"][selectedCategory]["tests"].as<JsonArray>();
  int numTests = testsArray.size();
  int col1X = leftIndent;
  int col2X = leftIndent + colWidth + colSpacing;
  int testsOnPage = min(6, numTests - currentPage*6);

  for (int i = 0; i < testsOnPage; i++) {
    int x = (i < 3) ? col1X : col2X;
    int y = yStart + (i % 3) * rowHeight;

    if (i == selectedTest) tft.fillRect(x, y, colWidth, rowHeight, TFT_HIGHLIGHT);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(testsArray[i + currentPage*6]["name"].as<const char*>(), x + 2, y + 2, 2);
  }

  if (numTests > 6) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("᳝ ᳝ ᳝", 100, 90, 2);
  }
}

void runTest(const char* testName) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_HEADER, TFT_BLACK);
  tft.drawString("Test:", leftIndent, 20, 2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(testName, leftIndent, 50, 2);

  // Look up test in JSON
  JsonObject testObj;
  bool found = false;
  for (JsonObject category : doc["categories"].as<JsonArray>()) {
    for (JsonObject t : category["tests"].as<JsonArray>()) {
      if (strcmp(t["name"], testName) == 0) {
        testObj = t;
        found = true;
        break;
      }
    }
    if (found) break;
  }

  if (!found) {
    tft.drawString("Test not found in JSON!", leftIndent, 80, 2);
    delay(2000);
    return;
  }

  // Show instruction
  tft.drawString(testObj["instruction"], leftIndent, 80, 2);

  // Active test: handle duration
  int duration = testObj["duration"] | 0;
  if (duration > 0) {
    int start = millis();
    while (millis() - start < duration * 1000) {
      int remaining = duration - (millis() - start) / 1000;
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.fillRect(leftIndent, 110, 100, 20, TFT_BLACK); // clear countdown
      tft.drawString("Time left: " + String(remaining) + "s", leftIndent, 110, 2);
      delay(200);
    }
  }

  // Show sensor source
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Sensor: " + String(testObj["sensor"].as<const char*>()), leftIndent, 140, 2);

  // Placeholder result
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Result: " + String(testObj["resultFormat"].as<const char*>()), leftIndent, 170, 2);

  // Wait for button press to continue
  tft.drawString("Press SELECT to continue...", leftIndent, 200, 2);
  while (digitalRead(btnSelect) == HIGH) delay(100); // wait for press
  delay(200); // debounce
}





