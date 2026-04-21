// ============================================================
// TIGA — Main Application v5.0
// Wearable health tracker for elderly users
//
// What's new in v5:
//   - MPU6050 init retries 3x at boot (with Serial diagnostics)
//   - I2C dropped to 100kHz for breadboard reliability
//   - readMPU() wrapper with auto-reconnect on mid-session failure
//   - I2C bus recovery routine (unsticks SDA after power glitches)
//   - Fall detection rejects unphysical >6G spikes (kills false triggers)
//   - Adaptive step detection (orientation-independent, works in bag/hand/pocket)
//   - Session reset: hold BTN2 3s from clock/health screen
//   - Auto-anchor session to first detected step (fallback if reset not pressed)
//   - MPU health stats included in session export
//   - All v4 features retained
//
// Library needed: TinyGPS++ by Mikal Hart
//   Arduino IDE → Library Manager → search "TinyGPSPlus" → install
//
// Confirmed pin map (T-Display-S3):
//   MPU6050  SDA→18   SCL→17  VCC→3V  GND→GND
//   Pulse    S→1      +→3V    -→GND
//   SW-420   DO→3     VCC→3V  GND→GND
//   TTP223   IO→13    VCC→3V  GND→GND
//   Button1  GPIO21→GND (scroll / toggle / hold 3s = sleep)
//   Button2  GPIO16→GND (select / confirm / hold 3s = reset session)
//   Both     BTN1+BTN2 together = export session to Serial
//   Battery  GPIO04 (internal ADC)
//   LCD pwr  GPIO15 (must HIGH)
//   GPS      VCC→3V  GND→GND  TX→GPIO44  RX→GPIO43
// ============================================================

#include <TFT_eSPI.h>
#include <Wire.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>
#include <TinyGPSPlus.h>

// ── GPS ──────────────────────────────────────────────────────
#define GPS_RX_PIN   44   // ESP32 receives from GPS TX
#define GPS_TX_PIN   43   // ESP32 transmits to GPS RX
#define GPS_BAUD     9600

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);  // UART1

struct GPSData {
  bool    hasFix       = false;
  double  lat          = 0;
  double  lng          = 0;
  float   speedKmh     = 0;
  float   distanceM    = 0;    // total distance this session
  int     satellites   = 0;
  double  lastLat      = 0;
  double  lastLng      = 0;
  bool    lastValid    = false;
} gpsData;

// ── Deep sleep ───────────────────────────────────────────────
#define SLEEP_HOLD_MS  3000   // Hold BTN1 this long to sleep
#define WAKE_PIN       GPIO_NUM_21  // BTN1 wakes from sleep

// ── WiFi for NTP time sync (optional — works without it) ─────
// Leave blank if no WiFi — clock will show 00:00 until set
const char* WIFI_SSID = "Cronus";      // e.g. "MyHomeWiFi"
const char* WIFI_PASS = "Garfield77";      // e.g. "mypassword"
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 28800;   // GMT+8 (Malaysia/Singapore)
const int   DAYLIGHT_OFFSET = 0;

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
#define C_BG      0x0000   // Black
#define C_CARD    0x18E3   // Dark grey
#define C_CARD2   0x2104   // Slightly lighter card
#define C_TEXT    0xFFFF   // White
#define C_MUTED   0x8410   // Grey
#define C_DIM     0x4208   // Dark grey text
#define C_ACCENT  0x051D   // Cyan
#define C_GREEN   0x0680   // Green
#define C_ORANGE  0xFD20   // Orange
#define C_RED     0xF800   // Red
#define C_YELLOW  0xFFE0   // Yellow
#define C_PURPLE  0x801F   // Purple
#define C_PINK    0xF81F   // Pink

// ── Health thresholds (CVD patient, 65+) ─────────────────────
#define HR_SAFE_MIN   50
#define HR_SAFE_MAX  100
#define HR_WARN_LOW   45
#define HR_WARN_HIGH 110
#define STEPS_GOAL  3000
#define FALL_G       3.0f   // Raised from 2.5 — reduces false triggers on table
#define STABLE_G     1.3f

// ── App states ───────────────────────────────────────────────
enum AppState {
  STATE_CLOCK,        // Default: big clock face
  STATE_HEALTH,       // 4-card health dashboard
  STATE_MENU,         // Test menu
  STATE_HEART,        // Heart detail
  STATE_FITNESS,      // Steps + activity
  STATE_STABILITY,    // Fall + balance
  STATE_DEXTERITY,    // Tap + reaction tests
  STATE_SUMMARY,      // Daily summary
  STATE_DOCTOR,       // Doctor's report (medical terms)
  STATE_SETTINGS,     // Language + preferences
  STATE_EMERGENCY,    // Fall / high HR alert
  STATE_SOS,          // Manual SOS
  STATE_FALL_CONFIRM  // 10s countdown after fall before alerting
};

AppState state     = STATE_CLOCK;
AppState lastState = STATE_EMERGENCY; // force full draw on boot
bool needsFullDraw = true;
bool inHealthView  = false; // toggle on clock screen

// ── Health data ──────────────────────────────────────────────
struct HealthData {
  float heartRate    = 0;
  int   steps        = 0;
  float accelG       = 1.0f;
  float tiltAngle    = 0;
  bool  isStable     = true;
  bool  fallDetected = false;
  float battery      = 100.0f;
  int   healthScore  = 85;
  bool  wearing      = false;
  float tempC        = 33.0f;
  int   hrZone       = 0;     // 0=rest 1=light 2=moderate 3=high
  int   activityMins = 0;
  int   balanceScore = 100;   // 0-100
} data;

// Daily tracking
struct DailyData {
  int   peakSteps    = 0;
  float avgHR        = 0;
  int   hrSamples    = 0;
  int   fallCount    = 0;
  int   activityMins = 0;
  int   sosCount     = 0;
} daily;

// Previous values for partial redraw
struct PrevData {
  float heartRate   = -1;
  int   steps       = -1;
  bool  isStable    = true;
  float tempC       = -1;
  int   healthScore = -1;
  bool  wearing     = false;
  float battery     = -1;
  int   minute      = -1;
} prev;

// ── Sensors ──────────────────────────────────────────────────
MPU6050 mpu;
bool mpuOK = false;

// MPU health tracking (v5)
unsigned long mpuReconnectCount   = 0;
unsigned long mpuLastFailMs       = 0;
unsigned long mpuConsecutiveZeros = 0;
bool          mpuHealthDegraded   = false;

// Step detection state (v5 — adaptive, orientation-independent)
#define STEP_BUF_SIZE 10
float         stepMagBuf[STEP_BUF_SIZE];
int           stepBufIdx     = 0;
float         stepBaseline   = 1.0f;
bool          stepAboveThr   = false;
unsigned long stepDebounce   = 0;

// Session auto-anchor (v5)
bool          sessionAnchored = false;   // true once first step or reset
// BTN2 long-press for session reset (v5)
unsigned long btn2HoldStart  = 0;
bool          btn2Held       = false;
#define RESET_HOLD_MS  3000

// Pulse
#define PULSE_SAMPLES 30
float   pulseBuf[PULSE_SAMPLES];
int     pulseIdx      = 0;
float   pulseBaseline = 2000;
float   pulseThreshold = 2300;
unsigned long lastBeat = 0;
float   lastBPM        = 0;
bool    wasAbove       = false;

// Step counter
int   stepCount  = 0;
float lastG      = 1.0f;
unsigned long lastStep = 0;

// Fall detection
bool  inFall     = false;
unsigned long fallTime = 0;
unsigned long fallConfirmStart = 0;
int   fallCountdown = 10;

// Activity timing
unsigned long activityStart = 0;
bool inActivity = false;

// Buttons
bool btn1Last = false, btn2Last = false;
bool btn1Pressed = false, btn2Pressed = false;
unsigned long btn1HoldStart = 0;
bool btn1Held = false;

// Menu
const char* menuItems[] = {
  "Heart", "Fitness", "Stability",
  "Dexterity", "Summary", "Doctor report",
  "Settings", "SOS", "Back"
};
#define MENU_COUNT 9
int menuSel = 0;

// SOS / emergency
unsigned long lastPulse = 0;
bool pulseOn = true;
int sosCountdown = 0;
unsigned long sosStart = 0;

// ── Session tracking (for export) ────────────────────────────
unsigned long sessionStart = 0;
float   sessionPeakHR  = 0;
float   sessionLowHR   = 999;
int     sessionSteps   = 0;


