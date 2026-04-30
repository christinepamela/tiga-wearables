#include "tests.h"
#include "sensors.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

// Using same color scheme as main UI
const uint16_t TEST_BG = tft.color565(18, 18, 18);
const uint16_t TEST_CARD = tft.color565(32, 33, 36);
const uint16_t TEST_TEXT = tft.color565(255, 255, 255);
const uint16_t TEST_SUBTEXT = tft.color565(154, 160, 166);
const uint16_t TEST_ACCENT = tft.color565(66, 133, 244);
const uint16_t TEST_SUCCESS = tft.color565(52, 168, 83);
const uint16_t TEST_WARNING = tft.color565(251, 188, 4);
const uint16_t TEST_DANGER = tft.color565(234, 67, 53);

// Test Categories
const char* categories[] = {
    "Heart Tests",
    "Fitness Tests",
    "Stability Tests",
    "Dexterity Tests",
    "Back"
};

// Test states
static int currentCategory = 0;
static int currentTest = 0;
static bool inTestList = false;
static bool testRunning = false;

// Store last test results
struct TestResult {
    char category[20];
    char name[30];
    char result[50];
    unsigned long timestamp;
};

#define MAX_RESULTS 10
static TestResult results[MAX_RESULTS];
static int resultCount = 0;

void addTestResult(const char* category, const char* name, const char* result) {
    if(resultCount >= MAX_RESULTS) {
        // Shift all results down
        for(int i = 0; i < MAX_RESULTS-1; i++) {
            results[i] = results[i+1];
        }
        resultCount = MAX_RESULTS-1;
    }
    
    strncpy(results[resultCount].category, category, 19);
    strncpy(results[resultCount].name, name, 29);
    strncpy(results[resultCount].result, result, 49);
    results[resultCount].timestamp = millis();
    resultCount++;
}

void drawTestMenu() {
    tft.fillScreen(TEST_BG);
    
    // Header
    tft.fillRect(0, 0, 320, 30, TEST_CARD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TEST_TEXT);
    tft.drawString("Select Test Category", 160, 15);
    
    // Categories
    for(int i = 0; i < 5; i++) {
        int y = 40 + i * 25;
        bool selected = (i == currentCategory);
        
        if(selected) {
            tft.fillRoundRect(20, y-2, 280, 22, 4, TEST_ACCENT);
            tft.setTextColor(TEST_TEXT);
        } else {
            tft.setTextColor(TEST_SUBTEXT);
        }
        
        tft.setTextDatum(ML_DATUM);
        tft.drawString(categories[i], 30, y + 9);
    }
    
    // Footer hint
    tft.setTextColor(TEST_SUBTEXT);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Scroll: Next   Enter: Select", 160, 165);
}

// Test list for each category
const char* heartTests[] = {
    "Heart Rate",
    "Heart Rate Variability",
    "Continuous Monitoring",
    "Back"
};

const char* fitnessTests[] = {
    "Step Counter",
    "Activity Level",
    "Walking Test",
    "Back"
};

const char* stabilityTests[] = {
    "Balance Test",
    "Fall Detection",
    "Posture Check",
    "Back"
};

const char* dexterityTests[] = {
    "Reaction Time",
    "Touch Pattern",
    "Grip Test",
    "Back"
};

void drawTestList() {
    tft.fillScreen(TEST_BG);
    
    // Header
    tft.fillRect(0, 0, 320, 30, TEST_CARD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TEST_TEXT);
    tft.drawString(categories[currentCategory], 160, 15);
    
    const char** tests;
    int count;
    
    // Select appropriate test list
    switch(currentCategory) {
        case 0: tests = heartTests; count = 4; break;
        case 1: tests = fitnessTests; count = 4; break;
        case 2: tests = stabilityTests; count = 4; break;
        case 3: tests = dexterityTests; count = 4; break;
        default: return;
    }
    
    // Draw tests
    for(int i = 0; i < count; i++) {
        int y = 40 + i * 25;
        bool selected = (i == currentTest);
        
        if(selected) {
            tft.fillRoundRect(20, y-2, 280, 22, 4, TEST_ACCENT);
            tft.setTextColor(TEST_TEXT);
        } else {
            tft.setTextColor(TEST_SUBTEXT);
        }
        
        tft.setTextDatum(ML_DATUM);
        tft.drawString(tests[i], 30, y + 9);
    }
    
    // Footer hint
    tft.setTextColor(TEST_SUBTEXT);
    tft.setTextDatum(BC_DATUM);
    tft.drawString("Scroll: Next   Enter: Select", 160, 165);
}

