void drawCard(int x, int y, int w, int h, const char* title, const char* value, 
             const uint16_t* icon, uint16_t color) {
    // Draw card background with rounded corners
    tft.fillRoundRect(x, y, w, h, CARD_RADIUS, COLOR_CARD);
    
    // Draw icon
    if (icon) {
        tft.pushImage(x + PADDING, y + (h-24)/2, 24, 24, icon);
    }
    
    // Draw title
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FONT_MEDIUM);
    tft.setTextColor(COLOR_SUBTEXT);
    tft.drawString(title, x + 24 + PADDING*2, y + PADDING);
    
    // Draw value
    tft.setTextSize(FONT_LARGE);
    tft.setTextColor(color);
    tft.drawString(value, x + 24 + PADDING*2, y + h - PADDING*2 - 8);
}

void updateMainScreen() {
    if (!needsRedraw) return;
    
    tft.fillScreen(COLOR_BG);
    drawHeader();
    drawHealthBar();
    
    // Calculate card layout
    int cardX = HEALTH_BAR_W + 10;
    int cardY = HEADER_H + 10;
    int cardW = (SCREEN_W - cardX - 20) / 2;
    int cardH = (SCREEN_H - HEADER_H - FOOTER_H - 30) / 2;
    
    // Convert sensor values to strings
    char heartRateStr[16], stepsStr[16], tempStr[16];
    char stabilityStr[16] = "Safe";
    
    floatToString(heartRateStr, sensorData.heartRate, 0);
    strcat(heartRateStr, " BPM");
    
    intToString(stepsStr, sensorData.steps);
    
    floatToString(tempStr, sensorData.temp, 1);
    strcat(tempStr, "°C");
    
    if (!sensorData.isStable) {
        strcpy(stabilityStr, "Warning");
    }
    
    // Draw cards
    drawCard(cardX, cardY, cardW-5, cardH, 
             "Heart Rate", heartRateStr,
             heartIcon, COLOR_DANGER);
             
    drawCard(cardX + cardW + 5, cardY, cardW-5, cardH,
             "Steps", stepsStr,
             stepsIcon, COLOR_SUCCESS);
             
    drawCard(cardX, cardY + cardH + 10, cardW-5, cardH,
             "Stability", stabilityStr,
             stabilityIcon, sensorData.isStable ? COLOR_SUCCESS : COLOR_WARNING);
             
    drawCard(cardX + cardW + 5, cardY + cardH + 10, cardW-5, cardH,
             "Temperature", tempStr,
             tempIcon, COLOR_ACCENT);
             
    drawFooter();
    
    needsRedraw = false;
}