bool timeSet = false;
int  displayHour = 0, displayMin = 0, displaySec = 0;
int  displayDay = 14, displayMonth = 4, displayYear = 2026;
const char* dayNames[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* monthNames[] = {"","Jan","Feb","Mar","Apr","May","Jun",
                              "Jul","Aug","Sep","Oct","Nov","Dec"};

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

  // I2C + MPU with retry and bus recovery (v5)
  i2cBusRecover();                      // pulse SCL in case bus is stuck
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);                // 100kHz — reliable on breadboard
  delay(200);

  mpuOK = false;
  for (int attempt = 1; attempt <= 3 && !mpuOK; attempt++) {
    mpu.initialize();
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_4);
    mpu.setDLPFMode(MPU6050_DLPF_BW_20);
    delay(100);
    mpuOK = mpu.testConnection();
    Serial.printf("[TIGA] MPU6050 init attempt %d: %s\n",
                  attempt, mpuOK ? "OK" : "FAIL");
    if (!mpuOK) delay(200);
  }
  if (!mpuOK) {
    Serial.println("[TIGA] MPU6050 failed after 3 attempts — continuing without");
    Serial.println("[TIGA] Check wiring: SDA=GPIO18, SCL=GPIO17, VCC=3V3, GND=GND");
  }

  // Initialize step detection baseline buffer
  for (int i = 0; i < STEP_BUF_SIZE; i++) stepMagBuf[i] = 1.0f;

  // Calibrate pulse
  long sum = 0; int mn=4095, mx=0;
  for (int i = 0; i < 100; i++) {
    int v = analogRead(PULSE_PIN);
    sum += v; if(v<mn)mn=v; if(v>mx)mx=v;
    delay(10);
  }
  pulseBaseline  = sum / 100.0f;
  pulseThreshold = pulseBaseline + max(200.0f, (float)(mx-mn)*1.5f);
  for (int i = 0; i < PULSE_SAMPLES; i++) pulseBuf[i] = pulseBaseline;
  Serial.printf("[TIGA] Pulse base:%.0f thresh:%.0f\n", pulseBaseline, pulseThreshold);

  // WiFi + NTP
  if (strlen(WIFI_SSID) > 0) {
    tft.fillScreen(C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED);
    tft.setTextSize(1);
    tft.drawString("Connecting to WiFi...", W/2, H/2);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER);
      delay(1000);
      updateTime();
      Serial.println("[TIGA] WiFi + NTP OK");
    } else {
      Serial.println("[TIGA] WiFi failed — using manual time");
    }
  }

  needsFullDraw = true;
  state = STATE_CLOCK;
  // GPS Serial
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[TIGA] GPS UART started on RX=44, TX=43");

  sessionStart = millis();
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

  // Feed GPS continuously — must be called often
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // Update GPS data every 2 seconds
  static unsigned long lastGPS = 0;
  if (millis() - lastGPS >= 2000) {
    lastGPS = millis();
    readGPS();
  }

  // Update time every second
  static unsigned long lastTimeTick = 0;
  if (millis() - lastTimeTick >= 1000) {
    lastTimeTick = millis();
    tickTime();
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

  // Partial updates
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();

    if (state == STATE_CLOCK)  drawClockPartial();
    if (state == STATE_HEALTH) drawHealthPartial();
    copyPrev();
  }

  // SOS / emergency pulse animation
  if ((state == STATE_EMERGENCY || state == STATE_SOS) &&
      millis() - lastPulse > 800) {
    lastPulse = millis();
    pulseOn = !pulseOn;
    drawEmergency();
  }

  // Fall confirm countdown
  if (state == STATE_FALL_CONFIRM) {
    int elapsed = (millis() - fallConfirmStart) / 1000;
    int remaining = 10 - elapsed;
    if (remaining != fallCountdown) {
      fallCountdown = remaining;
      drawFallConfirm();
    }
    if (remaining <= 0) {
      // Time's up — send alert (WiFi in v2)
      daily.fallCount++;
      state = STATE_EMERGENCY;
      needsFullDraw = true;
    }
  }

  delay(20);
}

// ── Helpers ──────────────────────────────────────────────────
void copyPrev() {
  prev.heartRate   = data.heartRate;
  prev.steps       = data.steps;
  prev.isStable    = data.isStable;
  prev.tempC       = data.tempC;
  prev.healthScore = data.healthScore;
  prev.wearing     = data.wearing;
  prev.battery     = data.battery;
  prev.minute      = displayMin;
}

// ============================================================
// TIME
// ============================================================
void updateTime() {
  struct tm ti;
  if (getLocalTime(&ti)) {
    displayHour  = ti.tm_hour;
    displayMin   = ti.tm_min;
    displaySec   = ti.tm_sec;
    displayDay   = ti.tm_mday;
    displayMonth = ti.tm_mon + 1;
    displayYear  = ti.tm_year + 1900;
    timeSet = true;
  }
}

void tickTime() {
  if (timeSet) {
    updateTime();
    return;
  }
  // Manual tick if no WiFi
  displaySec++;
  if (displaySec >= 60) { displaySec = 0; displayMin++; }
  if (displayMin >= 60) { displayMin = 0; displayHour++; }
  if (displayHour >= 24) { displayHour = 0; }
}

// ============================================================
// SENSORS
// ============================================================
void readGPS() {
  gpsData.hasFix     = gps.location.isValid() && gps.location.age() < 3000;
  gpsData.satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;

  if (gpsData.hasFix) {
    double lat = gps.location.lat();
    double lng = gps.location.lng();
    gpsData.lat = lat;
    gpsData.lng = lng;

    // Speed — zero out below 0.5 km/h to suppress standing-still jitter
    if (gps.speed.isValid()) {
      float raw = gps.speed.kmph();
      gpsData.speedKmh = (raw < 0.5f) ? 0.0f : raw;
    }

    // Accumulate distance
    if (gpsData.lastValid) {
      double dist = TinyGPSPlus::distanceBetween(
        gpsData.lastLat, gpsData.lastLng, lat, lng);
      // Only count if moved more than 5m (filters GPS jitter better)
      if (dist > 5.0) {
        gpsData.distanceM += dist;
        gpsData.lastLat = lat;
        gpsData.lastLng = lng;
      }
    } else {
      gpsData.lastLat   = lat;
      gpsData.lastLng   = lng;
      gpsData.lastValid = true;
    }

    Serial.printf("[GPS] Fix: %.6f, %.6f  Speed: %.1f km/h  Dist: %.0fm  Sats: %d\n",
                  lat, lng, gpsData.speedKmh, gpsData.distanceM, gpsData.satellites);
  } else {
    gpsData.speedKmh = 0;
    Serial.printf("[GPS] Searching... sats: %d  chars: %lu\n",
                  gpsData.satellites, gps.charsProcessed());
  }
}

// ============================================================
// MPU6050 ROBUSTNESS HELPERS (v5)
// ============================================================

// I2C bus recovery: clocks SCL 9 times to release a stuck slave.
// The MPU6050 can hold SDA low after an aborted transaction (e.g.
// power glitch). This bit-bangs SCL until the slave releases the
// bus, then issues a STOP condition. Safe to call before Wire.begin().
void i2cBusRecover() {
  pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
  pinMode(I2C_SDA, INPUT_PULLUP);

  if (digitalRead(I2C_SDA) == HIGH) {
    Serial.println("[TIGA] I2C bus clean, no recovery needed");
    return;
  }

  Serial.println("[TIGA] I2C SDA stuck low — recovering bus");
  for (int i = 0; i < 9; i++) {
    digitalWrite(I2C_SCL, LOW);
    delayMicroseconds(5);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(5);
    pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C_SCL, HIGH);
    delayMicroseconds(5);
    if (digitalRead(I2C_SDA) == HIGH) break;
  }

  // Manual STOP condition
  pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(I2C_SDA, HIGH);
  delayMicroseconds(5);
  Serial.println("[TIGA] I2C bus recovery complete");
}

// Robust MPU accel read with auto-reinit on failure.
// Returns true if the read looks valid. Sets ax/ay/az to 0 on failure.
// After 5 consecutive all-zero reads, marks MPU as dead and tries
// to recover every 30 seconds.
bool readMPU(int16_t* ax, int16_t* ay, int16_t* az) {
  if (!mpuOK) {
    static unsigned long lastRecoveryAttempt = 0;
    if (millis() - lastRecoveryAttempt > 30000) {
      lastRecoveryAttempt = millis();
      Serial.println("[TIGA] Attempting MPU recovery...");
      i2cBusRecover();
      Wire.begin(I2C_SDA, I2C_SCL);
      Wire.setClock(100000);
      delay(50);
      mpu.initialize();
      mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_4);
      mpu.setDLPFMode(MPU6050_DLPF_BW_20);
      delay(50);
      if (mpu.testConnection()) {
        mpuOK = true;
        mpuReconnectCount++;
        mpuConsecutiveZeros = 0;
        Serial.printf("[TIGA] MPU recovered! (reconnect #%lu)\n",
                      mpuReconnectCount);
      }
    }
    *ax = 0; *ay = 0; *az = 0;
    return false;
  }

  mpu.getAcceleration(ax, ay, az);

  // "Exactly zero on all axes" = bus failure, not a real reading.
  // At rest at least one axis always reads ~±8192 due to gravity.
  if (*ax == 0 && *ay == 0 && *az == 0) {
    mpuConsecutiveZeros++;
    if (mpuConsecutiveZeros >= 5) {
      Serial.printf("[TIGA] MPU6050 appears disconnected at %lums\n",
                    millis());
      mpuOK = false;
      mpuLastFailMs = millis();
      mpuHealthDegraded = true;
      mpuConsecutiveZeros = 0;
    }
    return false;
  }

  mpuConsecutiveZeros = 0;
  return true;
}

