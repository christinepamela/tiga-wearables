// ============================================================
// TIGA — Main Application v1.1
// Fix: no full screen wipe = no flicker
//      Only redraw card values when data changes
//      Pulse threshold tuned for breadboard environment
// ============================================================

#include <TFT_eSPI.h>
#include <Wire.h>
#include <MPU6050.h>

// ── Pins ─────────────────────────────────────────────────────
#define PULSE_PIN     1
#define SW420_PIN     3
#define TTP223_PIN   13
#define BUTTON1_PIN  21
#define BUTTON2_PIN  16
#define BAT_ADC_PIN   4
#define LCD_PWR_PIN  15
#define I2C_SDA      18
#define I2C_SCL      17
#define TFT_BL       38

// ── Display ──────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);  // sprite for flicker-free draws
#define W 320
#define H 170

// ── Colours ──────────────────────────────────────────────────
#define C_BG     0x0000
#define C_CARD   0x18E3
#define C_TEXT   0xFFFF
#define C_MUTED  0x8410
#define C_ACCENT 0x051D
#define C_GREEN  0x0680
#define C_ORANGE 0xFD20
#define C_RED    0xF800
#define C_YELLOW 0xFFE0

// ── Thresholds ───────────────────────────────────────────────
#define HR_SAFE_MIN   50
#define HR_SAFE_MAX  100
#define HR_WARN_LOW   45
#define HR_WARN_HIGH 110
#define STEPS_GOAL  3000
#define FALL_G       2.5f
#define STABLE_G     1.3f

// ── State ────────────────────────────────────────────────────
enum AppState { STATE_MAIN, STATE_EMERGENCY, STATE_MENU, STATE_DETAIL };
AppState state    = STATE_MAIN;
AppState lastState = STATE_EMERGENCY;  // force full draw on first run
bool needsFullDraw = true;

// ── Health data ──────────────────────────────────────────────
struct HealthData {
  float heartRate   = 0;
  int   steps       = 0;
  float accelG      = 1.0f;
  bool  isStable    = true;
  bool  fallDetected = false;
  float battery     = 100.0f;
  int   healthScore = 85;
  bool  wearing     = false;
  float tempC       = 33.0f;
};
HealthData data;

// Track previous values to avoid unnecessary redraws
struct PrevData {
  float heartRate = -1;
  int   steps     = -1;
  bool  isStable  = true;
  float tempC     = -1;
  int   healthScore = -1;
  bool  wearing   = false;
  float battery   = -1;
} prev;

// ── Sensors ──────────────────────────────────────────────────
MPU6050 mpu;

// Pulse
#define PULSE_SAMPLES 30
float   pulseBuf[PULSE_SAMPLES];
int     pulseIdx     = 0;
float   pulseBaseline = 2000;
float   pulseThreshold = 0;  // set after calibration
unsigned long lastBeat = 0;
float   lastBPM = 0;
int     beatCount = 0;

// Buttons
bool btn1Last = false, btn2Last = false;
bool btn1Pressed = false, btn2Pressed = false;

// Menu
const char* menuItems[] = {
  "Heart Detail", "Fitness", "Stability",
  "Dexterity", "SOS", "Back"
};
#define MENU_COUNT 6
int menuSel = 0;

