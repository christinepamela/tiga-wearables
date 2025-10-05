#include "bitchat.h"
#include "icons.h"

// Global variables
BitchatState bitchatState = BITCHAT_SCANNING;
BitchatMessage messages[MAX_MESSAGES];
int messageCount = 0;

// Modern UI colors (matching main theme)
const uint16_t CHAT_BG = 0x1082;        // Dark background
const uint16_t CHAT_CARD = 0x2104;      // Message bubble background
const uint16_t CHAT_TEXT = 0xFFFF;      // White text
const uint16_t CHAT_SUBTEXT = 0x8410;   // Gray text
const uint16_t CHAT_ACCENT = 0x05FF;    // Accent color for status

void initBitchat() {
    WiFi.mode(WIFI_STA);
    
    if (esp_now_init() != ESP_OK) {
        bitchatState = BITCHAT_ERROR;
        return;
    }
    
    esp_now_register_recv_cb((esp_now_recv_cb_t)onBitchatReceive);
    bitchatState = BITCHAT_SCANNING;
}

void drawBitchatScreen(TFT_eSPI& tft) {
    static unsigned long lastRedraw = 0;
    static BitchatState lastState = BITCHAT_ERROR;
    
    // Only redraw if state changed or 1 second passed
    if (bitchatState == lastState && millis() - lastRedraw < 1000) {
        return;
    }
    
    lastState = bitchatState;
    lastRedraw = millis();
    
    // Clear screen
    tft.fillScreen(CHAT_BG);
    
    // Draw header
    tft.fillRect(0, 0, 320, 30, CHAT_CARD);
    tft.setTextDatum(MC_DATUM);
    
    // Status text and color based on state
    const char* statusText;
    uint16_t statusColor;
    
    switch(bitchatState) {
        case BITCHAT_SCANNING:
            statusText = "Looking for phone...";
            statusColor = CHAT_SUBTEXT;
            break;
        case BITCHAT_CONNECTING:
            statusText = "Connecting...";
            statusColor = CHAT_ACCENT;
            break;
        case BITCHAT_CONNECTED:
            statusText = "Connected";
            statusColor = 0x07E0; // Green
            break;
        case BITCHAT_ERROR:
            statusText = "Connection Error";
            statusColor = 0xF800; // Red
            break;
    }
    
    tft.setTextColor(statusColor);
    tft.drawString(statusText, 160, 15);
    
    // If not connected, show QR code placeholder
    if (bitchatState != BITCHAT_CONNECTED) {
        tft.setTextColor(CHAT_SUBTEXT);
        tft.drawString("Install Bitchat app", 160, 50);
        tft.drawString("Scan QR code to connect", 160, 70);
        
        // QR code area
        tft.drawRect(110, 90, 100, 100, CHAT_SUBTEXT);
        tft.drawString("QR Code", 160, 140);
        return;
    }
    
    // Draw messages
    int y = 130;
    for (int i = messageCount - 1; i >= 0 && i >= messageCount - 3; i--) {
        BitchatMessage& msg = messages[i];
        
        // Message bubble
        int bubbleW = tft.textWidth(msg.text) + 20;
        int bubbleX = msg.isIncoming ? 10 : (320 - bubbleW - 10);
        
        tft.fillRoundRect(bubbleX, y, bubbleW, 40, 8, CHAT_CARD);
        
        // Message text
        tft.setTextColor(CHAT_TEXT);
        tft.drawString(msg.text, bubbleX + bubbleW/2, y + 12);
        
        // Time
        unsigned long ago = (millis() - msg.timestamp) / 1000;
        char timeStr[16];
        if (ago < 60) {
            snprintf(timeStr, sizeof(timeStr), "%lus ago", ago);
        } else {
            snprintf(timeStr, sizeof(timeStr), "%lum ago", ago/60);
        }
        
        tft.setTextColor(CHAT_SUBTEXT);
        tft.drawString(timeStr, bubbleX + bubbleW/2, y + 28);
        
        y -= 45;
    }
    
    // Draw "Back" button
    tft.fillRoundRect(120, 140, 80, 25, 4, CHAT_CARD);
    tft.setTextColor(CHAT_TEXT);
    tft.drawString("Back", 160, 152);
}

void handleBitchat() {
    static unsigned long lastScan = 0;
    
    // Periodic scanning when not connected
    if (bitchatState == BITCHAT_SCANNING && millis() - lastScan > 5000) {
        // Scan for Bitchat app
        // This would be implemented with actual ESP-NOW peer scanning
        lastScan = millis();
    }
}

bool isBitchatConnected() {
    return bitchatState == BITCHAT_CONNECTED;
}

void sendBitchatMessage(const char* msg) {
    if (messageCount >= MAX_MESSAGES) {
        // Shift messages up
        for (int i = 0; i < MAX_MESSAGES-1; i++) {
            messages[i] = messages[i+1];
        }
        messageCount = MAX_MESSAGES-1;
    }
    
    // Add new message
    strncpy(messages[messageCount].text, msg, 127);
    messages[messageCount].timestamp = millis();
    messages[messageCount].isIncoming = false;
    messageCount++;
}

void onBitchatReceive(const esp_now_recv_info_t* esp_now_info, const uint8_t* data, int len) {
    if (len > 127) len = 127;
    
    if (messageCount >= MAX_MESSAGES) {
        // Shift messages up
        for (int i = 0; i < MAX_MESSAGES-1; i++) {
            messages[i] = messages[i+1];
        }
        messageCount = MAX_MESSAGES-1;
    }
    
    // Add received message
    memcpy(messages[messageCount].text, data, len);
    messages[messageCount].text[len] = 0; // Null terminate
    messages[messageCount].timestamp = millis();
    messages[messageCount].isIncoming = true;
    messageCount++;
    
    // Update state if needed
    if (bitchatState != BITCHAT_CONNECTED) {
        bitchatState = BITCHAT_CONNECTED;
    }
}
