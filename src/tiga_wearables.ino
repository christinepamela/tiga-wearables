#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>

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

// Categories
const char* categories[] = {"❤️ Heart", "👣 Fitness", "⚠️ Stability", "👣 Dexterity"};
int selectedCategory = 0;      // in category bar
bool inCategoryMenu = false;   // true if inside test menu
int selectedTest = 0;
int currentPage = 0;

// Example test lists
const char* heartTests[] = {"Heart Rate", "HRV", "Pulse Alert", "Exercise Zones", "Resting Heart"};
const char* fitnessTests[] = {"Step Counter", "Activity Level", "Posture Monitor", "Workout Test", "Jump Detection", "Gesture Detection"};
const char* stabilityTests[] = {"Fall Detection", "Balance Test", "Impact Alert", "Sleep Movement"};
const char* dexterityTests[] = {"Finger Mobility", "Hand Coordination", "Grip Strength", "Custom Gestures"};

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  pinMode(btnScroll, INPUT_PULLUP);
  pinMode(btnSelect, INPUT_PULLUP);

  drawCategoryBar();
}

void loop() {
  if (!inCategoryMenu) {
    // Navigate categories left/right
    if (digitalRead(btnScroll) == LOW) {
      selectedCategory = (selectedCategory + 1) % 4;
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
    const char** tests;
    int numTests;

    switch(selectedCategory) {
      case 0: tests = heartTests; numTests = sizeof(heartTests)/sizeof(heartTests[0]); break;
      case 1: tests = fitnessTests; numTests = sizeof(fitnessTests)/sizeof(fitnessTests[0]); break;
      case 2: tests = stabilityTests; numTests = sizeof(stabilityTests)/sizeof(stabilityTests[0]); break;
      case 3: tests = dexterityTests; numTests = sizeof(dexterityTests)/sizeof(dexterityTests[0]); break;
    }

    int testsOnPage = min(6, numTests - currentPage*6);

    // Scroll through tests
    if (digitalRead(btnScroll) == LOW) {
      selectedTest++;
      if (selectedTest >= testsOnPage) selectedTest = 0;
      drawTestMenu();
      delay(200);
    }

    // Select test
    if (digitalRead(btnSelect) == LOW) {
      runTest(tests[selectedTest + currentPage*6]);
      drawTestMenu();
      delay(500);
    }
  }
}

void drawCategoryBar() {
  tft.fillRect(0, 100, 240, 35, TFT_BLACK);
  tft.drawLine(0, 100, 240, 100, TFT_DARKGREY);
  int x = leftIndent;
  for (int i = 0; i < 4; i++) {
    if (i == selectedCategory) tft.fillRect(x-2, 102, 55, 20, TFT_CATEGORY_SELECTED);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(categories[i], x, 105, 2);
    x += 60;
  }
}

void drawTestMenu() {
  tft.fillRect(0, 22, 240, 75, TFT_BLACK); // clear area
  int yStart = 22;

  const char** tests;
  int numTests;

  switch(selectedCategory) {
    case 0: tests = heartTests; numTests = sizeof(heartTests)/sizeof(heartTests[0]); break;
    case 1: tests = fitnessTests; numTests = sizeof(fitnessTests)/sizeof(fitnessTests[0]); break;
    case 2: tests = stabilityTests; numTests = sizeof(stabilityTests)/sizeof(stabilityTests[0]); break;
    case 3: tests = dexterityTests; numTests = sizeof(dexterityTests)/sizeof(dexterityTests[0]); break;
  }

  int col1X = leftIndent;
  int col2X = leftIndent + colWidth + colSpacing;
  int testsOnPage = min(6, numTests - currentPage*6);

  for (int i = 0; i < testsOnPage; i++) {
    int x = (i < 3) ? col1X : col2X;
    int y = yStart + (i % 3) * rowHeight;

    // Highlight selected
    if (i == selectedTest) tft.fillRect(x, y, colWidth, rowHeight, TFT_HIGHLIGHT);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(tests[i + currentPage*6], x + 2, y + 2, 2);
  }

  // Page indicator
  if (numTests > 6) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("᳝ ᳝ ᳝", 100, 90, 2);
  }
}

void runTest(const char* testName) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_HEADER, TFT_BLACK);
  tft.drawString("Running Test:", leftIndent, 50, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(testName, leftIndent, 80, 2);

  // **Placeholder for actual sensor test**
  if (strcmp(testName, "Heart Rate") == 0) {
    // Call function to read pulse sensor
    tft.drawString("Place finger on sensor...", leftIndent, 110, 2);
  } else if (strcmp(testName, "HRV") == 0) {
    tft.drawString("HRV calculation...", leftIndent, 110, 2);
  } else if (strcmp(testName, "Pulse Alert") == 0) {
    tft.drawString("Monitoring BPM...", leftIndent, 110, 2);
  } else if (strcmp(testName, "Fall Detection") == 0) {
    tft.drawString("Monitoring movement...", leftIndent, 110, 2);
  } else if (strcmp(testName, "Finger Mobility") == 0) {
    tft.drawString("Tap sensor quickly...", leftIndent, 110, 2);
  }
  delay(2000);
}