// Emergency flash
unsigned long lastFlash = 0;
bool flashOn = true;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(LCD_PWR_PIN, OUTPUT);
  digitalWrite(LCD_PWR_PIN, HIGH);
  pinMode(SW420_PIN,   INPUT);
  pinMode(TTP223_PIN,  INPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(C_BG);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  drawSplash();
  delay(2500);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(200);
  mpu.initialize();
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_4);
  mpu.setDLPFMode(MPU6050_DLPF_BW_20);

  // Calibrate pulse: read 100 samples to find baseline
  Serial.println("[TIGA] Calibrating pulse sensor...");
  long sum = 0;
  int mn = 4095, mx = 0;
  for (int i = 0; i < 100; i++) {
    int v = analogRead(PULSE_PIN);
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delay(10);
  }
  pulseBaseline  = sum / 100.0f;
  // Threshold = baseline + 40% of the ambient range seen at rest
  // Gives a gentler threshold than a fixed 300 offset
  pulseThreshold = pulseBaseline + max(150.0f, (mx - mn) * 2.0f);
  for (int i = 0; i < PULSE_SAMPLES; i++) pulseBuf[i] = pulseBaseline;

  Serial.printf("[TIGA] Pulse baseline:%.0f  threshold:%.0f\n",
                pulseBaseline, pulseThreshold);

  needsFullDraw = true;
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  readButtons();
  handleInput();

  static unsigned long lastSensor = 0;
  if (millis() - lastSensor >= 100) {
    lastSensor = millis();
    readSensors();
  }

  // State changed → full redraw
  if (state != lastState) {
    needsFullDraw = true;
    lastState = state;
  }

  if (needsFullDraw) {
    drawScreenFull();
    needsFullDraw = false;
    copyPrev();
    return;
  }

  // Main screen: partial updates every second
  static unsigned long lastUpdate = 0;
  if (state == STATE_MAIN && millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    drawMainPartial();
    copyPrev();
  }

  // Emergency: flash every 600ms
  if (state == STATE_EMERGENCY && millis() - lastFlash > 600) {
    lastFlash = millis();
    flashOn = !flashOn;
    drawEmergency();
  }

  delay(20);
}

void copyPrev() {
  prev.heartRate   = data.heartRate;
  prev.steps       = data.steps;
  prev.isStable    = data.isStable;
  prev.tempC       = data.tempC;
  prev.healthScore = data.healthScore;
  prev.wearing     = data.wearing;
  prev.battery     = data.battery;
}

// ============================================================
// SENSORS
// ============================================================
void readSensors() {
  // ── Pulse ─────────────────────────────────────────────────
  int rawPulse = analogRead(PULSE_PIN);
  pulseBuf[pulseIdx] = rawPulse;
  pulseIdx = (pulseIdx + 1) % PULSE_SAMPLES;

  // Rising edge detection above dynamic threshold
  static bool wasAbove = false;
  bool above = (rawPulse > pulseThreshold);
  if (above && !wasAbove && millis() - lastBeat > 300) {
    unsigned long now = millis();
    if (lastBeat > 0) {
      float interval = now - lastBeat;
      float bpm = 60000.0f / interval;
      if (bpm >= 40 && bpm <= 180) {
        lastBPM = lastBPM * 0.6f + bpm * 0.4f;
        beatCount++;
      }
    }
    lastBeat = now;
  }
  wasAbove = above;
  if (millis() - lastBeat > 6000) { lastBPM = 0; beatCount = 0; }
  data.heartRate = lastBPM;

  // ── MPU6050 ───────────────────────────────────────────────
  static int   stepCount  = 0;
  static float lastG      = 1.0f;
  static unsigned long lastStep = 0;
  static bool  inFall     = false;
  static unsigned long fallTime = 0;

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float g = sqrtf((float)ax*ax + (float)ay*ay + (float)az*az) / 8192.0f;
  data.accelG = g;

  // Fall: high G then sudden stillness
  if (g > FALL_G && !inFall) { inFall = true; fallTime = millis(); }
  if (inFall && millis() - fallTime > 250) {
    if (g < 0.5f) {
      data.fallDetected = true;
      state = STATE_EMERGENCY;
      needsFullDraw = true;
    }
    inFall = false;
  }

  // Steps
  if (g > 1.25f && lastG <= 1.25f && millis() - lastStep > 350) {
    stepCount++;
    lastStep = millis();
  }
  lastG = g;
  data.steps    = stepCount;
  data.isStable = (g < STABLE_G);

  // Temperature from MPU die sensor
  int16_t tempRaw = mpu.getTemperature();
  data.tempC = (tempRaw / 340.0f) + 36.53f;

  // Wearing (TTP223)
  data.wearing = digitalRead(TTP223_PIN);

  // Battery (every 30s)
  static unsigned long lastBat = 0;
  if (millis() - lastBat > 30000) {
    lastBat = millis();
    int r = analogRead(BAT_ADC_PIN);
    float v = (r / 4095.0f) * 3.3f * 2.0f;
    data.battery = constrain((v - 3.2f) / (4.2f - 3.2f) * 100.0f, 0, 100);
  }

  data.healthScore = calcScore();

  // HR alert
  if (data.heartRate > 0 && state == STATE_MAIN) {
    if (data.heartRate > HR_WARN_HIGH || data.heartRate < HR_WARN_LOW) {
      state = STATE_EMERGENCY;
      needsFullDraw = true;
    }
  }
}