void drawTestInstructions(const char* title, const char* instructions) {
    tft.fillScreen(TEST_BG);
    
    // Header
    tft.fillRect(0, 0, 320, 30, TEST_CARD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TEST_TEXT);
    tft.drawString(title, 160, 15);
    
    // Instructions
    tft.setTextColor(TEST_TEXT);
    tft.setTextDatum(MC_DATUM);
    
    // Split instructions into multiple lines if needed
    String str = instructions;
    int y = 60;
    while (str.length() > 0) {
        int space = str.indexOf(' ', 20);
        if (space == -1) space = str.length();
        String line = str.substring(0, space);
        tft.drawString(line, 160, y);
        str = str.substring(space + 1);
        y += 20;
    }
    
    // Start button
    tft.fillRoundRect(100, 120, 120, 30, 4, TEST_SUCCESS);
    tft.setTextColor(TEST_TEXT);
    tft.drawString("Start Test", 160, 135);
}

void drawTestProgress(const char* title, int progress) {
    static int lastProgress = -1;
    if(progress == lastProgress) return;
    lastProgress = progress;
    
    // Only redraw what's needed
    if(progress == 0) {
        tft.fillScreen(TEST_BG);
        
        // Header
        tft.fillRect(0, 0, 320, 30, TEST_CARD);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TEST_TEXT);
        tft.drawString(title, 160, 15);
    }
    
    // Progress bar background
    tft.fillRect(40, 80, 240, 20, TEST_CARD);
    
    // Progress bar fill
    int fillWidth = (240 - 4) * progress / 100;
    tft.fillRect(42, 82, fillWidth, 16, TEST_ACCENT);
    
    // Progress text
    tft.setTextColor(TEST_TEXT);
    tft.drawString(String(progress) + "%", 160, 120);
}

void drawTestResults(const char* title, const char* result, bool success) {
    tft.fillScreen(TEST_BG);
    
    // Header
    tft.fillRect(0, 0, 320, 30, TEST_CARD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TEST_TEXT);
    tft.drawString("Test Complete", 160, 15);
    
    // Result card
    tft.fillRoundRect(20, 50, 280, 80, 8, TEST_CARD);
    
    // Title
    tft.setTextColor(TEST_SUBTEXT);
    tft.drawString(title, 160, 70);
    
    // Result
    tft.setTextColor(success ? TEST_SUCCESS : TEST_WARNING);
    tft.drawString(result, 160, 100);
    
    // Buttons
    tft.fillRoundRect(40, 150, 100, 30, 4, TEST_ACCENT);
    tft.fillRoundRect(180, 150, 100, 30, 4, TEST_CARD);
    
    tft.setTextColor(TEST_TEXT);
    tft.drawString("Retry", 90, 165);
    tft.setTextColor(TEST_SUBTEXT);
    tft.drawString("Done", 230, 165);
}

