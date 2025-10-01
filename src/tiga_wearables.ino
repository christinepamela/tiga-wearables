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
  // Show on screen
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("JSON parse failed!", leftIndent, 20, 2);

  // Log in Serial for debugging
  Serial.print(F("JSON parse failed: "));
  Serial.println(error.c_str());

  while (1) delay(10); // stop if JSON invalid
} else {
  Serial.println("Loaded test categories:");
  for (JsonObject category : doc["categories"].as<JsonArray>()) {
    Serial.println(category["name"].as<const char*>());
  }
}


  drawCategoryBar();
}

void loop() {
  // DEBUG auto-run once (optional)
  if (millis() > 2000 && millis() < 5000) {
    runTest("Heart Rate");   // run specific test by name
  }

  // Category navigation
  if (!inCategoryMenu) {
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
    // Test navigation
    JsonArray testsArray = doc["categories"][selectedCategory]["tests"].as<JsonArray>();
    int numTests = testsArray.size();
    int testsOnPage = min(6, numTests - currentPage * 6);

    if (digitalRead(btnScroll) == LOW) {
      selectedTest++;
      if (selectedTest >= testsOnPage) selectedTest = 0;
      drawTestMenu();
      delay(200);
    }

    if (digitalRead(btnSelect) == LOW) {
      JsonObject testObj = testsArray[selectedTest + currentPage * 6];
      const char* testName = testObj["name"];  // ✅ grab name
      runTest(testName);                       // ✅ pass string
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

// ---------------- Run Test with Countdown + Progress Bar ----------------
void runTest(const char* testName) {
  JsonArray categories = doc["categories"].as<JsonArray>();

  for (JsonObject category : categories) {
    JsonArray tests = category["tests"].as<JsonArray>();
    for (JsonObject testObj : tests) {
      if (testObj["name"] == testName) {
        const char* instruction = testObj["instruction"];
        int duration = testObj["duration"];
        const char* resultFormat = testObj["resultFormat"];

        // clear and show instruction
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(instruction, leftIndent, 20, 2);

        if (duration > 0) {
          unsigned long start = millis();
          int barX = leftIndent;
          int barY = 80;
          int barW = tft.width() - 2 * leftIndent;
          int barH = 12;

          while (millis() - start < (unsigned long)duration * 1000) {
            float frac = float(millis() - start) / (duration * 1000.0);
            int filled = frac * barW;

            // draw progress bar
            tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
            tft.fillRect(barX, barY, filled, barH, TFT_GREEN);

            int remaining = duration - (millis() - start) / 1000;
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.fillRect(leftIndent, barY - 20, 120, 16, TFT_BLACK); // clear old
            tft.drawString("Time left: " + String(remaining) + "s", leftIndent, barY - 20, 2);

            delay(200);
          }
        }

        // feedback (blink screen as placeholder)
        tft.fillScreen(TFT_DARKGREEN);
        delay(300);
        tft.fillScreen(TFT_BLACK);

        // show result
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        String result = String(resultFormat);
        result.replace("{value}", "72"); // placeholder substitution
        result.replace("{bpm}", "72");
        result.replace("{status}", "OK");
        result.replace("{steps}", "1200");
        result.replace("{calories}", "18");
        tft.drawString("Result:", leftIndent, 120, 2);
        tft.drawString(result, leftIndent, 145, 2);

        delay(3000); // show result for 3s
        drawTestMenu(); // return to menu
        return;
      }
    }
  }
}