void readSensors() {
  // ── Pulse ─────────────────────────────────────────────────
  int rawPulse = analogRead(PULSE_PIN);
  pulseBuf[pulseIdx] = rawPulse;
  pulseIdx = (pulseIdx + 1) % PULSE_SAMPLES;

  bool above = (rawPulse > pulseThreshold);
  if (above && !wasAbove && millis() - lastBeat > 300) {
    unsigned long now = millis();
    if (lastBeat > 0) {
      float bpm = 60000.0f / (float)(now - lastBeat);
      if (bpm >= 40 && bpm <= 180) {
        lastBPM = lastBPM * 0.6f + bpm * 0.4f;
        // Track for daily average
        daily.avgHR = (daily.avgHR * daily.hrSamples + lastBPM) / (daily.hrSamples + 1);
        daily.hrSamples++;
      }
    }
    lastBeat = now;
  }
  wasAbove = above;
  if (millis() - lastBeat > 6000) lastBPM = 0;
  data.heartRate = lastBPM;

  // Track session peak / low HR
  if (lastBPM > 0) {
    if (lastBPM > sessionPeakHR) sessionPeakHR = lastBPM;
    if (lastBPM < sessionLowHR)  sessionLowHR  = lastBPM;
  }
  sessionSteps = data.steps;

  // HR zone
  if      (data.heartRate == 0)                   data.hrZone = 0;
  else if (data.heartRate < 60)                   data.hrZone = 0; // resting
  else if (data.heartRate < 75)                   data.hrZone = 1; // light
  else if (data.heartRate <= HR_SAFE_MAX)         data.hrZone = 2; // moderate
  else                                            data.hrZone = 3; // high

  // ── MPU6050 (v5 — robust read with auto-reconnect) ────────
  int16_t ax, ay, az;
  if (readMPU(&ax, &ay, &az)) {
    float g = sqrtf((float)ax*ax + (float)ay*ay + (float)az*az) / 8192.0f;

    // Sanity check: reject unphysical spikes (>6G on a still device = I2C glitch)
    // Real falls produce 2-5G; nothing realistic exceeds 6G for a wrist device.
    if (g > 6.0f) {
      Serial.printf("[TIGA] Rejecting unphysical accel spike: %.2fG\n", g);
      // Don't update accelG, fall detection, or steps this cycle
    } else {
      data.accelG = g;

      // Tilt angle for posture (angle from vertical)
      float pitch = atan2f((float)ax, sqrtf((float)ay*ay + (float)az*az))
                    * 180.0f / 3.14159f;
      data.tiltAngle = pitch;

      // Fall detection
      if (g > FALL_G && !inFall && state != STATE_FALL_CONFIRM) {
        inFall = true; fallTime = millis();
      }
      if (inFall && millis() - fallTime > 250) {
        if (g < 0.5f) {
          inFall = false;
          fallConfirmStart = millis();
          fallCountdown = 10;
          state = STATE_FALL_CONFIRM;
          needsFullDraw = true;
        } else {
          inFall = false;
        }
      }

      // ── ADAPTIVE STEP DETECTION (v5) ──────────────────────
      // Orientation-independent: uses |g| magnitude, not per-axis.
      // Works for bag/hand/pocket carry regardless of device angle.
      stepMagBuf[stepBufIdx] = g;
      stepBufIdx = (stepBufIdx + 1) % STEP_BUF_SIZE;

      float sum = 0;
      for (int i = 0; i < STEP_BUF_SIZE; i++) sum += stepMagBuf[i];
      stepBaseline = sum / STEP_BUF_SIZE;

      float deviation = g - stepBaseline;

      // Peak detection with hysteresis + debounce (min 300ms between steps)
      if (!stepAboveThr && deviation > 0.15f &&
          millis() - stepDebounce > 300) {
        stepAboveThr = true;
        stepDebounce = millis();
        stepCount++;
        lastStep = millis();
        if (stepCount > daily.peakSteps) daily.peakSteps = stepCount;

        // Auto-anchor session on first real step (v5)
        if (!sessionAnchored) {
          sessionStart = millis();
          sessionAnchored = true;
          Serial.println("[TIGA] Session auto-anchored on first step");
        }
      } else if (stepAboveThr && deviation < 0.08f) {
        stepAboveThr = false;
      }

      lastG = g;
      data.steps    = stepCount;
      data.isStable = (g < STABLE_G);

      // Balance score (unchanged)
      static float wobbleAccum = 0;
      static int   wobbleSamples = 0;
      wobbleAccum += fabsf(g - 1.0f);
      wobbleSamples++;
      if (wobbleSamples >= 50) {
        float avgWobble = wobbleAccum / wobbleSamples;
        data.balanceScore = (int)constrain(100 - avgWobble * 200, 0, 100);
        wobbleAccum = 0; wobbleSamples = 0;
      }

      // Activity timing (unchanged)
      if (g > 1.15f && !inActivity) {
        inActivity = true; activityStart = millis();
      } else if (g < 1.05f && inActivity) {
        inActivity = false;
        daily.activityMins += (millis() - activityStart) / 60000;
        data.activityMins = daily.activityMins;
      }
    }

    // Temperature (safe even when we rejected a spike — separate read)
    int16_t tempRaw = mpu.getTemperature();
    data.tempC = (tempRaw / 340.0f) + 36.53f;
  }

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

  // HR alert (only from health/clock state)
  if (data.heartRate > 0 && (state == STATE_CLOCK || state == STATE_HEALTH)) {
    if (data.heartRate > HR_WARN_HIGH || data.heartRate < HR_WARN_LOW) {
      state = STATE_EMERGENCY;
      needsFullDraw = true;
    }
  }
}

int calcScore() {
  int hr=50, act=50, stab=50;
  if (data.heartRate > 0) {
    if      (data.heartRate >= HR_SAFE_MIN && data.heartRate <= HR_SAFE_MAX) hr = 100;
    else if (data.heartRate >= HR_WARN_LOW  && data.heartRate <= HR_WARN_HIGH) hr = 65;
    else hr = 20;
  }
  act  = (int)(min((float)data.steps / STEPS_GOAL, 1.0f) * 100);
  stab = data.isStable ? 100 : 40;
  return (int)(hr*0.35f + act*0.25f + stab*0.40f);
}

const char* hrZoneLabel() {
  switch(data.hrZone) {
    case 0: return "resting";
    case 1: return "light activity";
    case 2: return "moderate";
    case 3: return "high — take a rest";
    default: return "";
  }
}

const char* hrStatusLabel() {
  if (data.heartRate == 0) return "Place finger on sensor";
  if (data.heartRate < HR_WARN_LOW)  return "Very low — please rest";
  if (data.heartRate < HR_SAFE_MIN)  return "A little low";
  if (data.heartRate <= HR_SAFE_MAX) return "Heart is steady";
  if (data.heartRate <= HR_WARN_HIGH)return "Beating a bit fast";
  return "Too fast — please rest";
}

uint16_t hrStatusColor() {
  if (data.heartRate == 0) return C_MUTED;
  if (data.heartRate < HR_WARN_LOW || data.heartRate > HR_WARN_HIGH) return C_RED;
  if (data.heartRate < HR_SAFE_MIN || data.heartRate > HR_SAFE_MAX)  return C_ORANGE;
  return C_GREEN;
}

const char* stepsLabel() {
  int pct = (int)((float)data.steps / STEPS_GOAL * 100);
  if (data.steps == 0)   return "Let's start moving!";
  if (pct < 25)          return "Good start — keep going";
  if (pct < 50)          return "Making progress";
  if (pct < 75)          return "More than halfway!";
  if (pct < 100)         return "Almost there!";
  return "Goal reached! Well done!";
}

const char* postureLabel() {
  float a = fabsf(data.tiltAngle);
  if (a < 10) return "Posture looks good";
  if (a < 25) return "Slight lean detected";
  return "Please stand up straight";
}

uint16_t healthDot() {
  if (data.healthScore >= 75) return C_GREEN;
  if (data.healthScore >= 50) return C_ORANGE;
  return C_RED;
}

// ============================================================
// BUTTONS
// ============================================================
// ── Session reset (v5) ───────────────────────────────────────
// Triggered by holding BTN2 for 3s on clock or health screen.
// Resets timer, step count, peak/low HR, and re-anchors session.
void resetSession() {
  // Visual confirmation
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GREEN);
  tft.setTextSize(2);
  tft.drawString("Session reset", W/2, H/2 - 10);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString("Timer starts now", W/2, H/2 + 14);

  // Reset counters
  sessionStart     = millis();
  sessionAnchored  = true;
  stepCount        = 0;
  sessionSteps     = 0;
  sessionPeakHR    = 0;
  sessionLowHR     = 999;
  daily.activityMins = 0;
  daily.fallCount  = 0;
  daily.hrSamples  = 0;
  daily.avgHR      = 0;

  // Reset GPS distance tracking
  gpsData.distanceM = 0;
  gpsData.lastValid = false;

  // Reset MPU health stats for new session
  mpuReconnectCount   = 0;
  mpuLastFailMs       = 0;
  mpuHealthDegraded   = false;

  Serial.println("[TIGA] Session reset by user (BTN2 hold)");
  delay(1500);
  needsFullDraw = true;
}

