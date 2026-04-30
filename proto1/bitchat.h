#ifndef BITCHAT_H
#define BITCHAT_H

#include <esp_now.h>
#include <WiFi.h>
#include <TFT_eSPI.h>

// Bitchat states
enum BitchatState {
    BITCHAT_SCANNING,     // Looking for phone
    BITCHAT_CONNECTING,   // Establishing connection
    BITCHAT_CONNECTED,    // Active connection
    BITCHAT_ERROR        // Connection error
};

// Message structure
struct BitchatMessage {
    char text[128];
    unsigned long timestamp;
    bool isIncoming;
};

// Function declarations
void initBitchat();
void handleBitchat();
void drawBitchatScreen(TFT_eSPI& tft);
bool isBitchatConnected();
void sendBitchatMessage(const char* msg);
void onBitchatReceive(const esp_now_recv_info_t* esp_now_info, const uint8_t* data, int len);

// Maximum number of stored messages
#define MAX_MESSAGES 5

extern BitchatState bitchatState;
extern BitchatMessage messages[MAX_MESSAGES];
extern int messageCount;

#endif
