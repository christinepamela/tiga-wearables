// ============================================================
// TIGA — Main Application v1.0
// Wearable health tracker for elderly users
//
// Screen: 4-card grid + health score. Calm by default.
//         Red emergency screen only for falls or critical HR.
//
// Confirmed pin map (T-Display-S3):
//   MPU6050  SDA→18  SCL→17  VCC→3V  GND→GND
//   Pulse    S→1     +→3V    -→GND
//   SW-420   DO→3    VCC→3V  GND→GND
//   TTP223   IO→13   VCC→3V  GND→GND
//   Button1  GPIO21→GND (scroll)
//   Button2  GPIO16→GND (select)
//   Battery  GPIO04 (internal ADC)
//   LCD pwr  GPIO15 (must HIGH)
// ============================================================

#include <TFT_eSPI.h>
#include <Wire.h>
#include <MPU6050.h>  // Electronic Cats

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
#define W 320
#define H 170

// ── Colours (RGB565) ─────────────────────────────────────────
#define C_BG        0x0000   // Black
#define C_CARD      0x18E3   // Dark grey
#define C_TEXT      0xFFFF   // White
#define C_MUTED     0x8410   // Mid grey
#define C_ACCENT    0x051D   // Soft cyan  (~0,160,230)
#define C_GREEN     0x0680   // Soft green (~0,208,0)  -- was 0x07E0 too bright
#define C_ORANGE    0xFD20   // Orange
#define C_RED       0xF800   // Red
#define C_YELLOW    0xFFE0   // Yellow

// ── Health thresholds (CVD patient, 65+) ─────────────────────
#define HR_SAFE_MIN    50
#define HR_SAFE_MAX   100
#define HR_WARN_LOW    45
#define HR_WARN_HIGH  110
#define STEPS_GOAL   3000
#define FALL_G        2.5f   // G-force threshold for fall
#define STABLE_G      1.3f   // Above this = significant movement

// ── App states ───────────────────────────────────────────────
enum AppState {
  STATE_MAIN,
  STATE_EMERGENCY,
  STATE_MENU,
  STATE_DETAIL
};

// ── Live data ────────────────────────────────────────────────
struct HealthData {
  float   heartRate   = 0;
  int     steps       = 0;
  float   accelG      = 1.0f;
  bool    isStable    = true;
  bool    fallDetected = false;
  float   battery     = 100.0f;
  int     healthScore = 85;
  bool    wearing     = false;  // TTP223 skin contact
};

HealthData data;
AppState   state     = STATE_MAIN;
bool       needsDraw = true;

// ── Sensor objects ────────────────────────────────────────────
MPU6050 mpu;

// ── Pulse sensor (beat detection) ────────────────────────────
#define PULSE_SAMPLES 20
float   pulseBuf[PULSE_SAMPLES];
int     pulseIdx      = 0;
float   pulseBaseline = 2000;
unsigned long lastBeat = 0;
float   lastBPM        = 0;

// ── Button state ─────────────────────────────────────────────
bool btn1Last = false;
bool btn2Last = false;
bool btn1Pressed = false;
bool btn2Pressed = false;

// ── Menu ─────────────────────────────────────────────────────
const char* menuItems[] = {"Heart Detail", "Fitness", "Stability", "Dexterity", "SOS", "Back"};
#define MENU_COUNT 6
int menuSel = 0;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Power
  pinMode(LCD_PWR_PIN, OUTPUT);
  digitalWrite(LCD_PWR_PIN, HIGH);

  // GPIO
  pinMode(SW420_PIN,   INPUT);
  pinMode(TTP223_PIN,  INPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  // Display
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(C_BG);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Splash
  drawSplash();
  delay(2500);

  // I2C + MPU6050
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(200);
  mpu.initialize();
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_4);
  mpu.setDLPFMode(MPU6050_DLPF_BW_20);

  // Calibrate pulse baseline
  long sum = 0;
  for (int i = 0; i < 50; i++) { sum += analogRead(PULSE_PIN); delay(10); }
  pulseBaseline = sum / 50.0f;
  for (int i = 0; i < PULSE_SAMPLES; i++) pulseBuf[i] = pulseBaseline;

  needsDraw = true;
  Serial.println("[TIGA] Boot complete");
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

  if (needsDraw) {
    drawScreen();
    needsDraw = false;
  }

  // Force redraw main screen every second for live data
  static unsigned long lastRedraw = 0;
  if (state == STATE_MAIN && millis() - lastRedraw >= 1000) {
    lastRedraw = millis();
    needsDraw = true;
  }

  delay(20);
}