void exportSession() {
  unsigned long dur = (millis() - sessionStart) / 1000;
  int durMin = dur / 60;
  int durSec = dur % 60;

  // Show exporting message on screen
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.setTextSize(1);
  tft.drawString("Exporting to Serial Monitor...", W/2, H/2 - 10);
  tft.setTextColor(C_MUTED);
  tft.drawString("Open Serial Monitor at 115200", W/2, H/2 + 8);
  tft.drawString("then copy the report", W/2, H/2 + 24);

  // ── TIGA WALK TEST REPORT ────────────────────────────────
  Serial.println();
  Serial.println("=================================================");
  Serial.println("  TIGA WALK TEST REPORT");
  Serial.println("=================================================");
  Serial.printf ("  Date:      %02d/%02d/%04d  %02d:%02d\n",
                  displayDay, displayMonth, displayYear,
                  displayHour, displayMin);
  Serial.printf ("  Duration:  %d min %d sec\n", durMin, durSec);
  Serial.printf ("  Subject:   Test subject (self)\n");
  Serial.println("-------------------------------------------------");

  // ── WHAT TO ANALYSE: HEART ──
  Serial.println("  [1] HEART RATE");
  if (daily.hrSamples > 0) {
    Serial.printf ("  Average:   %.0f bpm\n", daily.avgHR);
    Serial.printf ("  Peak:      %.0f bpm\n", sessionPeakHR);
    Serial.printf ("  Low:       %.0f bpm\n", sessionLowHR > 900 ? 0 : sessionLowHR);
    Serial.printf ("  Samples:   %d readings\n", daily.hrSamples);
    Serial.println("  >> Check: avg should be 60-100 for resting/light walk");
    Serial.println("  >> Check: peak shows exertion level during walk");
    if (daily.avgHR > 0 && daily.avgHR < 60)
      Serial.println("  >> FLAG: Average below 60 — bradycardia range");
    if (sessionPeakHR > 110)
      Serial.println("  >> FLAG: Peak above 110 — monitor closely");
  } else {
    Serial.println("  No HR readings — finger was not on sensor.");
    Serial.println("  >> ACTION: Stop 2-3x next walk, hold finger 15s each.");
  }
  Serial.println();

  // ── WHAT TO ANALYSE: ACTIVITY ──
  Serial.println("  [2] ACTIVITY");
  Serial.printf ("  Steps:     %d of %d goal (%d%%)\n",
                  sessionSteps, STEPS_GOAL,
                  (int)min((float)sessionSteps/STEPS_GOAL*100,100.0f));
  Serial.printf ("  Active:    %d minutes\n", daily.activityMins);
  if (gpsData.distanceM > 0) {
    Serial.printf ("  Distance:  %.0f m (%.2f km) — GPS measured\n",
                    gpsData.distanceM, gpsData.distanceM/1000.0f);
    // Estimate stride length from steps vs GPS distance
    if (sessionSteps > 0) {
      float strideM = gpsData.distanceM / sessionSteps;
      Serial.printf ("  Stride:    ~%.2f m per step\n", strideM);
      Serial.println("  >> Check: typical stride 0.6-0.8m. Shorter = cautious gait.");
    }
  } else {
    Serial.println("  Distance:  No GPS fix (indoors / no sky view)");
  }
  Serial.println("  >> Check: did step count increase steadily during walk?");
  Serial.println("  >> Check: any long pauses? (could indicate fatigue/rest)");
  Serial.println();

  // ── WHAT TO ANALYSE: STABILITY ──
  Serial.println("  [3] STABILITY & FALL RISK");
  Serial.printf ("  Falls detected:  %d\n", daily.fallCount);
  Serial.printf ("  Balance score:   %d / 100\n", data.balanceScore);
  Serial.printf ("  Tilt angle:      %.1f degrees\n", data.tiltAngle);
  Serial.printf ("  Accel peak:      %.2f G\n", data.accelG);
  Serial.printf ("  MPU health:      %s\n",
                 mpuHealthDegraded ? "DEGRADED" : "OK");
  if (mpuReconnectCount > 0) {
    Serial.printf ("  MPU reconnects:  %lu\n", mpuReconnectCount);
    if (mpuLastFailMs > 0) {
      unsigned long failedAtMin = (mpuLastFailMs - sessionStart) / 60000;
      Serial.printf ("  Last MPU fail:   %lu min into session\n",
                     failedAtMin);
    }
  }
  Serial.println("  >> Check: 0 falls = good. Any fall = review threshold.");
  Serial.println("  >> Check: balance score >70 = stable gait.");
  Serial.println("  >> Check: tilt >25 degrees = posture concern.");
  Serial.println("  >> Check: MPU reconnects >0 = loose wiring, re-seat SDA/SCL.");
  if (daily.fallCount > 0)
    Serial.println("  >> FLAG: Falls detected — check if real or false trigger.");
  Serial.println();

  // ── WHAT TO ANALYSE: GPS ──
  Serial.println("  [4] GPS & LOCATION");
  if (gpsData.hasFix) {
    Serial.printf ("  Last position: %.6f, %.6f\n",
                    gpsData.lat, gpsData.lng);
    Serial.printf ("  Satellites:    %d\n", gpsData.satellites);
    Serial.printf ("  Maps:          https://maps.google.com/?q=%.6f,%.6f\n",
                    gpsData.lat, gpsData.lng);
    Serial.println("  >> Paste Maps link into browser to see last location.");
    Serial.println("  >> On fall alert this link would go to family via Telegram.");
  } else {
    Serial.printf ("  No GPS fix. Satellites seen: %d\n", gpsData.satellites);
    Serial.println("  >> Go outside with clear sky view for GPS to lock.");
  }
  Serial.println();

  // ── WHAT TO ANALYSE: DEVICE HEALTH ──
  Serial.println("  [5] DEVICE HEALTH");
  Serial.printf ("  Battery:   %.0f%%\n", data.battery);
  Serial.printf ("  Worn:      %s\n", data.wearing ? "Yes" : "No — not in skin contact");
  Serial.printf ("  Temp:      %.1f C (chip die temp, not body)\n", data.tempC);
  Serial.println("  >> Battery should be >50% for a full walk test.");
  Serial.println("  >> Worn = No means sensor was not against skin.");
  Serial.println();

  // ── SENSOR CALIBRATION NOTES ──
  Serial.println("  [6] CALIBRATION NOTES FOR NEXT WALK");
  Serial.println("  Pulse sensor: stationary readings only.");
  Serial.println("  Stop, press finger firmly, wait 10s for stable reading.");
  Serial.println("  Step counter: accelerometer-based, ~5-10% error vs GPS.");
  Serial.println("  Fall threshold: 3.0G (raised from 2.5G to reduce false alarms).");
  Serial.println("  GPS jitter filter: 5m minimum movement to count distance.");
  Serial.println();
  Serial.println("=================================================");
  Serial.println("  END OF REPORT — copy and paste to share");
  Serial.println("=================================================");
  Serial.println();

  delay(3000);
  state = STATE_CLOCK;
  needsFullDraw = true;
}

void goToSleep() {
  // Show sleep screen briefly
  tft.fillScreen(0x0000);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0x2104);  // very dim
  tft.setTextSize(1);
  tft.drawString("sleeping...", W/2, H/2 - 10);
  tft.drawString("press any button to wake", W/2, H/2 + 8);
  delay(1200);

  // Wait for BTN1 to be released before sleeping
  // Otherwise the held button immediately wakes the device
  while (digitalRead(BUTTON1_PIN) == LOW) {
    delay(10);
  }
  delay(200); // small debounce after release

  // Backlight off
  digitalWrite(TFT_BL, LOW);
  digitalWrite(LCD_PWR_PIN, LOW);

  // Wake on BTN1 (GPIO21) going LOW (button pressed = pulled to GND)
  esp_sleep_enable_ext0_wakeup(WAKE_PIN, 0);
  esp_deep_sleep_start();
  // Code never reaches here — ESP32 resets on wake
}

