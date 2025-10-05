#include <TFT_eSPI.h>
#include <SPI.h>
#include "fonts.h"
#include "icons.h"
#include "sensors.h"
#include "tests.h"
#include "bitchat.h"
#include "draw_functions.h"

TFT_eSPI tft = TFT_eSPI();
bool needsRedraw = true;

// Application state
enum AppState {
    STATE_SPLASH,
    STATE_MAIN,
    STATE_TEST_MENU,
    STATE_BITCHAT,
    STATE_SOS
};

AppState currentState = STATE_SPLASH;
int selectedMenuItem = 0;

// Sensor data structure
struct SensorData {
    float heartRate = 75.0;
    int steps = 0;
    float temp = 36.5;
    bool isStable = true;
    int healthScore = 85;
    float battery = 100.0;
} sensorData;

void drawSplashScreen() {
    tft.fillScreen(COLOR_BG);
    
    // Date
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.drawString("25 Sep 2025", SCREEN_W/2, SCREEN_H/3);
    
    // Day
    tft.setTextSize(1);
    tft.setTextColor(COLOR_SUBTEXT);
    tft.drawString("Thursday", SCREEN_W/2, SCREEN_H/3 + 25);
    
    // App name with custom logo
    drawTigaLogo(tft, SCREEN_W/2 - 50, SCREEN_H*2/3 - 20, COLOR_ACCENT);
    
    delay(2000);
    currentState = STATE_MAIN;
    needsRedraw = true;
}

void updateSensorValues() {
    static unsigned long lastUpdate = 0;
    if(millis() - lastUpdate < 1000) return;
    
    // Read all sensors
    sensorData.heartRate = readHeartRate();
    sensorData.steps = readSteps();
    sensorData.temp = readTemperature();
    sensorData.isStable = !detectFall();
    
    // Calculate health score based on all metrics
    float heartScore = (sensorData.heartRate >= 60 && sensorData.heartRate <= 100) ? 100 : 50;
    float tempScore = (sensorData.temp >= 36.0 && sensorData.temp <= 37.5) ? 100 : 50;
    float stabilityScore = sensorData.isStable ? 100 : 0;
    
    sensorData.healthScore = (heartScore + tempScore + stabilityScore) / 3;
    
    // Simulate battery drain
    if(sensorData.battery > 0) {
        sensorData.battery -= 0.01;
    }
    
    lastUpdate = millis();
    needsRedraw = true;
}

void handleButtons() {
    if(readButton1()) { // Scroll
        switch(currentState) {
            case STATE_MAIN:
                selectedMenuItem = (selectedMenuItem + 1) % 3;
                needsRedraw = true;
                break;
            case STATE_TEST_MENU:
                handleTestInput();
                break;
        }
        delay(200); // Simple debounce
    }
    
    if(readButton2()) { // Select
        switch(currentState) {
            case STATE_MAIN:
                switch(selectedMenuItem) {
                    case 0: // SOS
                        // No flashing, just a calm screen
                        currentState = STATE_SOS;
                        break;
                    case 1: // Tests
                        currentState = STATE_TEST_MENU;
                        break;
                    case 2: // Bitchat
                        currentState = STATE_BITCHAT;
                        break;
                }
                break;
                
            case STATE_SOS:
            case STATE_BITCHAT:
                currentState = STATE_MAIN;
                break;
                
            case STATE_TEST_MENU:
                handleTestInput();
                break;
        }
        needsRedraw = true;
        delay(200); // Simple debounce
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize display
    tft.init();
    tft.setRotation(1); // Landscape
    tft.setTextFont(2);
    
    // Initialize all subsystems
    initSensors();
    initBitchat();
    
    currentState = STATE_SPLASH;
    needsRedraw = true;
}

void loop() {
    updateSensorValues();
    handleButtons();
    handleBitchat();
    
    switch(currentState) {
        case STATE_SPLASH:
            drawSplashScreen();
            break;
            
        case STATE_MAIN:
            updateMainScreen();
            break;
            
        case STATE_TEST_MENU:
            showTestMenu();
            break;
            
        case STATE_BITCHAT:
            drawBitchatScreen(tft);
            break;
            
        case STATE_SOS:
            // Simple, non-flashing SOS screen
            tft.fillScreen(COLOR_BG);
            tft.setTextColor(COLOR_DANGER);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("Emergency Contact", SCREEN_W/2, SCREEN_H/3);
            tft.setTextColor(COLOR_TEXT);
            tft.drawString("Contacting family members", SCREEN_W/2, SCREEN_H/2);
            tft.setTextColor(COLOR_SUBTEXT);
            tft.drawString("Press any button to cancel", SCREEN_W/2, SCREEN_H*2/3);
            break;
    }
    
    delay(10);
}