// ============================================================
// SENSOR READING
// ============================================================
void readSensors() {
  // ── Pulse sensor ─────────────────────────────────────────
  float raw = analogRead(PULSE_PIN);
  pulseBuf[pulseIdx] = raw;
  pulseIdx = (pulseIdx + 1) % PULSE_SAMPLES;

  float avg = 0;
  for (int i = 0; i < PULSE_SAMPLES; i++) avg += pulseBuf[i];
  avg /= PULSE_SAMPLES;

  float threshold = pulseBaseline + 300;
  if (raw > threshold && millis() - lastBeat > 300) {
    if (lastBeat > 0) {
      float interval = millis() - lastBeat;
      float bpm = 60000.0f / interval;
      if (bpm >= 40 && bpm <= 180) {
        lastBPM = bpm * 0.7f + lastBPM * 0.3f;  // smooth
      }
    }
    lastBeat = millis();
  }
  if (millis() - lastBeat > 5000) lastBPM = 0;  // no signal
  data.heartRate = lastBPM;

  // ── MPU6050 ──────────────────────────────────────────────
  static int stepCount = 0;
  static float lastG = 1.0f;
  static unsigned long lastStep = 0;
  static bool inFallCheck = false;
  static unsigned long fallTime = 0;

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float g = sqrtf((float)ax*ax + (float)ay*ay + (float)az*az) / 8192.0f;
  data.accelG = g;

  // Fall detection: high G spike followed by low G (lying still)
  if (g > FALL_G && !inFallCheck) {
    inFallCheck = true;
    fallTime = millis();
  }
  if (inFallCheck && millis() - fallTime > 200) {
    if (g < 0.5f) {
      data.fallDetected = true;
      state = STATE_EMERGENCY;
      needsDraw = true;
    }
    inFallCheck = false;
  }

  // Step detection
  if (g > 1.2f && lastG <= 1.2f && millis() - lastStep > 350) {
    stepCount++;
    lastStep = millis();
  }
  lastG = g;
  data.steps = stepCount;
  data.isStable = (g < STABLE_G);

  // ── SW-420 vibration ─────────────────────────────────────
  // Already factored into stability via accelG

  // ── TTP223 — wearing detection ────────────────────────────
  data.wearing = digitalRead(TTP223_PIN);

  // ── Battery ───────────────────────────────────────────────
  static unsigned long lastBat = 0;
  if (millis() - lastBat > 30000) {
    lastBat = millis();
    int raw2 = analogRead(BAT_ADC_PIN);
    float v = (raw2 / 4095.0f) * 3.3f * 2.0f;
    data.battery = constrain((v - 3.2f) / (4.2f - 3.2f) * 100.0f, 0, 100);
  }

  // ── Health score ─────────────────────────────────────────
  data.healthScore = calcHealthScore();

  // ── HR alert check ───────────────────────────────────────
  if (data.heartRate > 0) {
    if (data.heartRate > HR_WARN_HIGH || data.heartRate < HR_WARN_LOW) {
      if (state == STATE_MAIN) {
        state = STATE_EMERGENCY;
        needsDraw = true;
      }
    }
  }
}

int calcHealthScore() {
  int hr = 50, act = 50, stab = 50;

  if (data.heartRate > 0) {
    if (data.heartRate >= HR_SAFE_MIN && data.heartRate <= HR_SAFE_MAX) hr = 100;
    else if (data.heartRate >= HR_WARN_LOW && data.heartRate <= HR_WARN_HIGH) hr = 65;
    else hr = 20;
  }

  float stepPct = min((float)data.steps / STEPS_GOAL, 1.0f);
  act = (int)(stepPct * 100);

  stab = data.isStable ? 100 : 40;

  // Stability weighted heavily for elderly
  return (int)(hr * 0.35f + act * 0.25f + stab * 0.40f);
}