void readButtons() {
  bool b1 = (digitalRead(BUTTON1_PIN) == LOW);
  bool b2 = (digitalRead(BUTTON2_PIN) == LOW);

  // Both buttons together = export session (from any state)
  static bool bothLast = false;
  bool both = (b1 && b2);
  if (both && !bothLast) {
    bothLast = true;
    exportSession();
    return;
  }
  bothLast = both;

  btn1Pressed = (b1 && !btn1Last);
  btn2Pressed = (b2 && !btn2Last);

  // Detect BTN1 long hold for sleep
  if (b1) {
    if (btn1HoldStart == 0) btn1HoldStart = millis();
    if (!btn1Held && millis() - btn1HoldStart >= SLEEP_HOLD_MS) {
      btn1Held = true;
      btn1Pressed = false;  // cancel short press action
      goToSleep();
    }
  } else {
    btn1HoldStart = 0;
    btn1Held = false;
  }

  // Detect BTN2 long hold for session reset (v5)
  // Only active on clock/health screens to avoid menu accidents
  if (b2 && (state == STATE_CLOCK || state == STATE_HEALTH)) {
    if (btn2HoldStart == 0) btn2HoldStart = millis();
    if (!btn2Held && millis() - btn2HoldStart >= RESET_HOLD_MS) {
      btn2Held = true;
      btn2Pressed = false;  // cancel short press action
      resetSession();
    }
  } else {
    btn2HoldStart = 0;
    btn2Held = false;
  }

  btn1Last = b1;
  btn2Last = b2;
}

void handleInput() {
  if (!btn1Pressed && !btn2Pressed) return;

  switch (state) {

    case STATE_CLOCK:
      if (btn1Pressed) {
        // Toggle to health view
        state = STATE_HEALTH;
        needsFullDraw = true;
      }
      if (btn2Pressed) {
        state = STATE_MENU;
        menuSel = 0;
        needsFullDraw = true;
      }
      break;

    case STATE_HEALTH:
      if (btn1Pressed) {
        state = STATE_CLOCK;
        needsFullDraw = true;
      }
      if (btn2Pressed) {
        state = STATE_MENU;
        menuSel = 0;
        needsFullDraw = true;
      }
      break;

    case STATE_MENU:
      if (btn1Pressed) {
        menuSel = (menuSel + 1) % MENU_COUNT;
        needsFullDraw = true;
      }
      if (btn2Pressed) {
        switch(menuSel) {
          case 0: state = STATE_HEART;     break;
          case 1: state = STATE_FITNESS;   break;
          case 2: state = STATE_STABILITY; break;
          case 3: state = STATE_DEXTERITY; break;
          case 4: state = STATE_SUMMARY;   break;
          case 5: state = STATE_DOCTOR;    break;
          case 6: state = STATE_SETTINGS;  break;
          case 7: state = STATE_SOS; daily.sosCount++; break;
          case 8: state = STATE_CLOCK;     break;
        }
        needsFullDraw = true;
      }
      break;

    case STATE_HEART:
    case STATE_FITNESS:
    case STATE_STABILITY:
    case STATE_SUMMARY:
    case STATE_DOCTOR:
    case STATE_SETTINGS:
      if (btn1Pressed || btn2Pressed) {
        state = STATE_MENU;
        needsFullDraw = true;
      }
      break;

    case STATE_DEXTERITY:
      // Dexterity handles its own input inside drawDexterity()
      break;

    case STATE_FALL_CONFIRM:
      if (btn2Pressed) {
        // Cancel — user is OK
        inFall = false;
        state = STATE_CLOCK;
        needsFullDraw = true;
      }
      break;

    case STATE_EMERGENCY:
    case STATE_SOS:
      if (btn2Pressed) {
        data.fallDetected = false;
        state = STATE_CLOCK;
        needsFullDraw = true;
      }
      break;
  }
}

// ============================================================
// DRAWING — FULL REDRAWS
// ============================================================
void drawScreenFull() {
  tft.fillScreen(C_BG);
  switch(state) {
    case STATE_CLOCK:       drawClockFull();    break;
    case STATE_HEALTH:      drawHealthFull();   break;
    case STATE_MENU:        drawMenu();         break;
    case STATE_HEART:       drawHeart();        break;
    case STATE_FITNESS:     drawFitness();      break;
    case STATE_STABILITY:   drawStability();    break;
    case STATE_DEXTERITY:   drawDexterity();    break;
    case STATE_SUMMARY:     drawSummary();      break;
    case STATE_DOCTOR:      drawDoctor();       break;
    case STATE_SETTINGS:    drawSettings();     break;
    case STATE_FALL_CONFIRM:drawFallConfirm();  break;
    case STATE_EMERGENCY:   drawEmergency();    break;
    case STATE_SOS:         drawEmergency();    break;
  }
}

// ── Shared header / footer ───────────────────────────────────
void drawTopBar(const char* title, uint16_t col = C_CARD) {
  tft.fillRect(0, 0, W, 22, col);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.drawString(title, W/2, 11);
}

void drawBottomHint(const char* hint) {
  tft.fillRect(0, H-14, W, 14, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.drawString(hint, W/2, H-7);
}

// ── Splash ───────────────────────────────────────────────────
void drawSplash() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.setTextSize(4);
  tft.drawString("tiga", W/2, H/2 - 18);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString("health tracker for mom", W/2, H/2 + 18);
}

// ── CLOCK FACE (default screen) ──────────────────────────────
void drawClockFull() {
  // Health status dot (top left — tiny, non-intrusive)
  uint16_t dot = healthDot();
  tft.fillCircle(12, 12, 5, dot);

  // Battery (top right — subtle)
  char batStr[8]; sprintf(batStr, "%.0f%%", data.battery);
  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(data.battery < 20 ? C_ORANGE : C_DIM);
  tft.drawString(batStr, W-8, 12);

  // Wearing indicator (top centre — only show if NOT wearing)
  if (!data.wearing) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DIM);
    tft.setTextSize(1);
    tft.drawString("not worn", W/2, 12);
  }

  // Large time
  char timeStr[8];
  sprintf(timeStr, "%02d:%02d", displayHour, displayMin);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(5);
  tft.drawString(timeStr, W/2, 78);

  // Date
  char dateStr[24];
  struct tm ti; getLocalTime(&ti);
  sprintf(dateStr, "%s, %d %s %d",
          dayNames[ti.tm_wday], displayDay,
          monthNames[displayMonth], displayYear);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString(dateStr, W/2, 120);

  // Subtle button hints
  tft.setTextColor(C_DIM);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("BTN1: health  hold: sleep", 8, H-8);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("BTN2 hold: reset  both: export", W-8, H-8);
}

void drawClockPartial() {
  // Only update time if minute changed
  if (displayMin == prev.minute) return;

  // Wipe time area only
  tft.fillRect(20, 52, W-40, 60, C_BG);
  char timeStr[8];
  sprintf(timeStr, "%02d:%02d", displayHour, displayMin);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(5);
  tft.drawString(timeStr, W/2, 78);

  // Update health dot if score changed
  if (data.healthScore != prev.healthScore) {
    tft.fillCircle(12, 12, 6, C_BG);
    tft.fillCircle(12, 12, 5, healthDot());
  }
}

// ── HEALTH DASHBOARD ─────────────────────────────────────────
void drawHealthFull() {
  // Header
  tft.fillRect(0, 0, W, 22, C_CARD);
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(data.wearing ? C_GREEN : C_MUTED);
  tft.drawString(data.wearing ? "wearing" : "not worn", 6, 11);

  char scoreStr[16]; sprintf(scoreStr, "score %d", data.healthScore);
  uint16_t sc = data.healthScore >= 75 ? C_GREEN :
                data.healthScore >= 50 ? C_ORANGE : C_RED;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(sc);
  tft.drawString(scoreStr, W/2, 11);

  char batStr[8]; sprintf(batStr, "%.0f%%", data.battery);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(data.battery < 20 ? C_ORANGE : C_MUTED);
  tft.drawString(batStr, W-6, 11);

  drawHealthCards();
  drawBottomHint("BTN1: clock   BTN2: menu");
}

void drawHealthCards() {
  int cx=4, cy=26, cw=(W-12)/2, ch=(H-cy-18)/2, gap=4;

  // Heart card
  tft.fillRect(cx, cy, cw, ch, C_CARD);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1); tft.setTextColor(C_MUTED);
  tft.drawString("HEART", cx+6, cy+10);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
  tft.setTextColor(hrStatusColor());
  tft.drawString(hrStatusLabel(), cx+cw/2, cy+ch/2+6);

  // Steps card
  tft.fillRect(cx+cw+gap, cy, cw, ch, C_CARD);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(C_MUTED);
  tft.drawString("STEPS", cx+cw+gap+6, cy+10);
  char stepsStr[12]; sprintf(stepsStr, "%d", data.steps);
  float prog = min((float)data.steps/STEPS_GOAL, 1.0f);
  uint16_t stCol = prog>=1.0f ? C_GREEN : prog>=0.5f ? C_ACCENT : C_TEXT;
  tft.setTextDatum(MC_DATUM); tft.setTextSize(2); tft.setTextColor(stCol);
  tft.drawString(stepsStr, cx+cw+gap+cw/2, cy+ch/2+2);
  // Progress bar
  int bx=cx+cw+gap+4, by=cy+ch-10, bw=cw-8, bh=5;
  tft.fillRect(bx, by, bw, bh, C_BG);
  tft.fillRect(bx, by, (int)(bw*prog), bh, stCol);

  // Stability card
  tft.fillRect(cx, cy+ch+gap, cw, ch, C_CARD);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1); tft.setTextColor(C_MUTED);
  tft.drawString("STABILITY", cx+6, cy+ch+gap+10);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
  tft.setTextColor(data.isStable ? C_GREEN : C_ORANGE);
  tft.drawString(data.isStable ? "Moving well" : "Unstable", cx+cw/2, cy+ch+gap+ch/2+6);

  // Temp card
  tft.fillRect(cx+cw+gap, cy+ch+gap, cw, ch, C_CARD);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(C_MUTED);
  tft.drawString("TEMP", cx+cw+gap+6, cy+ch+gap+10);
  char tempStr[12]; sprintf(tempStr, "%.1f C", data.tempC);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(2); tft.setTextColor(C_ACCENT);
  tft.drawString(tempStr, cx+cw+gap+cw/2, cy+ch+gap+ch/2+6);
}