void handleTestInput() {
    if(readButton1()) { // Scroll
        if(!testRunning) {
            if(!inTestList) {
                currentCategory = (currentCategory + 1) % 5;
                drawTestMenu();
            } else {
                const int maxTests = 4; // All test lists have 4 items including Back
                currentTest = (currentTest + 1) % maxTests;
                drawTestList();
            }
        }
        delay(200);
    }
    
    if(readButton2()) { // Select
        if(!testRunning) {
            if(!inTestList) {
                if(currentCategory == 4) { // Back
                    return; // Return to main screen
                }
                inTestList = true;
                currentTest = 0;
                drawTestList();
            } else {
                const char** tests;
                switch(currentCategory) {
                    case 0: tests = heartTests; break;
                    case 1: tests = fitnessTests; break;
                    case 2: tests = stabilityTests; break;
                    case 3: tests = dexterityTests; break;
                    default: return;
                }
                
                if(currentTest == 3) { // Back
                    inTestList = false;
                    drawTestMenu();
                } else {
                    // Start the selected test
                    testRunning = true;
                    runTest(currentCategory, currentTest);
                }
            }
        }
        delay(200);
    }
}

void showTestMenu() {
    currentCategory = 0;
    currentTest = 0;
    inTestList = false;
    testRunning = false;
    drawTestMenu();
    handleTestInput();
}

// Main test execution function
void runTest(int category, int test) {
    const char* testName = "";
    const char* instructions = "";
    
    switch(category) {
        case 0: // Heart Tests
            testName = heartTests[test];
            switch(test) {
                case 0: // Heart Rate
                    instructions = "Place your finger gently on the sensor. Keep still until the test completes.";
                    break;
                case 1: // HRV
                    instructions = "Place your finger on the sensor. Breathe normally for 60 seconds.";
                    break;
                case 2: // Continuous
                    instructions = "Wear the device normally. Monitoring will run in background.";
                    break;
            }
            break;
            
        case 1: // Fitness Tests
            testName = fitnessTests[test];
            switch(test) {
                case 0: // Step Counter
                    instructions = "Walk normally for 30 seconds. Device will count your steps.";
                    break;
                case 1: // Activity
                    instructions = "Perform your normal activities. Test runs for 2 minutes.";
                    break;
                case 2: // Walking
                    instructions = "Walk at a comfortable pace for 1 minute.";
                    break;
            }
            break;
            
        case 2: // Stability Tests
            testName = stabilityTests[test];
            switch(test) {
                case 0: // Balance
                    instructions = "Stand still with feet shoulder-width apart. Hold position for 30 seconds.";
                    break;
                case 1: // Fall Detection
                    instructions = "System will monitor for sudden movements and potential falls.";
                    break;
                case 2: // Posture
                    instructions = "Sit or stand normally. Device will analyze your posture for 30 seconds.";
                    break;
            }
            break;
            
        case 3: // Dexterity Tests
            testName = dexterityTests[test];
            switch(test) {
                case 0: // Reaction
                    instructions = "Touch the sensor quickly when you see the green light.";
                    break;
                case 1: // Pattern
                    instructions = "Follow the pattern of touches shown on screen.";
                    break;
                case 2: // Grip
                    instructions = "Hold the touch sensor continuously as long as you can.";
                    break;
            }
            break;
    }
    
    // Show instructions
    drawTestInstructions(testName, instructions);
    
    // Wait for start button
    while(!readButton2()) delay(50);
    
    // Run the actual test
    int duration = 30; // Default duration in seconds
    bool success = true;
    String result = "";
    
    // Show progress
    for(int i = 0; i <= 100; i++) {
        drawTestProgress(testName, i);
        delay(duration * 10); // Spread over test duration
        
        // Check for early exit
        if(readButton1()) {
            success = false;
            break;
        }
    }
    
    // Get test results based on category and test
    switch(category) {
        case 0: // Heart Tests
            if(test == 0) {
                float bpm = readHeartRate();
                result = String(bpm, 0) + " BPM";
                success = (bpm >= 60 && bpm <= 100);
            }
            break;
        case 1: // Fitness Tests
            if(test == 0) {
                int steps = readSteps();
                result = String(steps) + " steps";
                success = (steps > 0);
            }
            break;
        // Add other test result handling
    }
    
    // Show results
    drawTestResults(testName, result.c_str(), success);
    
    // Store result
    addTestResult(categories[category], testName, result.c_str());
    
    // Wait for button press
    while(!readButton2()) delay(50);
    
    // Return to test list
    testRunning = false;
    drawTestList();
}