// ============================================================
// BUTTON READING
// ============================================================
void readButtons() {
  bool b1 = (digitalRead(BUTTON1_PIN) == LOW);
  bool b2 = (digitalRead(BUTTON2_PIN) == LOW);

  btn1Pressed = (b1 && !btn1Last);
  btn2Pressed = (b2 && !btn2Last);

  btn1Last = b1;
  btn2Last = b2;
}

void handleInput() {
  if (!btn1Pressed && !btn2Pressed) return;

  switch (state) {
    case STATE_MAIN:
      if (btn1Pressed) {                 // scroll → open menu
        state = STATE_MENU;
        menuSel = 0;
        needsDraw = true;
      }
      if (btn2Pressed) {                 // select → open menu too
        state = STATE_MENU;
        menuSel = 0;
        needsDraw = true;
      }
      break;

    case STATE_MENU:
      if (btn1Pressed) {
        menuSel = (menuSel + 1) % MENU_COUNT;
        needsDraw = true;
      }
      if (btn2Pressed) {
        if (menuSel == MENU_COUNT - 1) { // Back
          state = STATE_MAIN;
        } else if (menuSel == 4) {       // SOS
          state = STATE_EMERGENCY;
          data.fallDetected = false;     // manual SOS, not fall
        } else {
          state = STATE_DETAIL;
        }
        needsDraw = true;
      }
      break;

    case STATE_DETAIL:
      if (btn2Pressed || btn1Pressed) {
        state = STATE_MENU;
        needsDraw = true;
      }
      break;

    case STATE_EMERGENCY:
      if (btn2Pressed) {                 // confirm/dismiss
        data.fallDetected = false;
        state = STATE_MAIN;
        needsDraw = true;
      }
      break;
  }
}