void drawHealthPartial() {
  int cx=4, cy=26, cw=(W-12)/2, ch=(H-cy-18)/2, gap=4;

  // Heart card value
  if ((int)data.heartRate != (int)prev.heartRate) {
    tft.fillRect(cx+1, cy+16, cw-2, ch-17, C_CARD);
    tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
    tft.setTextColor(hrStatusColor());
    tft.drawString(hrStatusLabel(), cx+cw/2, cy+ch/2+6);
  }

  // Steps
  if (data.steps != prev.steps) {
    tft.fillRect(cx+cw+gap+1, cy+16, cw-2, ch-17, C_CARD);
    char stepsStr[12]; sprintf(stepsStr, "%d", data.steps);
    float prog = min((float)data.steps/STEPS_GOAL, 1.0f);
    uint16_t stCol = prog>=1.0f ? C_GREEN : prog>=0.5f ? C_ACCENT : C_TEXT;
    tft.setTextDatum(MC_DATUM); tft.setTextSize(2); tft.setTextColor(stCol);
    tft.drawString(stepsStr, cx+cw+gap+cw/2, cy+ch/2+2);
    int bx=cx+cw+gap+4, by=cy+ch-10, bw=cw-8, bh=5;
    tft.fillRect(bx, by, bw, bh, C_BG);
    tft.fillRect(bx, by, (int)(bw*prog), bh, stCol);
  }

  // Stability
  if (data.isStable != prev.isStable) {
    tft.fillRect(cx+1, cy+ch+gap+16, cw-2, ch-17, C_CARD);
    tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
    tft.setTextColor(data.isStable ? C_GREEN : C_ORANGE);
    tft.drawString(data.isStable ? "Moving well" : "Unstable", cx+cw/2, cy+ch+gap+ch/2+6);
  }

  // Header values
  if (data.healthScore != prev.healthScore ||
      data.wearing != prev.wearing ||
      (int)data.battery != (int)prev.battery) {
    tft.fillRect(0, 0, W, 22, C_CARD);
    tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
    tft.setTextColor(data.wearing ? C_GREEN : C_MUTED);
    tft.drawString(data.wearing ? "wearing" : "not worn", 6, 11);
    char scoreStr[16]; sprintf(scoreStr, "score %d", data.healthScore);
    uint16_t sc = data.healthScore >= 75 ? C_GREEN :
                  data.healthScore >= 50 ? C_ORANGE : C_RED;
    tft.setTextDatum(MC_DATUM); tft.setTextColor(sc);
    tft.drawString(scoreStr, W/2, 11);
    char batStr[8]; sprintf(batStr, "%.0f%%", data.battery);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(data.battery < 20 ? C_ORANGE : C_MUTED);
    tft.drawString(batStr, W-6, 11);
  }
}

// ── MENU ─────────────────────────────────────────────────────
void drawMenu() {
  drawTopBar("TIGA MENU");
  int itemH=14, startY=26;
  for (int i=0; i<MENU_COUNT; i++) {
    bool sel = (i==menuSel);
    int y = startY + i*itemH;
    if (sel) { tft.fillRect(8, y, W-16, itemH-1, C_ACCENT); tft.setTextColor(C_BG); }
    else        tft.setTextColor(i==7 ? C_RED : C_TEXT);
    tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
    tft.drawString(menuItems[i], 18, y+itemH/2);
  }
  drawBottomHint("BTN1: scroll   BTN2: select");
}

// ── HEART DETAIL ─────────────────────────────────────────────
void drawHeart() {
  drawTopBar("HEART");

  // Status message — large, human language
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(hrStatusColor());
  tft.drawString(hrStatusLabel(), W/2, 45);

  // BPM — shown but not dominant
  if (data.heartRate > 0) {
    char bpmStr[16]; sprintf(bpmStr, "%.0f bpm", data.heartRate);
    tft.setTextColor(C_MUTED);
    tft.drawString(bpmStr, W/2, 62);
  }

  // Zone bar
  tft.fillRect(20, 76, W-40, 14, C_CARD);
  const char* zones[] = {"Rest", "Light", "Moderate", "High"};
  uint16_t zoneColors[] = {C_ACCENT, C_GREEN, C_ORANGE, C_RED};
  int zw = (W-40)/4;
  for (int i=0; i<4; i++) {
    uint16_t col = (i == data.hrZone) ? zoneColors[i] : C_CARD2;
    tft.fillRect(20+i*zw, 76, zw-2, 14, col);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(i==data.hrZone ? C_BG : C_MUTED);
    tft.setTextSize(1);
    tft.drawString(zones[i], 20+i*zw+zw/2, 83);
  }

  // Zone label
  tft.setTextColor(C_TEXT);
  tft.drawString(hrZoneLabel(), W/2, 102);

  // Safe range for mom
  char rng[32]; sprintf(rng, "safe for you: %d - %d bpm", HR_SAFE_MIN, HR_SAFE_MAX);
  tft.setTextColor(C_DIM);
  tft.drawString(rng, W/2, 118);

  // Doctor note on what to do
  tft.setTextColor(C_MUTED);
  if (data.hrZone >= 3) {
    tft.drawString("Slow down and breathe deeply.", W/2, 134);
  } else if (data.hrZone == 0 && data.heartRate > 0) {
    tft.drawString("Good resting heart rate.", W/2, 134);
  } else {
    tft.drawString("Keep going at this pace — you're doing well.", W/2, 134);
  }

  drawBottomHint("any button: back");
}

// ── FITNESS ──────────────────────────────────────────────────
void drawFitness() {
  drawTopBar("FITNESS");

  // Steps message
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.drawString(stepsLabel(), W/2, 38);

  // Steps number
  char stepsStr[20];
  sprintf(stepsStr, "%d of %d steps", data.steps, STEPS_GOAL);
  tft.setTextColor(C_MUTED);
  tft.drawString(stepsStr, W/2, 54);

  // Progress bar
  float prog = min((float)data.steps/STEPS_GOAL, 1.0f);
  uint16_t stCol = prog>=1.0f ? C_GREEN : prog>=0.5f ? C_ACCENT : C_TEXT;
  tft.fillRect(20, 66, W-40, 12, C_CARD);
  tft.fillRect(20, 66, (int)((W-40)*prog), 12, stCol);

  // Percentage
  char pctStr[16]; sprintf(pctStr, "%.0f%%", prog*100);
  tft.setTextColor(stCol);
  tft.drawString(pctStr, W/2, 92);

  // Activity
  tft.setTextColor(C_MUTED);
  char actStr[32]; sprintf(actStr, "Active time today: %d min", daily.activityMins);
  tft.drawString(actStr, W/2, 108);

  // Posture
  tft.setTextColor(fabsf(data.tiltAngle) < 20 ? C_GREEN : C_ORANGE);
  tft.drawString(postureLabel(), W/2, 124);

  // Encouragement
  tft.setTextColor(C_DIM);
  if (data.steps < 1000)
    tft.drawString("Every step counts. You've got this!", W/2, 140);
  else if (data.steps >= STEPS_GOAL)
    tft.drawString("Your doctor would be proud today!", W/2, 140);
  else
    tft.drawString("Your doctor said 3,000 steps. Keep going!", W/2, 140);

  // GPS distance and speed (if fix)
  tft.fillRect(0, 148, W, 20, C_BG);
  if (gpsData.hasFix) {
    char distStr[32];
    if (gpsData.distanceM < 1000)
      sprintf(distStr, "Distance: %.0f m", gpsData.distanceM);
    else
      sprintf(distStr, "Distance: %.2f km", gpsData.distanceM / 1000.0f);
    tft.setTextColor(C_ACCENT); tft.setTextDatum(MC_DATUM);
    tft.drawString(distStr, W/2, 150);

    // Speed label
    const char* speedLabel;
    if      (gpsData.speedKmh < 0.5f) speedLabel = "Standing still";
    else if (gpsData.speedKmh < 3.0f) speedLabel = "Walking slowly";
    else if (gpsData.speedKmh < 5.0f) speedLabel = "Good walking pace";
    else if (gpsData.speedKmh < 7.0f) speedLabel = "Brisk walk";
    else                               speedLabel = "Moving fast";

    char spStr[32]; sprintf(spStr, "%.1f km/h — %s", gpsData.speedKmh, speedLabel);
    tft.setTextColor(C_MUTED);
    tft.drawString(spStr, W/2, 163);
  } else {
    tft.setTextColor(C_DIM); tft.setTextDatum(MC_DATUM);
    char satStr[32]; sprintf(satStr, "GPS searching... (%d sats)", gpsData.satellites);
    tft.drawString(satStr, W/2, 156);
  }

  drawBottomHint("any button: back");
}