int calcScore() {
  int hr = 50, act = 50, stab = 50;
  if (data.heartRate > 0) {
    if (data.heartRate >= HR_SAFE_MIN && data.heartRate <= HR_SAFE_MAX) hr = 100;
    else if (data.heartRate >= HR_WARN_LOW && data.heartRate <= HR_WARN_HIGH) hr = 65;
    else hr = 20;
  }
  act  = (int)(min((float)data.steps / STEPS_GOAL, 1.0f) * 100);
  stab = data.isStable ? 100 : 40;
  return (int)(hr * 0.35f + act * 0.25f + stab * 0.40f);
}

// ============================================================
// BUTTONS
// ============================================================
void readButtons() {
  bool b1 = (digitalRead(BUTTON1_PIN) == LOW);
  bool b2 = (digitalRead(BUTTON2_PIN) == LOW);
  btn1Pressed = (b1 && !btn1Last);
  btn2Pressed = (b2 && !btn2Last);
  btn1Last = b1; btn2Last = b2;
}

void handleInput() {
  if (!btn1Pressed && !btn2Pressed) return;
  switch (state) {
    case STATE_MAIN:
      if (btn1Pressed || btn2Pressed) {
        state = STATE_MENU; menuSel = 0; needsFullDraw = true;
      }
      break;
    case STATE_MENU:
      if (btn1Pressed) { menuSel = (menuSel + 1) % MENU_COUNT; needsFullDraw = true; }
      if (btn2Pressed) {
        if      (menuSel == MENU_COUNT - 1) state = STATE_MAIN;
        else if (menuSel == 4)              { state = STATE_EMERGENCY; data.fallDetected = false; }
        else                                 state = STATE_DETAIL;
        needsFullDraw = true;
      }
      break;
    case STATE_DETAIL:
      if (btn1Pressed || btn2Pressed) { state = STATE_MENU; needsFullDraw = true; }
      break;
    case STATE_EMERGENCY:
      if (btn2Pressed) { data.fallDetected = false; state = STATE_MAIN; needsFullDraw = true; }
      break;
  }
}

// ============================================================
// DRAWING — full redraws (on state change only)
// ============================================================
void drawScreenFull() {
  tft.fillScreen(C_BG);
  switch (state) {
    case STATE_MAIN:      drawMainFull();  break;
    case STATE_MENU:      drawMenu();      break;
    case STATE_DETAIL:    drawDetail();    break;
    case STATE_EMERGENCY: drawEmergency(); break;
  }
}

// ── Splash ────────────────────────────────────────────────────
void drawSplash() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.setTextSize(4);
  tft.drawString("tiga", W/2, H/2 - 15);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString("health tracker for mom", W/2, H/2 + 20);
}

// ── Card layout helpers ───────────────────────────────────────
// Card positions (computed once)
int cx = 4, cy = 26;
int cw, ch;

void initCardLayout() {
  cw = (W - 12) / 2;
  ch = (H - cy - 18) / 2;
}