// ============================================================
// DRAWING
// ============================================================
void drawScreen() {
  switch (state) {
    case STATE_MAIN:      drawMain();      break;
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
  tft.drawString("wearable health tracker", W/2, H/2 + 20);
}

// ── Main screen ──────────────────────────────────────────────
void drawMain() {
  tft.fillScreen(C_BG);

  // ── Header bar ─────────────────────────────────────────
  tft.fillRect(0, 0, W, 22, C_CARD);

  // Wearing indicator
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(data.wearing ? C_GREEN : C_MUTED);
  tft.drawString(data.wearing ? "● wearing" : "○ not worn", 6, 11);

  // Battery
  char batStr[12];
  sprintf(batStr, "%.0f%%", data.battery);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(data.battery < 20 ? C_ORANGE : C_MUTED);
  tft.drawString(batStr, W - 6, 11);

  // Health score — centre of header
  char scoreStr[16];
  sprintf(scoreStr, "score %d", data.healthScore);
  uint16_t scoreCol = data.healthScore >= 75 ? C_GREEN :
                      data.healthScore >= 50 ? C_ORANGE : C_RED;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(scoreCol);
  tft.drawString(scoreStr, W/2, 11);

  // ── 4 cards ────────────────────────────────────────────
  // Layout: 2 cols × 2 rows, with small gaps
  int cx = 4, cy = 26;
  int cw = (W - 12) / 2;   // ~154px each
  int ch = (H - cy - 18) / 2; // ~58px each
  int gap = 4;

  // Heart
  char hrStr[16];
  if (data.heartRate > 0) sprintf(hrStr, "%.0f bpm", data.heartRate);
  else strcpy(hrStr, "-- bpm");
  uint16_t hrCol = (data.heartRate > HR_SAFE_MAX || (data.heartRate < HR_SAFE_MIN && data.heartRate > 0))
                    ? C_ORANGE : C_TEXT;
  drawCard(cx,            cy,          cw, ch, "HEART", hrStr,    hrCol);

  // Steps
  char stepsStr[16];
  sprintf(stepsStr, "%d", data.steps);
  float prog = min((float)data.steps / STEPS_GOAL, 1.0f);
  uint16_t stCol = prog >= 1.0f ? C_GREEN : prog >= 0.5f ? C_ACCENT : C_TEXT;
  drawCardWithBar(cx + cw + gap, cy, cw, ch, "STEPS", stepsStr, stCol, prog);

  // Stability
  const char* stabStr = data.isStable ? "stable" : "moving";
  uint16_t stabCol = data.isStable ? C_GREEN : C_ORANGE;
  drawCard(cx,            cy + ch + gap, cw, ch, "STABILITY", stabStr, stabCol);

  // Temp (MPU die temp — ambient proxy)
  int16_t tempRaw = mpu.getTemperature();
  float tempC = (tempRaw / 340.0f) + 36.53f;
  char tempStr[12];
  sprintf(tempStr, "%.1f C", tempC);
  drawCard(cx + cw + gap, cy + ch + gap, cw, ch, "TEMP",  tempStr, C_ACCENT);

  // ── Footer hint ────────────────────────────────────────
  tft.fillRect(0, H - 14, W, 14, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.drawString("BTN1: menu   BTN2: menu", W/2, H - 7);
}

void drawCard(int x, int y, int w, int h,
              const char* label, const char* value, uint16_t valCol) {
  tft.fillRect(x, y, w, h, C_CARD);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString(label, x + 6, y + 10);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(valCol);
  tft.drawString(value, x + w/2, y + h/2 + 5);
}

void drawCardWithBar(int x, int y, int w, int h,
                     const char* label, const char* value,
                     uint16_t valCol, float progress) {
  drawCard(x, y, w, h, label, value, valCol);
  // Progress bar at bottom of card
  int bx = x + 4, by = y + h - 8, bw = w - 8, bh = 4;
  tft.fillRect(bx, by, bw, bh, C_BG);
  tft.fillRect(bx, by, (int)(bw * progress), bh, valCol);
}

// ── Menu screen ──────────────────────────────────────────────
void drawMenu() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, 22, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.drawString("TIGA  TEST MENU", W/2, 11);

  int itemH = 22;
  int startY = 28;
  for (int i = 0; i < MENU_COUNT; i++) {
    bool sel = (i == menuSel);
    int y = startY + i * itemH;
    if (sel) {
      tft.fillRect(8, y, W - 16, itemH - 2, C_ACCENT);
      tft.setTextColor(C_BG);
    } else {
      tft.setTextColor(C_TEXT);
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextSize(1);
    tft.drawString(menuItems[i], 18, y + (itemH/2));
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.drawString("BTN1:scroll  BTN2:select", W/2, H - 6);
}

// ── Detail screen (per menu item) ────────────────────────────
void drawDetail() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, 22, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.drawString(menuItems[menuSel], W/2, 11);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);

  switch(menuSel) {
    case 0: {  // Heart detail
      tft.setTextColor(C_ACCENT);
      tft.setTextSize(3);
      char hrStr[16];
      if (data.heartRate > 0) sprintf(hrStr, "%.0f", data.heartRate);
      else strcpy(hrStr, "--");
      tft.drawString(hrStr, W/2, 75);
      tft.setTextSize(1);
      tft.setTextColor(C_MUTED);
      tft.drawString("beats per minute", W/2, 108);
      tft.setTextColor(C_MUTED);
      char rangeStr[32];
      sprintf(rangeStr, "safe range: %d - %d bpm", HR_SAFE_MIN, HR_SAFE_MAX);
      tft.drawString(rangeStr, W/2, 125);
      break;
    }
    case 1: {  // Fitness
      char s1[32], s2[32];
      sprintf(s1, "Steps today: %d / %d", data.steps, STEPS_GOAL);
      sprintf(s2, "%.0f%% of daily goal", min((float)data.steps/STEPS_GOAL, 1.0f)*100);
      tft.setTextColor(C_TEXT); tft.drawString(s1, W/2, 65);
      tft.setTextColor(C_ACCENT); tft.drawString(s2, W/2, 90);
      // Progress bar
      int bw = W - 60;
      tft.fillRect(30, 110, bw, 8, C_CARD);
      tft.fillRect(30, 110, (int)(bw * min((float)data.steps/STEPS_GOAL,1.0f)), 8, C_GREEN);
      break;
    }
    case 2: {  // Stability
      tft.setTextColor(data.isStable ? C_GREEN : C_ORANGE);
      tft.setTextSize(2);
      tft.drawString(data.isStable ? "STABLE" : "MOVING", W/2, 70);
      tft.setTextSize(1);
      tft.setTextColor(C_MUTED);
      char gStr[24];
      sprintf(gStr, "accel: %.2f G", data.accelG);
      tft.drawString(gStr, W/2, 100);
      tft.drawString("fall detection: active", W/2, 118);
      break;
    }
    case 3: {  // Dexterity
      tft.setTextColor(C_TEXT);
      tft.drawString("Touch test: tap TTP223 quickly", W/2, 60);
      tft.drawString("Counting taps for 5 seconds...", W/2, 80);
      // Live tap count
      int taps = 0;
      bool touching = false;
      unsigned long t = millis();
      while (millis()-t < 5000) {
        bool cur = digitalRead(TTP223_PIN);
        if (cur && !touching) { taps++; touching = true; }
        if (!cur) touching = false;
        tft.fillRect(100, 100, 120, 30, C_BG);
        tft.setTextSize(3);
        tft.setTextColor(C_ACCENT);
        char tapStr[8]; sprintf(tapStr, "%d", taps);
        tft.drawString(tapStr, W/2, 115);
        tft.setTextSize(1);
        delay(30);
      }
      tft.setTextColor(C_MUTED);
      char result[32];
      sprintf(result, "Score: %d taps in 5s", taps);
      tft.drawString(result, W/2, 148);
      delay(2000);
      state = STATE_MENU;
      needsDraw = true;
      break;
    }
    default:
      tft.setTextColor(C_MUTED);
      tft.drawString("Coming soon", W/2, 85);
      break;
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.drawString("any button: back", W/2, H - 6);
}

// ── Emergency screen ─────────────────────────────────────────
void drawEmergency() {
  // Flash red — but only draw once (loop handles flash)
  static unsigned long lastFlash = 0;
  static bool flashOn = true;

  if (millis() - lastFlash > 600) {
    lastFlash = millis();
    flashOn = !flashOn;
    needsDraw = true;  // keep redrawing for flash
  }

  tft.fillScreen(flashOn ? C_RED : 0x7000);  // red / dark red

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);

  if (data.fallDetected) {
    tft.setTextSize(3);
    tft.drawString("FALL", W/2, 45);
    tft.setTextSize(2);
    tft.drawString("DETECTED", W/2, 80);
  } else if (data.heartRate > HR_WARN_HIGH) {
    tft.setTextSize(2);
    tft.drawString("HIGH HEART", W/2, 45);
    tft.drawString("RATE", W/2, 70);
    char hrStr[16]; sprintf(hrStr, "%.0f bpm", data.heartRate);
    tft.setTextSize(1);
    tft.setTextColor(C_YELLOW);
    tft.drawString(hrStr, W/2, 100);
  } else if (data.heartRate > 0 && data.heartRate < HR_WARN_LOW) {
    tft.setTextSize(2);
    tft.drawString("LOW HEART", W/2, 45);
    tft.drawString("RATE", W/2, 70);
    char hrStr[16]; sprintf(hrStr, "%.0f bpm", data.heartRate);
    tft.setTextSize(1);
    tft.setTextColor(C_YELLOW);
    tft.drawString(hrStr, W/2, 100);
  } else {
    tft.setTextSize(3);
    tft.drawString("SOS", W/2, 55);
  }

  tft.setTextSize(1);
  tft.setTextColor(C_TEXT);
  tft.drawString("Press BTN2 to dismiss", W/2, H - 12);
}