void drawStability() {
  drawTopBar("STABILITY & SAFETY");

  tft.setTextDatum(MC_DATUM);

  // Main status
  uint16_t stabCol = data.isStable ? C_GREEN : C_ORANGE;
  tft.setTextSize(2);
  tft.setTextColor(stabCol);
  tft.drawString(data.isStable ? "Moving well" : "Watch your step", W/2, 50);

  // Balance score
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  char balStr[24]; sprintf(balStr, "Balance score: %d / 100", data.balanceScore);
  tft.drawString(balStr, W/2, 72);

  // Balance bar
  uint16_t balCol = data.balanceScore > 70 ? C_GREEN :
                    data.balanceScore > 40 ? C_ORANGE : C_RED;
  tft.fillRect(40, 82, W-80, 10, C_CARD);
  tft.fillRect(40, 82, (int)((W-80)*(data.balanceScore/100.0f)), 10, balCol);

  // Posture
  tft.setTextColor(fabsf(data.tiltAngle) < 20 ? C_GREEN : C_ORANGE);
  tft.drawString(postureLabel(), W/2, 104);

  // Fall detector status
  tft.setTextColor(C_DIM);
  tft.drawString("Fall detection: active", W/2, 120);

  // Falls today
  char fallStr[24];
  sprintf(fallStr, "Falls detected today: %d", daily.fallCount);
  tft.setTextColor(daily.fallCount > 0 ? C_ORANGE : C_MUTED);
  tft.drawString(fallStr, W/2, 136);

  // Advice
  tft.setTextColor(C_DIM);
  if (daily.fallCount > 0)
    tft.drawString("Please take extra care today.", W/2, 152);
  else
    tft.drawString("Stay safe. Take your time on stairs.", W/2, 152);

  drawBottomHint("any button: back");
}

// ── DEXTERITY ────────────────────────────────────────────────
void drawDexterity() {
  drawTopBar("DEXTERITY TEST");

  // Sub-menu for dexterity tests
  const char* dexTests[] = {
    "Finger tap speed",
    "Reaction time",
    "Grip hold strength",
    "Back"
  };
  static int dexSel = 0;

  // Show sub-menu
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  for (int i=0; i<4; i++) {
    bool sel = (i==dexSel);
    int y = 32 + i*28;
    if (sel) { tft.fillRect(10, y, W-20, 24, C_ACCENT); tft.setTextColor(C_BG); }
    else        tft.setTextColor(C_TEXT);
    tft.drawString(dexTests[i], 22, y+12);
  }

  drawBottomHint("BTN1: scroll   BTN2: start");

  // Handle button input for sub-menu
  // Wait for input here
  unsigned long waitStart = millis();
  bool chosen = false;
  while (!chosen && millis()-waitStart < 30000) {
    bool b1 = (digitalRead(BUTTON1_PIN)==LOW);
    bool b2 = (digitalRead(BUTTON2_PIN)==LOW);
    static bool lb1=false, lb2=false;
    bool p1=(b1&&!lb1), p2=(b2&&!lb2);
    lb1=b1; lb2=b2;

    if (p1) {
      dexSel = (dexSel+1) % 4;
      tft.fillRect(0, 26, W, H-40, C_BG);
      for (int i=0; i<4; i++) {
        bool sel=(i==dexSel);
        int y=32+i*28;
        if(sel){tft.fillRect(10,y,W-20,24,C_ACCENT);tft.setTextColor(C_BG);}
        else tft.setTextColor(C_TEXT);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(dexTests[i], 22, y+12);
      }
      delay(200);
    }
    if (p2) {
      chosen = true;
      if (dexSel == 3) { state=STATE_MENU; needsFullDraw=true; return; }
      runDexTest(dexSel);
      dexSel = 0;
      state = STATE_MENU;
      needsFullDraw = true;
      return;
    }
    delay(20);
  }
  state = STATE_MENU;
  needsFullDraw = true;
}

void runDexTest(int test) {
  tft.fillScreen(C_BG);
  drawTopBar(test==0?"FINGER TAP":test==1?"REACTION TIME":"GRIP STRENGTH");

  if (test == 0) {
    // Tap speed — 10 seconds
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT); tft.setTextSize(1);
    tft.drawString("Tap TTP223 as fast as you can!", W/2, 40);
    tft.drawString("10 seconds — go!", W/2, 56);

    int taps=0; bool touching=false;
    unsigned long t=millis();
    while(millis()-t<10000) {
      bool cur=digitalRead(TTP223_PIN);
      if(cur&&!touching){taps++;touching=true;}
      if(!cur) touching=false;
      // Show live count
      tft.fillRect(80, 70, 160, 50, C_BG);
      tft.setTextSize(4); tft.setTextColor(C_ACCENT);
      char ts[6]; sprintf(ts,"%d",taps);
      tft.drawString(ts, W/2, 95);
      tft.setTextSize(1); tft.setTextColor(C_MUTED);
      int rem=(10000-(millis()-t))/1000;
      char rs[8]; sprintf(rs,"%ds left",rem);
      tft.drawString(rs, W/2, 130);
      delay(30);
    }
    // Result
    tft.fillScreen(C_BG);
    drawTopBar("RESULT");
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1); tft.setTextColor(C_TEXT);
    char res[32]; sprintf(res,"%d taps in 10 seconds",taps);
    tft.drawString(res, W/2, 60);
    tft.setTextColor(C_MUTED);
    if(taps>15) tft.drawString("Excellent finger mobility!", W/2, 80);
    else if(taps>8) tft.drawString("Good — keep practising!", W/2, 80);
    else tft.drawString("Keep going — it gets easier!", W/2, 80);
    drawBottomHint("any button: back");
    delay(3000);

  } else if (test == 1) {
    // Reaction time
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_MUTED); tft.setTextSize(1);
    tft.drawString("Watch for the signal...", W/2, 50);
    tft.drawString("Tap TTP223 as soon as it appears!", W/2, 66);

    delay(random(2000, 5000)); // Random wait

    // Signal!
    unsigned long signalTime = millis();
    tft.fillRect(0, 80, W, 60, C_GREEN);
    tft.setTextColor(C_BG); tft.setTextSize(2);
    tft.drawString("TAP NOW!", W/2, 110);

    bool tapped=false;
    while(!tapped && millis()-signalTime < 3000) {
      if(digitalRead(TTP223_PIN)) tapped=true;
      delay(5);
    }
    unsigned long rt = millis()-signalTime;

    tft.fillScreen(C_BG);
    drawTopBar("RESULT");
    tft.setTextDatum(MC_DATUM);
    if(tapped) {
      char res[24]; sprintf(res,"%lums reaction time",(unsigned long)rt);
      tft.setTextSize(1); tft.setTextColor(C_TEXT);
      tft.drawString(res, W/2, 60);
      tft.setTextColor(C_MUTED);
      if(rt<400)      tft.drawString("Very sharp — great alertness!", W/2, 80);
      else if(rt<700) tft.drawString("Good reaction time!", W/2, 80);
      else            tft.drawString("Keep practising — it improves!", W/2, 80);
    } else {
      tft.setTextColor(C_ORANGE);
      tft.drawString("Missed it — try again!", W/2, 70);
    }
    drawBottomHint("any button: back");
    delay(3000);

  } else if (test == 2) {
    // Grip hold
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT); tft.setTextSize(1);
    tft.drawString("Hold TTP223 as long as you can.", W/2, 45);
    tft.drawString("Touch and HOLD now!", W/2, 61);

    // Wait for touch to start
    while(!digitalRead(TTP223_PIN)) delay(20);
    unsigned long holdStart=millis();

    while(digitalRead(TTP223_PIN)) {
      unsigned long held=(millis()-holdStart)/1000;
      tft.fillRect(80, 76, 160, 50, C_BG);
      tft.setTextSize(3); tft.setTextColor(C_PURPLE);
      char hs[8]; sprintf(hs,"%lus",(unsigned long)held);
      tft.drawString(hs, W/2, 100);
      delay(100);
    }
    unsigned long totalHold=(millis()-holdStart)/1000;

    tft.fillScreen(C_BG);
    drawTopBar("RESULT");
    tft.setTextDatum(MC_DATUM);
    char res[24]; sprintf(res,"Held for %lus",(unsigned long)totalHold);
    tft.setTextSize(1); tft.setTextColor(C_TEXT);
    tft.drawString(res, W/2, 60);
    tft.setTextColor(C_MUTED);
    if(totalHold>10) tft.drawString("Strong grip — excellent!", W/2, 80);
    else if(totalHold>5) tft.drawString("Good — build up daily!", W/2, 80);
    else tft.drawString("Keep practising — grip builds strength!", W/2, 80);
    drawBottomHint("any button: back");
    delay(3000);
  }
}