// Draws a card's static parts (label, border) — called on full draw
void drawCardFrame(int x, int y, int w, int h, const char* label) {
  tft.fillRect(x, y, w, h, C_CARD);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString(label, x + 6, y + 10);
}

// Draws only the value area of a card (no background wipe of label)
void drawCardValue(int x, int y, int w, int h,
                   const char* value, uint16_t col) {
  // Wipe value area only (below label)
  tft.fillRect(x + 1, y + 16, w - 2, h - 17, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(col);
  tft.drawString(value, x + w/2, y + h/2 + 6);
}

void drawCardValueWithBar(int x, int y, int w, int h,
                          const char* value, uint16_t col, float progress) {
  tft.fillRect(x + 1, y + 16, w - 2, h - 17, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(col);
  tft.drawString(value, x + w/2, y + h/2 + 2);
  int bx = x + 4, by = y + h - 10, bw = w - 8, bh = 5;
  tft.fillRect(bx, by, bw, bh, C_BG);
  tft.fillRect(bx, by, (int)(bw * progress), bh, col);
}

// ── Header bar (static + dynamic parts separated) ────────────
void drawHeader() {
  tft.fillRect(0, 0, W, 22, C_CARD);
}

void drawHeaderValues() {
  // Wipe dynamic zones
  tft.fillRect(1, 1, 90, 20, C_CARD);      // left: wearing
  tft.fillRect(100, 1, 120, 20, C_CARD);   // centre: score
  tft.fillRect(260, 1, 58, 20, C_CARD);    // right: battery

  tft.setTextSize(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(data.wearing ? C_GREEN : C_MUTED);
  tft.drawString(data.wearing ? "wearing" : "not worn", 6, 11);

  char scoreStr[12];
  sprintf(scoreStr, "score %d", data.healthScore);
  uint16_t sc = data.healthScore >= 75 ? C_GREEN :
                data.healthScore >= 50 ? C_ORANGE : C_RED;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(sc);
  tft.drawString(scoreStr, W/2, 11);

  char batStr[8];
  sprintf(batStr, "%.0f%%", data.battery);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(data.battery < 20 ? C_ORANGE : C_MUTED);
  tft.drawString(batStr, W - 6, 11);
}

void drawFooter() {
  tft.fillRect(0, H - 14, W, 14, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.drawString("BTN1: menu   BTN2: menu", W/2, H - 7);
}

// ── Full main draw (on state entry) ──────────────────────────
void drawMainFull() {
  initCardLayout();
  int gap = 4;
  drawHeader();
  drawHeaderValues();
  // Draw all 4 card frames
  drawCardFrame(cx,            cy,            cw, ch, "HEART");
  drawCardFrame(cx + cw + gap, cy,            cw, ch, "STEPS");
  drawCardFrame(cx,            cy + ch + gap, cw, ch, "STABILITY");
  drawCardFrame(cx + cw + gap, cy + ch + gap, cw, ch, "TEMP");
  drawFooter();
  // Then draw values
  drawAllCardValues();
}

void drawAllCardValues() {
  initCardLayout();
  int gap = 4;

  // Heart
  char hrStr[12];
  if (data.heartRate > 0) sprintf(hrStr, "%.0f bpm", data.heartRate);
  else strcpy(hrStr, "-- bpm");
  uint16_t hrCol = (data.heartRate > HR_SAFE_MAX ||
                   (data.heartRate < HR_SAFE_MIN && data.heartRate > 0))
                    ? C_ORANGE : C_TEXT;
  drawCardValue(cx, cy, cw, ch, hrStr, hrCol);

  // Steps
  char stepsStr[12];
  sprintf(stepsStr, "%d", data.steps);
  float prog = min((float)data.steps / STEPS_GOAL, 1.0f);
  uint16_t stCol = prog >= 1.0f ? C_GREEN : prog >= 0.5f ? C_ACCENT : C_TEXT;
  drawCardValueWithBar(cx + cw + gap, cy, cw, ch, stepsStr, stCol, prog);

  // Stability
  drawCardValue(cx, cy + ch + gap, cw, ch,
                data.isStable ? "stable" : "moving",
                data.isStable ? C_GREEN : C_ORANGE);

  // Temp
  char tempStr[12];
  sprintf(tempStr, "%.1f C", data.tempC);
  drawCardValue(cx + cw + gap, cy + ch + gap, cw, ch, tempStr, C_ACCENT);
}

// ── Partial main update (every second — NO full wipe) ────────
void drawMainPartial() {
  initCardLayout();
  int gap = 4;
  bool headerChanged = false;

  // Only redraw cards whose values changed
  char hrStr[12];
  if (data.heartRate > 0) sprintf(hrStr, "%.0f bpm", data.heartRate);
  else strcpy(hrStr, "-- bpm");
  uint16_t hrCol = (data.heartRate > HR_SAFE_MAX ||
                   (data.heartRate < HR_SAFE_MIN && data.heartRate > 0))
                    ? C_ORANGE : C_TEXT;

  if ((int)data.heartRate != (int)prev.heartRate)
    drawCardValue(cx, cy, cw, ch, hrStr, hrCol);

  if (data.steps != prev.steps) {
    char stepsStr[12]; sprintf(stepsStr, "%d", data.steps);
    float prog = min((float)data.steps / STEPS_GOAL, 1.0f);
    uint16_t stCol = prog>=1.0f ? C_GREEN : prog>=0.5f ? C_ACCENT : C_TEXT;
    drawCardValueWithBar(cx + cw + gap, cy, cw, ch, stepsStr, stCol, prog);
  }

  if (data.isStable != prev.isStable)
    drawCardValue(cx, cy + ch + gap, cw, ch,
                  data.isStable ? "stable" : "moving",
                  data.isStable ? C_GREEN : C_ORANGE);

  if ((int)(data.tempC * 10) != (int)(prev.tempC * 10)) {
    char tempStr[12]; sprintf(tempStr, "%.1f C", data.tempC);
    drawCardValue(cx + cw + gap, cy + ch + gap, cw, ch, tempStr, C_ACCENT);
  }

  if (data.healthScore != prev.healthScore ||
      data.wearing     != prev.wearing     ||
      (int)data.battery != (int)prev.battery)
    drawHeaderValues();
}

// ── Menu ─────────────────────────────────────────────────────
void drawMenu() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, 22, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.drawString("TIGA  TEST MENU", W/2, 11);

  int itemH = 22, startY = 28;
  for (int i = 0; i < MENU_COUNT; i++) {
    bool sel = (i == menuSel);
    int y = startY + i * itemH;
    if (sel) { tft.fillRect(8, y, W-16, itemH-2, C_ACCENT); tft.setTextColor(C_BG); }
    else      { tft.setTextColor(C_TEXT); }
    tft.setTextDatum(ML_DATUM);
    tft.drawString(menuItems[i], 18, y + itemH/2);
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.drawString("BTN1:scroll  BTN2:select", W/2, H - 6);
}

// ── Detail ────────────────────────────────────────────────────
void drawDetail() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, 22, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.drawString(menuItems[menuSel], W/2, 11);

  switch (menuSel) {
    case 0: {  // Heart
      char hrStr[12];
      if (data.heartRate > 0) sprintf(hrStr, "%.0f", data.heartRate);
      else strcpy(hrStr, "--");
      tft.setTextColor(C_ACCENT); tft.setTextSize(3);
      tft.drawString(hrStr, W/2, 72);
      tft.setTextSize(1); tft.setTextColor(C_MUTED);
      tft.drawString("beats per minute", W/2, 108);
      char rng[32];
      sprintf(rng, "safe range: %d - %d bpm", HR_SAFE_MIN, HR_SAFE_MAX);
      tft.drawString(rng, W/2, 126);
      break;
    }
    case 1: {  // Fitness
      char s1[32], s2[32];
      sprintf(s1, "Steps today: %d / %d", data.steps, STEPS_GOAL);
      float pct = min((float)data.steps/STEPS_GOAL,1.0f)*100;
      sprintf(s2, "%.0f%% of daily goal", pct);
      tft.setTextColor(C_TEXT); tft.drawString(s1, W/2, 65);
      tft.setTextColor(C_ACCENT); tft.drawString(s2, W/2, 90);
      int bw = W-60;
      tft.fillRect(30, 110, bw, 8, C_CARD);
      tft.fillRect(30, 110, (int)(bw*min((float)data.steps/STEPS_GOAL,1.0f)), 8, C_GREEN);
      break;
    }
    case 2: {  // Stability
      tft.setTextColor(data.isStable ? C_GREEN : C_ORANGE);
      tft.setTextSize(2);
      tft.drawString(data.isStable ? "STABLE" : "MOVING", W/2, 70);
      tft.setTextSize(1); tft.setTextColor(C_MUTED);
      char gStr[20]; sprintf(gStr, "accel: %.2f G", data.accelG);
      tft.drawString(gStr, W/2, 100);
      tft.drawString("fall detection: active", W/2, 118);
      break;
    }
    case 3: {  // Dexterity — live tap test
      tft.setTextColor(C_MUTED);
      tft.drawString("Tap the TTP223 as fast as you can", W/2, 55);
      tft.drawString("5 seconds starting now!", W/2, 72);
      int taps = 0;
      bool touching = false;
      unsigned long t = millis();
      while (millis()-t < 5000) {
        bool cur = digitalRead(TTP223_PIN);
        if (cur && !touching) { taps++; touching = true; }
        if (!cur) touching = false;
        tft.fillRect(100, 90, 120, 36, C_BG);
        tft.setTextSize(3); tft.setTextColor(C_ACCENT);
        char ts[6]; sprintf(ts, "%d", taps);
        tft.drawString(ts, W/2, 108);
        tft.setTextSize(1);
        delay(30);
      }
      tft.setTextColor(C_MUTED);
      char res[32]; sprintf(res, "Score: %d taps in 5s", taps);
      tft.drawString(res, W/2, 148);
      delay(2500);
      state = STATE_MENU;
      needsFullDraw = true;
      break;
    }
    default:
      tft.setTextColor(C_MUTED);
      tft.drawString("Coming soon in next build", W/2, 85);
      break;
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.drawString("any button: back", W/2, H - 6);
}

// ── Emergency ─────────────────────────────────────────────────
void drawEmergency() {
  tft.fillScreen(flashOn ? C_RED : 0x7000);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);

  if (data.fallDetected) {
    tft.setTextSize(3); tft.drawString("FALL",     W/2, 45);
    tft.setTextSize(2); tft.drawString("DETECTED", W/2, 80);
  } else if (data.heartRate > HR_WARN_HIGH) {
    tft.setTextSize(2);
    tft.drawString("HIGH HEART", W/2, 45);
    tft.drawString("RATE",       W/2, 70);
    char s[16]; sprintf(s, "%.0f bpm", data.heartRate);
    tft.setTextSize(1); tft.setTextColor(C_YELLOW);
    tft.drawString(s, W/2, 100);
  } else if (data.heartRate > 0 && data.heartRate < HR_WARN_LOW) {
    tft.setTextSize(2);
    tft.drawString("LOW HEART", W/2, 45);
    tft.drawString("RATE",      W/2, 70);
    char s[16]; sprintf(s, "%.0f bpm", data.heartRate);
    tft.setTextSize(1); tft.setTextColor(C_YELLOW);
    tft.drawString(s, W/2, 100);
  } else {
    tft.setTextSize(3); tft.drawString("SOS", W/2, 55);
  }

  tft.setTextSize(1); tft.setTextColor(C_TEXT);
  tft.drawString("Press BTN2 to dismiss", W/2, H - 12);
}