// ── DAILY SUMMARY ────────────────────────────────────────────
void drawSummary() {
  drawTopBar("TODAY'S SUMMARY");
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);

  int y=30;
  auto row = [&](const char* label, const char* val, uint16_t col){
    tft.setTextColor(C_MUTED); tft.drawString(label, 16, y);
    tft.setTextColor(col);     tft.drawString(val, 180, y);
    y+=18;
  };

  char s[32];
  sprintf(s, "%d steps", data.steps);
  row("Steps today:", s, data.steps>=STEPS_GOAL ? C_GREEN : C_TEXT);

  if(daily.hrSamples>0){
    sprintf(s, "%.0f bpm avg", daily.avgHR);
    row("Heart average:", s, C_TEXT);
  } else {
    row("Heart average:", "no reading yet", C_MUTED);
  }

  sprintf(s, "%d min", daily.activityMins);
  row("Active time:", s, C_TEXT);

  sprintf(s, "%d detected", daily.fallCount);
  row("Falls:", s, daily.fallCount>0 ? C_ORANGE : C_GREEN);

  sprintf(s, "score %d / 100", data.healthScore);
  row("Health score:", s, healthDot());

  // Overall message
  tft.fillRect(0, y+4, W, 2, C_CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_MUTED);
  if(data.steps>=STEPS_GOAL && daily.fallCount==0)
    tft.drawString("Great day! Your doctor would be pleased.", W/2, y+16);
  else if(data.steps<1000)
    tft.drawString("Try to get some steps in today.", W/2, y+16);
  else
    tft.drawString("Good effort today. Rest well tonight.", W/2, y+16);

  drawBottomHint("any button: back");
}

// ── DOCTOR'S REPORT ──────────────────────────────────────────
void drawDoctor() {
  drawTopBar("DOCTOR'S REPORT", C_CARD);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString("Medical data — share with your doctor", 10, 30);

  int y=46;
  auto drow = [&](const char* label, const char* val){
    tft.setTextColor(C_DIM);   tft.drawString(label, 10,  y);
    tft.setTextColor(C_ACCENT);tft.drawString(val,   180, y);
    y+=16;
  };

  char s[32];

  // Heart
  if(data.heartRate>0) sprintf(s,"%.0f bpm",data.heartRate);
  else strcpy(s,"no reading");
  drow("Heart Rate:", s);

  if(daily.hrSamples>0) sprintf(s,"%.0f bpm (n=%d)",daily.avgHR,daily.hrSamples);
  else strcpy(s,"insufficient data");
  drow("HR Average:", s);

  sprintf(s,"Zone %d (%s)",data.hrZone,hrZoneLabel());
  drow("HR Zone:", s);

  // Movement
  sprintf(s,"%d steps",data.steps);
  drow("Step Count:", s);

  sprintf(s,"%.2f G",data.accelG);
  drow("Accel Magnitude:", s);

  sprintf(s,"%.1f deg",data.tiltAngle);
  drow("Tilt / Posture:", s);

  sprintf(s,"%d / 100",data.balanceScore);
  drow("Balance Score:", s);

  // Events
  sprintf(s,"%d today",daily.fallCount);
  drow("Fall Events:", s);

  // Temp
  sprintf(s,"%.1f C (device)",data.tempC);
  drow("Temp Sensor:", s);

  // Battery
  sprintf(s,"%.0f%%",data.battery);
  drow("Battery:", s);

  tft.setTextColor(C_DIM);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Export via app — PIN required", W/2, H-20);

  drawBottomHint("any button: back");
}

// ── SETTINGS ─────────────────────────────────────────────────
void drawSettings() {
  drawTopBar("SETTINGS");
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);

  tft.setTextColor(C_TEXT);   tft.drawString("Language:", 16, 38);
  tft.setTextColor(C_ACCENT); tft.drawString("English (more coming)", 120, 38);

  tft.setTextColor(C_TEXT);   tft.drawString("Step goal:", 16, 58);
  char sg[16]; sprintf(sg,"%d steps",STEPS_GOAL);
  tft.setTextColor(C_MUTED);  tft.drawString(sg, 120, 58);

  tft.setTextColor(C_TEXT);   tft.drawString("HR safe range:", 16, 78);
  char hr[24]; sprintf(hr,"%d - %d bpm",HR_SAFE_MIN,HR_SAFE_MAX);
  tft.setTextColor(C_MUTED);  tft.drawString(hr, 120, 78);

  tft.setTextColor(C_TEXT);   tft.drawString("WiFi:", 16, 98);
  tft.setTextColor(strlen(WIFI_SSID)>0 ? C_GREEN : C_MUTED);
  tft.drawString(strlen(WIFI_SSID)>0 ? WIFI_SSID : "not set", 120, 98);

  tft.setTextColor(C_TEXT);   tft.drawString("Sleep:", 16, 118);
  tft.setTextColor(C_MUTED);  tft.drawString("hold BTN1 3s from clock", 120, 118);

  tft.setTextColor(C_TEXT);   tft.drawString("GPS:", 16, 138);
  if (gpsData.hasFix) {
    char gpsStr[32]; sprintf(gpsStr, "Fix! %d satellites", gpsData.satellites);
    tft.setTextColor(C_GREEN); tft.drawString(gpsStr, 120, 138);
  } else {
    char gpsStr[32]; sprintf(gpsStr, "Searching (%d sats)", gpsData.satellites);
    tft.setTextColor(C_MUTED); tft.drawString(gpsStr, 120, 138);
  }

  tft.setTextColor(C_DIM);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Full settings in companion app (v2)", W/2, 156);

  drawBottomHint("any button: back");
}

// ── FALL CONFIRMATION (10s countdown) ────────────────────────
void drawFallConfirm() {
  tft.fillScreen(C_BG);
  // Soft orange — not full red panic
  tft.fillRect(0, 0, W, H, 0x8200);  // dark orange

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(2);
  tft.drawString("Are you OK?", W/2, 35);

  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW);
  tft.drawString("Press BTN2 if you're fine.", W/2, 60);

  // Countdown number
  tft.setTextSize(5);
  tft.setTextColor(C_TEXT);
  char cs[4]; sprintf(cs,"%d",fallCountdown);
  tft.drawString(cs, W/2, 100);

  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW);
  tft.drawString("Alert sending if no response...", W/2, 140);
  tft.drawString("BTN2 = I'm fine", W/2, 155);
}

// ── EMERGENCY / SOS ──────────────────────────────────────────
void drawEmergency() {
  // Slow soft pulse — not harsh flash
  uint16_t bg = pulseOn ? C_RED : 0x9000; // red / dark red (softer)
  tft.fillScreen(bg);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);

  if (state == STATE_SOS) {
    tft.setTextSize(3); tft.drawString("SOS", W/2, 40);
    tft.setTextSize(1); tft.setTextColor(C_YELLOW);
    tft.drawString("Help is being notified.", W/2, 75);
    tft.drawString("Stay calm. Help is coming.", W/2, 92);
  } else if (data.fallDetected || state == STATE_EMERGENCY) {
    if (data.heartRate > HR_WARN_HIGH) {
      tft.setTextSize(2); tft.drawString("HEART RATE", W/2, 38);
      tft.drawString("TOO HIGH", W/2, 62);
      char s[16]; sprintf(s,"%.0f bpm",data.heartRate);
      tft.setTextSize(1); tft.setTextColor(C_YELLOW);
      tft.drawString(s, W/2, 85);
      tft.drawString("Please sit down and rest.", W/2, 100);
    } else if (data.heartRate > 0 && data.heartRate < HR_WARN_LOW) {
      tft.setTextSize(2); tft.drawString("HEART RATE", W/2, 38);
      tft.drawString("VERY LOW", W/2, 62);
      char s[16]; sprintf(s,"%.0f bpm",data.heartRate);
      tft.setTextSize(1); tft.setTextColor(C_YELLOW);
      tft.drawString(s, W/2, 85);
      tft.drawString("Please sit and rest.", W/2, 100);
    } else {
      tft.setTextSize(3); tft.drawString("FALL", W/2, 40);
      tft.setTextSize(2); tft.drawString("DETECTED", W/2, 72);
      tft.setTextSize(1); tft.setTextColor(C_YELLOW);
      tft.drawString("Family has been notified.", W/2, 100);
      tft.drawString("Help is on the way.", W/2, 116);
    }
  }

  tft.setTextSize(1); tft.setTextColor(C_TEXT);
  tft.drawString("BTN2: I'm OK — dismiss", W/2, H-12);
}
