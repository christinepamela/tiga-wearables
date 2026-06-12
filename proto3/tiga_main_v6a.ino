// ============================================================
// TIGA — Main Application v6a
// Wearable health tracker for elderly users
//
// What's new in v6a:
//   - MAX30102 driver (SparkFun MAX3010x library)
//       HR via beat detection, SpO2 via red/IR ratio
//       Finger/wearing detection via IR signal validity
//       Replaces v5.2 analog pulse sensor on GPIO01 entirely
//   - BMP280 driver (Adafruit BMP280 library)
//       Pressure → altitude (metres above sea level)
//       Floor counting (±3m threshold per floor)
//   - Buzzer alert patterns (GPIO13, passive buzzer via 100Ω)
//       goal_reached, fall_alert, sos_confirm, low_battery
//   - Vibration motor patterns (GPIO12, 2N2222 switch)
//       gentle_pulse, triple_buzz, heartbeat, long_buzz
//   - Multi-modal alert system
//       Severity tiers combining buzzer + motor + display
//   - Wearing detection now real — MAX30102 IR validity
//
// What was removed vs v5.2:
//   - Analog pulse sensor on PULSE_PIN (GPIO01) — gone
//   - SW420_PIN, TTP223_PIN defines — cleaned out
//   - Old pulseBuf / pulseBaseline / pulseThreshold block — gone
//
// The spo2_algorithm.h file no longer exists in v1.1.2.
// heartRate.h (and checkForBeat) still exists — keep it.
//
// SpO2 is now computed from the red/IR ratio directly.
// R = (red_AC/red_DC) / (ir_AC/ir_DC)
// SpO2 ≈ 110 - 25 * R  (linear approximation, valid 90-100%)
// This is exactly what the old maxim algorithm did internally.
//
// Libraries required (install via Arduino IDE Library Manager):
//   - SparkFun MAX3010x Pulse and Proximity Sensor Library
//     (by SparkFun Electronics)
//   - Adafruit BMP280 Library
//     (by Adafruit — install all dependencies when prompted)
//   - TinyGPS++ by Mikal Hart (same as v5.2)
//   - MPU6050 by Electronic Cats (same as v5.2)
//   - TFT_eSPI (same as v5.2)
//
// Confirmed pin map (T-Display-S3):
//   MAX30102 SDA→18  SCL→17  VCC→3V3  GND→GND  INT→nc
//   MPU6050  SDA→18  SCL→17  VCC→3V3  GND→GND
//   BMP280   SDA→18  SCL→17  VCC→3V3  GND→GND
//   Buzzer   GPIO13 → 100Ω → buzzer (+) → GND
//   Motor    GPIO12 → 1kΩ → 2N2222 base
//   Button1  GPIO21 → GND  (scroll / hold 3s = sleep)
//   Button2  GPIO16 → GND  (select / hold 3s = reset session)
//   Both     BTN1+BTN2 = export session to Serial
//   GPS      VCC→3V3  GND→GND  TX→GPIO44  RX→GPIO43
//   Battery  GPIO04 (internal ADC)
//   LCD pwr  GPIO15 (must HIGH)
// ============================================================

#include <TFT_eSPI.h>
#include <Wire.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>
#include <TinyGPSPlus.h>
#include "MAX30105.h"         // SparkFun MAX3010x library
#include "heartRate.h"        // SparkFun beat detection helper
#include <Adafruit_BMP280.h>
#include "tiga_ble.h"

// ── GPS ──────────────────────────────────────────────────────
#define GPS_RX_PIN   44
#define GPS_TX_PIN   43
#define GPS_BAUD     9600

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

struct GPSData {
  bool    hasFix      = false;
  double  lat         = 0;
  double  lng         = 0;
  float   speedKmh    = 0;
  float   distanceM   = 0;
  int     satellites  = 0;
  double  lastLat     = 0;
  double  lastLng     = 0;
  bool    lastValid   = false;
} gpsData;

// ── Deep sleep ───────────────────────────────────────────────
#define SLEEP_HOLD_MS  3000
#define WAKE_PIN       GPIO_NUM_21

// ── WiFi / NTP ───────────────────────────────────────────────
const char* WIFI_SSID        = "Cronus";
const char* WIFI_PASS        = "Garfield77";
const char* NTP_SERVER       = "pool.ntp.org";
const long  GMT_OFFSET_SEC   = 28800;   // GMT+8 Malaysia
const int   DAYLIGHT_OFFSET  = 0;

// ── Pins ─────────────────────────────────────────────────────
#define BUTTON1_PIN  21
#define BUTTON2_PIN  16
#define BAT_ADC_PIN   4
#define LCD_PWR_PIN  15
#define TFT_BL       38
#define BUZZER_PIN   13
#define MOTOR_PIN    12
#define I2C_SDA      18
#define I2C_SCL      17

// ── Display ──────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
#define W 320
#define H 170

// ── Colours (RGB565) ─────────────────────────────────────────
#define C_BG      0x0000
#define C_CARD    0x18E3
#define C_CARD2   0x2104
#define C_TEXT    0xFFFF
#define C_MUTED   0x8410
#define C_DIM     0x4208
#define C_ACCENT  0x051D
#define C_GREEN   0x0680
#define C_ORANGE  0xFD20
#define C_RED     0xF800
#define C_YELLOW  0xFFE0
#define C_PURPLE  0x801F
#define C_PINK    0xF81F

// ── Health thresholds ────────────────────────────────────────
#define HR_SAFE_MIN   50
#define HR_SAFE_MAX  100
#define HR_WARN_LOW   45
#define HR_WARN_HIGH 110
#define STEPS_GOAL  3000
#define FALL_G       3.0f
#define STABLE_G     1.3f

// ── App states ───────────────────────────────────────────────
enum AppState {
  STATE_CLOCK,
  STATE_HEALTH,
  STATE_MENU,
  STATE_HEART,
  STATE_FITNESS,
  STATE_STABILITY,
  STATE_DEXTERITY,
  STATE_SUMMARY,
  STATE_DOCTOR,
  STATE_SETTINGS,
  STATE_EMERGENCY,
  STATE_SOS,
  STATE_FALL_CONFIRM
};

AppState state     = STATE_CLOCK;
AppState lastState = STATE_EMERGENCY;
bool needsFullDraw = true;

// ── Health data ──────────────────────────────────────────────
struct HealthData {
  float heartRate    = 0;
  uint8_t spO2       = 0;       // % oxygen saturation (0 = no reading)
  bool  spO2Valid    = false;
  int   steps        = 0;
  float accelG       = 1.0f;
  float tiltAngle    = 0;
  bool  isStable     = true;
  bool  fallDetected = false;
  float battery      = 100.0f;
  int   healthScore  = 85;
  bool  wearing      = false;   // real now — MAX30102 IR validity
  float tempC        = 33.0f;   // MPU die temp (chip, not body)
  int   hrZone       = 0;
  int   activityMins = 0;
  int   balanceScore = 100;
  float altitudeM    = 0;       // BMP280 altitude metres
  int   floorsUp     = 0;       // floors climbed this session
  float pressureHPa  = 1013.0f; // BMP280 raw pressure
} data;

struct DailyData {
  int   peakSteps    = 0;
  float avgHR        = 0;
  int   hrSamples    = 0;
  int   fallCount    = 0;
  int   activityMins = 0;
  int   sosCount     = 0;
} daily;

struct PrevData {
  float heartRate    = -1;
  uint8_t spO2       = 0;
  int   steps        = -1;
  bool  isStable     = true;
  float tempC        = -1;
  int   healthScore  = -1;
  bool  wearing      = false;
  float battery      = -1;
  int   minute       = -1;
  float altitudeM    = -1;
} prev;

// ── Sensors ──────────────────────────────────────────────────
MPU6050         mpu;
MAX30105        max30102;
Adafruit_BMP280 bmp280;

bool mpuOK    = false;
bool maxOK    = false;
bool bmpOK    = false;

// MPU health tracking
unsigned long mpuReconnectCount   = 0;
unsigned long mpuLastFailMs       = 0;
unsigned long mpuConsecutiveZeros = 0;
bool          mpuHealthDegraded   = false;

// Step detection
#define STEP_BUF_SIZE 10
float         stepMagBuf[STEP_BUF_SIZE];
int           stepBufIdx    = 0;
float         stepBaseline  = 1.0f;
bool          stepAboveThr  = false;
unsigned long stepDebounce  = 0;

// Session
bool          sessionAnchored = false;
unsigned long btn2HoldStart   = 0;
bool          btn2Held        = false;
#define RESET_HOLD_MS 3000

// ── MAX30102 beat detection ───────────────────────────────────
// SparkFun heartRate.h uses a circular buffer of recent IR peaks
// to estimate BPM. We feed it one sample at a time.
#define MAX_RATE_SIZE 4          // rolling average over last 4 beats
byte    rates[MAX_RATE_SIZE];    // BPM values ring buffer
byte    rateSpot    = 0;
long    lastBeatMax = 0;         // millis of last beat
float   beatsPerMinute  = 0;
float   beatAvg         = 0;

// SpO2 algorithm needs 100-sample buffers
// Using 25 samples at 25Hz = ~4s per reading (lighter on RAM)
#define SPO2_SAMPLES   100
#define SPO2_SAMPLE_RATE 100     // must match what we tell the sensor

// IR threshold: below this = no finger present
#define IR_FINGER_THRESHOLD  50000UL

// ── BMP280 altitude tracking ─────────────────────────────────
float altitudeBaseline  = 0;    // set on first valid reading
bool  altBaselineSet    = false;
float lastFloorAlt      = 0;
#define FLOOR_HEIGHT_M  3.0f    // metres per floor

// ── Step counter ─────────────────────────────────────────────
int   stepCount  = 0;
unsigned long lastStep = 0;

// ── Fall detection ───────────────────────────────────────────
bool  inFall     = false;
unsigned long fallTime        = 0;
unsigned long fallConfirmStart = 0;
int   fallCountdown = 10;

// ── Activity timing ──────────────────────────────────────────
unsigned long activityStart = 0;
bool inActivity = false;

// ── Buttons ──────────────────────────────────────────────────
bool btn1Last = false, btn2Last = false;
bool btn1Pressed = false, btn2Pressed = false;
unsigned long btn1HoldStart = 0;
bool btn1Held = false;

// ── Menu ─────────────────────────────────────────────────────
const char* menuItems[] = {
  "Heart", "Fitness", "Stability",
  "Dexterity", "Summary", "Doctor report",
  "Settings", "SOS", "Back"
};
#define MENU_COUNT 9
int menuSel = 0;

// ── SOS / emergency ──────────────────────────────────────────
unsigned long lastPulse = 0;
bool pulseOn = true;

// ── Session tracking ─────────────────────────────────────────
unsigned long sessionStart = 0;
float   sessionPeakHR  = 0;
float   sessionLowHR   = 999;
int     sessionSteps   = 0;

// ── Time ─────────────────────────────────────────────────────
bool timeSet = false;
int  displayHour = 0, displayMin = 0, displaySec = 0;
int  displayDay = 14, displayMonth = 4, displayYear = 2026;
const char* dayNames[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* monthNames[] = {"","Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};

// ============================================================
// ALERT SYSTEM
// Severity tiers:
//   LOW    — gentle motor pulse only (non-intrusive)
//   MEDIUM — motor pattern + short buzzer tone
//   HIGH   — full buzzer pattern + strong motor
// ============================================================

// ── Buzzer patterns ──────────────────────────────────────────
// Each call is non-blocking-friendly via millis() but we use
// simple blocking here — patterns are short (<1s) and only
// fire on discrete events, not in the sensor loop.

void buzzerGoalReached() {
  // Three rising tones — celebratory
  tone(BUZZER_PIN, 880, 100);  delay(130);
  tone(BUZZER_PIN, 1047, 100); delay(130);
  tone(BUZZER_PIN, 1319, 200); delay(220);
  noTone(BUZZER_PIN);
}

void buzzerFallAlert() {
  // Urgent descending double-beep
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 1500, 150); delay(180);
    tone(BUZZER_PIN, 800, 150);  delay(180);
  }
  noTone(BUZZER_PIN);
}

void buzzerSosConfirm() {
  // SOS in morse: ... --- ...
  int dot = 100, dash = 300, gap = 120, letterGap = 360;
  // S (...)
  for (int i=0;i<3;i++){tone(BUZZER_PIN,1000,dot);delay(dot+gap);}
  delay(letterGap);
  // O (---)
  for (int i=0;i<3;i++){tone(BUZZER_PIN,1000,dash);delay(dash+gap);}
  delay(letterGap);
  // S (...)
  for (int i=0;i<3;i++){tone(BUZZER_PIN,1000,dot);delay(dot+gap);}
  noTone(BUZZER_PIN);
}

void buzzerLowBattery() {
  // Two slow low beeps
  tone(BUZZER_PIN, 400, 400); delay(500);
  tone(BUZZER_PIN, 400, 400); delay(500);
  noTone(BUZZER_PIN);
}

// ── Motor patterns ───────────────────────────────────────────
void motorGentlePulse() {
  // Single soft pulse — worn confirmation, goal nudge
  digitalWrite(MOTOR_PIN, HIGH); delay(80);
  digitalWrite(MOTOR_PIN, LOW);
}

void motorTripleBuzz() {
  // Three short pulses — attention getter
  for (int i = 0; i < 3; i++) {
    digitalWrite(MOTOR_PIN, HIGH); delay(80);
    digitalWrite(MOTOR_PIN, LOW);  delay(100);
  }
}

void motorHeartbeat() {
  // Double-tap like a heartbeat — for HR alerts
  digitalWrite(MOTOR_PIN, HIGH); delay(60);
  digitalWrite(MOTOR_PIN, LOW);  delay(80);
  digitalWrite(MOTOR_PIN, HIGH); delay(60);
  digitalWrite(MOTOR_PIN, LOW);
}

void motorLongBuzz() {
  // Long continuous buzz — SOS / fall
  digitalWrite(MOTOR_PIN, HIGH); delay(600);
  digitalWrite(MOTOR_PIN, LOW);
}

// ── Alert tier functions ──────────────────────────────────────
void alertLow() {
  // Motor only — gentle, non-intrusive
  motorGentlePulse();
}

void alertMedium() {
  // Motor + short buzzer
  motorTripleBuzz();
  delay(100);
  tone(BUZZER_PIN, 1000, 200);
  noTone(BUZZER_PIN);
}

void alertHigh() {
  // Full buzzer pattern + strong motor — for fall / HR emergency
  motorLongBuzz();
  buzzerFallAlert();
}

void alertGoal() {
  motorTripleBuzz();
  buzzerGoalReached();
}

void alertSOS() {
  motorLongBuzz();
  buzzerSosConfirm();
}

// Track goal alert so we only fire it once per session
bool goalAlertFired = false;

// ── Low battery alert (fire once per session below 15%) ──────
bool lowBatAlertFired = false;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(LCD_PWR_PIN, OUTPUT);
  digitalWrite(LCD_PWR_PIN, HIGH);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(MOTOR_PIN, LOW);

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(C_BG);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  drawSplash();
  delay(2500);

  // ── I2C + sensors ────────────────────────────────────────
  i2cBusRecover();
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(200);

  // MPU6050
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

  // MAX30102
  // begin() returns false if sensor not found on I2C bus
  if (max30102.begin(Wire, I2C_SPEED_STANDARD)) {
    maxOK = true;
    // Sample rate 100Hz, 16 bit ADC, 411µs pulse width, range 16384
    max30102.setup(60,           // LED brightness 0-255 (60 = moderate)
                   4,            // sampleAverage: average 4 samples
                   2,            // ledMode: 2 = red + IR
                   SPO2_SAMPLE_RATE, // sampleRate: 100 Hz
                   411,          // pulseWidth: 411µs (best resolution)
                   16384);       // adcRange: 16384
    max30102.setPulseAmplitudeRed(60);
    max30102.setPulseAmplitudeIR(60);
    Serial.println("[TIGA] MAX30102 init OK");
  } else {
    maxOK = false;
    Serial.println("[TIGA] MAX30102 not found — check wiring at 0x57");
  }

  // BMP280
  // Default I2C address for Adafruit BMP280 breakout is 0x76
  if (bmp280.begin(0x76)) {
    bmpOK = true;
    // Recommended settings for indoor navigation / altitude
    bmp280.setSampling(
      Adafruit_BMP280::MODE_NORMAL,
      Adafruit_BMP280::SAMPLING_X2,   // temperature oversampling
      Adafruit_BMP280::SAMPLING_X16,  // pressure oversampling (high res)
      Adafruit_BMP280::FILTER_X16,    // IIR filter (smooths noise)
      Adafruit_BMP280::STANDBY_MS_500 // 500ms standby
    );
    Serial.println("[TIGA] BMP280 init OK");
  } else {
    bmpOK = false;
    Serial.println("[TIGA] BMP280 not found — check wiring at 0x76");
  }

  // Step detection baseline
  for (int i = 0; i < STEP_BUF_SIZE; i++) stepMagBuf[i] = 1.0f;

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
      Serial.println("[TIGA] WiFi failed — manual time");
    }
  }

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[TIGA] GPS UART started");

  needsFullDraw = true;
  state = STATE_CLOCK;
  sessionStart = millis();

  // Startup confirmation buzz
  motorGentlePulse();

  Serial.println("[TIGA] v6a boot complete");
  Serial.printf("[TIGA] Sensors: MPU=%s  MAX=%s  BMP=%s\n",
                mpuOK?"OK":"FAIL", maxOK?"OK":"FAIL", bmpOK?"OK":"FAIL");

  bleSetup();
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
    readMPUSensor();
    readBMP280();
    readBattery();
  }

  // MAX30102 needs faster sampling — run every loop iteration
  readMAX30102();

  // GPS
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  static unsigned long lastGPS = 0;
  if (millis() - lastGPS >= 2000) {
    lastGPS = millis();
    readGPS();
  }

  // Time tick
  static unsigned long lastTimeTick = 0;
  if (millis() - lastTimeTick >= 1000) {
    lastTimeTick = millis();
    tickTime();
  }

  // Goal alert — fire once when steps cross the goal
  if (!goalAlertFired && data.steps >= STEPS_GOAL) {
    goalAlertFired = true;
    alertGoal();
  }

  // Low battery alert — fire once per session
  if (!lowBatAlertFired && data.battery > 0 && data.battery < 15.0f) {
    lowBatAlertFired = true;
    alertLow();
    buzzerLowBattery();
  }

  // State change → full redraw
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

  // Partial updates every second
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    if (state == STATE_CLOCK)  drawClockPartial();
    if (state == STATE_HEALTH) drawHealthPartial();
    copyPrev();
    bleNotify();
  }

  // Emergency pulse animation
  if ((state == STATE_EMERGENCY || state == STATE_SOS) &&
      millis() - lastPulse > 800) {
    lastPulse = millis();
    pulseOn = !pulseOn;
    drawEmergency();
  }

  // Fall confirm countdown
  if (state == STATE_FALL_CONFIRM) {
    int elapsed   = (millis() - fallConfirmStart) / 1000;
    int remaining = 10 - elapsed;
    if (remaining != fallCountdown) {
      fallCountdown = remaining;
      drawFallConfirm();
    }
    if (remaining <= 0) {
      daily.fallCount++;
      alertHigh();
      state = STATE_EMERGENCY;
      needsFullDraw = true;
    }
  }

  delay(20);
}

// ── copyPrev ─────────────────────────────────────────────────
void copyPrev() {
  prev.heartRate   = data.heartRate;
  prev.spO2        = data.spO2;
  prev.steps       = data.steps;
  prev.isStable    = data.isStable;
  prev.tempC       = data.tempC;
  prev.healthScore = data.healthScore;
  prev.wearing     = data.wearing;
  prev.battery     = data.battery;
  prev.minute      = displayMin;
  prev.altitudeM   = data.altitudeM;
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
  if (timeSet) { updateTime(); return; }
  displaySec++;
  if (displaySec >= 60) { displaySec = 0; displayMin++; }
  if (displayMin >= 60) { displayMin = 0; displayHour++; }
  if (displayHour >= 24) displayHour = 0;
}

// ============================================================
// GPS
// ============================================================
void readGPS() {
  gpsData.hasFix     = gps.location.isValid() && gps.location.age() < 3000;
  gpsData.satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;

  if (gpsData.hasFix) {
    double lat = gps.location.lat();
    double lng = gps.location.lng();
    gpsData.lat = lat;
    gpsData.lng = lng;
    if (gps.speed.isValid()) {
      float raw = gps.speed.kmph();
      gpsData.speedKmh = (raw < 0.5f) ? 0.0f : raw;
    }
    if (gpsData.lastValid) {
      double dist = TinyGPSPlus::distanceBetween(
        gpsData.lastLat, gpsData.lastLng, lat, lng);
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
  } else {
    gpsData.speedKmh = 0;
  }
}

// ============================================================
// I2C BUS RECOVERY (unchanged from v5.2)
// ============================================================
void i2cBusRecover() {
  pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
  pinMode(I2C_SDA, INPUT_PULLUP);
  if (digitalRead(I2C_SDA) == HIGH) return;
  Serial.println("[TIGA] I2C SDA stuck — recovering");
  for (int i = 0; i < 9; i++) {
    digitalWrite(I2C_SCL, LOW);  delayMicroseconds(5);
    pinMode(I2C_SCL, INPUT_PULLUP); delayMicroseconds(5);
    pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
    if (digitalRead(I2C_SDA) == HIGH) break;
  }
  pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA, LOW);  delayMicroseconds(5);
  digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
  digitalWrite(I2C_SDA, HIGH); delayMicroseconds(5);
}

// ============================================================
// MPU6050 (largely unchanged from v5.2)
// ============================================================
bool readMPURaw(int16_t* ax, int16_t* ay, int16_t* az) {
  if (!mpuOK) {
    static unsigned long lastRecovery = 0;
    if (millis() - lastRecovery > 30000) {
      lastRecovery = millis();
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
        Serial.printf("[TIGA] MPU recovered (#%lu)\n", mpuReconnectCount);
      }
    }
    *ax = 0; *ay = 0; *az = 0;
    return false;
  }
  mpu.getAcceleration(ax, ay, az);
  if (*ax == 0 && *ay == 0 && *az == 0) {
    if (++mpuConsecutiveZeros >= 5) {
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

void readMPUSensor() {
  int16_t ax, ay, az;
  if (!readMPURaw(&ax, &ay, &az)) return;

  float g = sqrtf((float)ax*ax + (float)ay*ay + (float)az*az) / 8192.0f;
  if (g > 6.0f) return;  // reject unphysical spike

  data.accelG = g;
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
      motorHeartbeat();  // alert user before countdown starts
    } else {
      inFall = false;
    }
  }

  // Adaptive step detection
  stepMagBuf[stepBufIdx] = g;
  stepBufIdx = (stepBufIdx + 1) % STEP_BUF_SIZE;
  float sum = 0;
  for (int i = 0; i < STEP_BUF_SIZE; i++) sum += stepMagBuf[i];
  stepBaseline = sum / STEP_BUF_SIZE;
  float deviation = g - stepBaseline;

  if (!stepAboveThr && deviation > 0.15f && millis() - stepDebounce > 300) {
    stepAboveThr = true;
    stepDebounce = millis();
    stepCount++;
    lastStep = millis();
    if (stepCount > daily.peakSteps) daily.peakSteps = stepCount;
    if (!sessionAnchored) {
      sessionStart   = millis();
      sessionAnchored = true;
    }
  } else if (stepAboveThr && deviation < 0.08f) {
    stepAboveThr = false;
  }

  data.steps    = stepCount;
  data.isStable = (g < STABLE_G);

  // Balance score
  static float wobbleAccum   = 0;
  static int   wobbleSamples = 0;
  wobbleAccum += fabsf(g - 1.0f);
  if (++wobbleSamples >= 50) {
    data.balanceScore = (int)constrain(100 - (wobbleAccum/wobbleSamples)*200, 0, 100);
    wobbleAccum = 0; wobbleSamples = 0;
  }

  // Activity timing
  if (g > 1.15f && !inActivity) { inActivity = true; activityStart = millis(); }
  else if (g < 1.05f && inActivity) {
    inActivity = false;
    daily.activityMins += (millis() - activityStart) / 60000;
    data.activityMins = daily.activityMins;
  }

  // MPU die temperature
  data.tempC = (mpu.getTemperature() / 340.0f) + 36.53f;

  data.healthScore = calcScore();

  // HR emergency — 3 consecutive bad readings
  static int consecutiveBadHR = 0;
  if (data.heartRate > 0 && (state == STATE_CLOCK || state == STATE_HEALTH)) {
    if (data.heartRate > HR_WARN_HIGH || data.heartRate < HR_WARN_LOW) {
      if (++consecutiveBadHR >= 3) {
        alertHigh();
        state = STATE_EMERGENCY;
        needsFullDraw = true;
        consecutiveBadHR = 0;
      }
    } else {
      consecutiveBadHR = 0;
    }
  }
}

// ============================================================
// MAX30102
// Strategy: fill 100-sample buffer, then run SpO2 algorithm.
// Between algorithm runs, use beat detection for live BPM.
// Wearing detection: IR value < IR_FINGER_THRESHOLD = no finger.
// ============================================================
void readMAX30102() {
  if (!maxOK) return;

  long irValue  = max30102.getIR();
  long redValue = max30102.getRed();

  // Wearing detection — IR signal validity
  data.wearing = (irValue >= (long)IR_FINGER_THRESHOLD);

  if (!data.wearing) {
    data.heartRate = 0;
    beatsPerMinute = 0;
    beatAvg        = 0;
    data.spO2      = 0;
    data.spO2Valid = false;
    return;
  }

  // ── Beat detection for live BPM ────────────────────────────
  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeatMax;
    lastBeatMax = millis();
    beatsPerMinute = 60.0f / (delta / 1000.0f);

    if (beatsPerMinute >= 40 && beatsPerMinute <= 180) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= MAX_RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0; x < MAX_RATE_SIZE; x++) beatAvg += rates[x];
      beatAvg /= MAX_RATE_SIZE;
      data.heartRate = beatAvg;

      // Daily HR tracking
      daily.avgHR = (daily.avgHR * daily.hrSamples + data.heartRate)
                    / (daily.hrSamples + 1);
      daily.hrSamples++;
      if (data.heartRate > sessionPeakHR) sessionPeakHR = data.heartRate;
      if (data.heartRate < sessionLowHR)  sessionLowHR  = data.heartRate;
    }
  }

  // ── SpO2 from red/IR ratio ──────────────────────────────────
  // We maintain a rolling window of 25 samples (~1s at ~25 reads/loop).
  // AC component = peak - valley over the window.
  // DC component = mean over the window.
  // R = (redAC / redDC) / (irAC / irDC)
  // SpO2 ≈ 110 - 25 * R  (Maxim application note approximation)

  #define SPO2_WIN 25
  static long irWin[SPO2_WIN];
  static long redWin[SPO2_WIN];
  static uint8_t winIdx = 0;
  static bool winFull   = false;

  irWin[winIdx]  = irValue;
  redWin[winIdx] = redValue;
  winIdx++;
  if (winIdx >= SPO2_WIN) { winIdx = 0; winFull = true; }

  if (winFull) {
    long irMin = irWin[0],  irMax = irWin[0],  irSum = 0;
    long redMin = redWin[0], redMax = redWin[0], redSum = 0;

    for (int i = 0; i < SPO2_WIN; i++) {
      if (irWin[i]  < irMin)  irMin  = irWin[i];
      if (irWin[i]  > irMax)  irMax  = irWin[i];
      if (redWin[i] < redMin) redMin = redWin[i];
      if (redWin[i] > redMax) redMax = redWin[i];
      irSum  += irWin[i];
      redSum += redWin[i];
    }

    float irAC  = (float)(irMax  - irMin);
    float redAC = (float)(redMax - redMin);
    float irDC  = (float)(irSum  / SPO2_WIN);
    float redDC = (float)(redSum / SPO2_WIN);

    // Guard: avoid divide-by-zero and reject tiny signals
    if (irDC > 1000 && redDC > 1000 && irAC > 50 && redAC > 50) {
      float R = (redAC / redDC) / (irAC / irDC);
      float spo2 = 110.0f - 25.0f * R;

      // Plausibility gate — physiologically valid range only
      if (spo2 >= 80.0f && spo2 <= 100.0f) {
        // Smooth with previous reading
        if (data.spO2Valid) {
          data.spO2 = (uint8_t)(0.7f * data.spO2 + 0.3f * spo2);
        } else {
          data.spO2 = (uint8_t)spo2;
        }
        data.spO2Valid = true;
      }
      // If out of range, keep last valid reading rather than flashing 0
    }

    Serial.printf("[MAX] HR=%.0f bpm  SpO2=%d%%  valid=%d  R=%.3f\n",
                  data.heartRate, data.spO2, data.spO2Valid ? 1 : 0,
                  (irDC > 0 && redDC > 0)
                    ? (redAC / redDC) / (irAC / irDC) : 0.0f);
  }

  // HR zone update
  if      (data.heartRate == 0)             data.hrZone = 0;
  else if (data.heartRate < 60)             data.hrZone = 0;
  else if (data.heartRate < 75)             data.hrZone = 1;
  else if (data.heartRate <= HR_SAFE_MAX)   data.hrZone = 2;
  else                                      data.hrZone = 3;
}


// ============================================================
// BMP280
// Reads pressure, computes altitude, tracks floors climbed.
// ============================================================
void readBMP280() {
  if (!bmpOK) return;

  data.pressureHPa = bmp280.readPressure() / 100.0f; // Pa → hPa

  // Altitude from sea-level pressure formula
  // Using standard sea level pressure 1013.25 hPa
  float alt = bmp280.readAltitude(1013.25f);

  // Set baseline on first valid read
  if (!altBaselineSet && alt > -500 && alt < 9000) {
    altitudeBaseline = alt;
    lastFloorAlt     = alt;
    altBaselineSet   = true;
    Serial.printf("[BMP] Altitude baseline set: %.1f m\n", alt);
  }

  if (altBaselineSet) {
    data.altitudeM = alt - altitudeBaseline; // relative to start

    // Floor counting — count up only (don't count descents)
    if ((alt - lastFloorAlt) >= FLOOR_HEIGHT_M) {
      data.floorsUp++;
      lastFloorAlt = alt;
      Serial.printf("[BMP] Floor climbed! Total: %d  Alt: %.1f m\n",
                    data.floorsUp, alt);
      alertLow();  // gentle motor pulse on floor climbed
    }
  }

  Serial.printf("[BMP] Pressure: %.1f hPa  Altitude: %.1f m  Floors: %d\n",
                data.pressureHPa, data.altitudeM, data.floorsUp);
}

// ============================================================
// HEALTH SCORE
// ============================================================
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

// ── Label helpers ────────────────────────────────────────────
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
  if (data.heartRate == 0)              return "Place finger on sensor";
  if (data.heartRate < HR_WARN_LOW)     return "Very low — please rest";
  if (data.heartRate < HR_SAFE_MIN)     return "A little low";
  if (data.heartRate <= HR_SAFE_MAX)    return "Heart is steady";
  if (data.heartRate <= HR_WARN_HIGH)   return "Beating a bit fast";
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
  if (data.steps == 0) return "Let's start moving!";
  if (pct < 25)        return "Good start — keep going";
  if (pct < 50)        return "Making progress";
  if (pct < 75)        return "More than halfway!";
  if (pct < 100)       return "Almost there!";
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
// BUTTONS (unchanged from v5.2)
// ============================================================
void resetSession() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GREEN);
  tft.setTextSize(3);
  tft.drawString("SESSION", W/2, 45);
  tft.drawString("RESET", W/2, 78);
  tft.setTextSize(1);
  tft.setTextColor(C_TEXT);
  tft.drawString("Timer and counters cleared", W/2, 110);
  tft.setTextColor(C_MUTED);
  tft.drawString("Tracking starts from now", W/2, 126);
  int barY = H-22, barW = 200, barX = (W-barW)/2;
  tft.drawRect(barX, barY, barW, 6, C_MUTED);
  for (int i = 0; i <= barW; i += 4) {
    tft.fillRect(barX, barY, i, 6, C_GREEN);
    delay(40);
  }

  sessionStart       = millis();
  sessionAnchored    = true;
  stepCount          = 0;
  sessionSteps       = 0;
  sessionPeakHR      = 0;
  sessionLowHR       = 999;
  daily.activityMins = 0;
  daily.fallCount    = 0;
  daily.hrSamples    = 0;
  daily.avgHR        = 0;
  gpsData.distanceM  = 0;
  gpsData.lastValid  = false;
  mpuReconnectCount  = 0;
  mpuLastFailMs      = 0;
  mpuHealthDegraded  = false;
  goalAlertFired     = false;
  lowBatAlertFired   = false;
  // Reset altitude baseline for new session
  altBaselineSet     = false;
  data.floorsUp      = 0;
  lastFloorAlt       = 0;

  Serial.println("[TIGA] Session reset by user");
  delay(600);
  state = STATE_CLOCK;
  needsFullDraw = true;
}

void exportSession() {
  unsigned long dur = (millis() - sessionStart) / 1000;
  int durMin = dur / 60, durSec = dur % 60;

  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.setTextSize(1);
  tft.drawString("Exporting to Serial Monitor...", W/2, H/2 - 10);
  tft.setTextColor(C_MUTED);
  tft.drawString("Open Serial Monitor at 115200", W/2, H/2 + 8);

  Serial.println();
  Serial.println("=================================================");
  Serial.println("  TIGA WALK TEST REPORT — v6a");
  Serial.println("=================================================");
  Serial.printf ("  Date:      %02d/%02d/%04d  %02d:%02d\n",
                  displayDay, displayMonth, displayYear,
                  displayHour, displayMin);
  Serial.printf ("  Duration:  %d min %d sec\n", durMin, durSec);
  Serial.println("-------------------------------------------------");

  Serial.println("  [1] HEART RATE (MAX30102)");
  if (daily.hrSamples > 0) {
    Serial.printf ("  Average:   %.0f bpm\n", daily.avgHR);
    Serial.printf ("  Peak:      %.0f bpm\n", sessionPeakHR);
    Serial.printf ("  Low:       %.0f bpm\n", sessionLowHR > 900 ? 0 : sessionLowHR);
    Serial.printf ("  Samples:   %d readings\n", daily.hrSamples);
  } else {
    Serial.println("  No HR readings — finger not on sensor.");
  }

  Serial.println();
  Serial.println("  [2] OXYGEN SATURATION (MAX30102 SpO2)");
  if (data.spO2Valid) {
    Serial.printf ("  SpO2:      %d%%\n", data.spO2);
    if (data.spO2 < 95)
      Serial.println("  >> FLAG: SpO2 below 95% — check reading or seek advice.");
    else
      Serial.println("  >> Normal range (95-100%).");
  } else {
    Serial.println("  No valid SpO2 reading — hold finger still for ~4s.");
  }

  Serial.println();
  Serial.println("  [3] ACTIVITY");
  Serial.printf ("  Steps:     %d of %d goal (%d%%)\n",
                  data.steps, STEPS_GOAL,
                  (int)min((float)data.steps/STEPS_GOAL*100, 100.0f));
  Serial.printf ("  Active:    %d minutes\n", daily.activityMins);
  if (gpsData.distanceM > 0) {
    Serial.printf ("  Distance:  %.0f m (%.2f km)\n",
                    gpsData.distanceM, gpsData.distanceM/1000.0f);
    if (data.steps > 0)
      Serial.printf ("  Stride:    ~%.2f m per step\n",
                      gpsData.distanceM / data.steps);
  } else {
    Serial.println("  Distance:  No GPS fix");
  }

  Serial.println();
  Serial.println("  [4] ALTITUDE & FLOORS (BMP280)");
  if (bmpOK) {
    Serial.printf ("  Altitude:  %.1f m (relative to session start)\n", data.altitudeM);
    Serial.printf ("  Floors up: %d\n", data.floorsUp);
    Serial.printf ("  Pressure:  %.1f hPa\n", data.pressureHPa);
  } else {
    Serial.println("  BMP280 not available.");
  }

  Serial.println();
  Serial.println("  [5] STABILITY & FALL RISK");
  Serial.printf ("  Falls detected:  %d\n", daily.fallCount);
  Serial.printf ("  Balance score:   %d / 100\n", data.balanceScore);
  Serial.printf ("  Tilt angle:      %.1f degrees\n", data.tiltAngle);
  Serial.printf ("  MPU health:      %s\n",
                 mpuHealthDegraded ? "DEGRADED" : "OK");

  Serial.println();
  Serial.println("  [6] DEVICE");
  Serial.printf ("  Battery:   %.0f%%\n", data.battery);
  Serial.printf ("  Worn:      %s (MAX30102 IR)\n",
                 data.wearing ? "Yes" : "No");
  Serial.printf ("  Sensors:   MPU=%s  MAX=%s  BMP=%s\n",
                 mpuOK?"OK":"FAIL", maxOK?"OK":"FAIL", bmpOK?"OK":"FAIL");

  Serial.println();
  Serial.println("=================================================");
  Serial.println("  END OF REPORT");
  Serial.println("=================================================");
  Serial.println();

  delay(3000);
  state = STATE_CLOCK;
  needsFullDraw = true;
}

void goToSleep() {
  tft.fillScreen(0x0000);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0x2104);
  tft.setTextSize(1);
  tft.drawString("sleeping...", W/2, H/2 - 10);
  tft.drawString("press any button to wake", W/2, H/2 + 8);
  delay(1200);
  while (digitalRead(BUTTON1_PIN) == LOW) delay(10);
  delay(200);
  digitalWrite(TFT_BL, LOW);
  digitalWrite(LCD_PWR_PIN, LOW);
  esp_sleep_enable_ext0_wakeup(WAKE_PIN, 0);
  esp_deep_sleep_start();
}

void readButtons() {
  bool b1 = (digitalRead(BUTTON1_PIN) == LOW);
  bool b2 = (digitalRead(BUTTON2_PIN) == LOW);

  static bool bothLast = false;
  bool both = (b1 && b2);
  if (both && !bothLast) { bothLast = true; exportSession(); return; }
  bothLast = both;

  bool b1Falling = (!b1 && btn1Last);
  bool b2Falling = (!b2 && btn2Last);
  btn1Pressed = false;
  btn2Pressed = false;

  if (b1) {
    if (btn1HoldStart == 0) btn1HoldStart = millis();
    if (!btn1Held && millis() - btn1HoldStart >= SLEEP_HOLD_MS) {
      btn1Held = true; goToSleep();
    }
  } else {
    if (b1Falling && !btn1Held && btn1HoldStart > 0) btn1Pressed = true;
    btn1HoldStart = 0; btn1Held = false;
  }

  bool b2HoldScreen = (state == STATE_CLOCK || state == STATE_HEALTH);
  if (b2) {
    if (btn2HoldStart == 0) btn2HoldStart = millis();
    if (!btn2Held && b2HoldScreen && millis() - btn2HoldStart >= RESET_HOLD_MS) {
      btn2Held = true; resetSession();
    }
  } else {
    if (b2Falling && !btn2Held && btn2HoldStart > 0) btn2Pressed = true;
    btn2HoldStart = 0; btn2Held = false;
  }

  btn1Last = b1;
  btn2Last = b2;
}

void handleInput() {
  if (!btn1Pressed && !btn2Pressed) return;
  switch (state) {
    case STATE_CLOCK:
      if (btn1Pressed) { state = STATE_HEALTH; needsFullDraw = true; }
      if (btn2Pressed) { state = STATE_MENU; menuSel = 0; needsFullDraw = true; }
      break;
    case STATE_HEALTH:
      if (btn1Pressed) { state = STATE_CLOCK; needsFullDraw = true; }
      if (btn2Pressed) { state = STATE_MENU; menuSel = 0; needsFullDraw = true; }
      break;
    case STATE_MENU:
      if (btn1Pressed) { menuSel = (menuSel + 1) % MENU_COUNT; needsFullDraw = true; }
      if (btn2Pressed) {
        const AppState targets[] = {
          STATE_HEART, STATE_FITNESS, STATE_STABILITY, STATE_DEXTERITY,
          STATE_SUMMARY, STATE_DOCTOR, STATE_SETTINGS, STATE_SOS, STATE_CLOCK
        };
        if (menuSel == 7) daily.sosCount++;
        state = targets[menuSel];
        needsFullDraw = true;
      }
      break;
    case STATE_HEART:
    case STATE_FITNESS:
    case STATE_STABILITY:
    case STATE_SUMMARY:
    case STATE_DOCTOR:
    case STATE_SETTINGS:
    case STATE_DEXTERITY:
      if (btn1Pressed || btn2Pressed) { state = STATE_MENU; needsFullDraw = true; }
      break;
    case STATE_FALL_CONFIRM:
      if (btn2Pressed) { inFall = false; state = STATE_CLOCK; needsFullDraw = true; }
      break;
    case STATE_EMERGENCY:
    case STATE_SOS:
      if (btn2Pressed) { data.fallDetected = false; state = STATE_CLOCK; needsFullDraw = true; }
      break;
  }
}

// ============================================================
// DRAWING
// ============================================================
void drawScreenFull() {
  tft.fillScreen(C_BG);
  switch(state) {
    case STATE_CLOCK:        drawClockFull();    break;
    case STATE_HEALTH:       drawHealthFull();   break;
    case STATE_MENU:         drawMenu();         break;
    case STATE_HEART:        drawHeart();        break;
    case STATE_FITNESS:      drawFitness();      break;
    case STATE_STABILITY:    drawStability();    break;
    case STATE_DEXTERITY:    drawDexterity();    break;
    case STATE_SUMMARY:      drawSummary();      break;
    case STATE_DOCTOR:       drawDoctor();       break;
    case STATE_SETTINGS:     drawSettings();     break;
    case STATE_FALL_CONFIRM: drawFallConfirm();  break;
    case STATE_EMERGENCY:
    case STATE_SOS:          drawEmergency();    break;
  }
}

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

void drawSplash() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT);
  tft.setTextSize(4);
  tft.drawString("tiga", W/2, H/2 - 18);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString("health tracker for mom", W/2, H/2 + 18);
  tft.setTextColor(C_DIM);
  tft.drawString("v6a", W/2, H/2 + 34);
}

// ── CLOCK ────────────────────────────────────────────────────
void drawClockFull() {
  tft.fillCircle(12, 12, 5, healthDot());

  char batStr[8]; sprintf(batStr, "%.0f%%", data.battery);
  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(data.battery < 20 ? C_ORANGE : C_DIM);
  tft.drawString(batStr, W-8, 12);

  if (!data.wearing) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DIM);
    tft.setTextSize(1);
    tft.drawString("not worn", W/2, 12);
  }

  char timeStr[8];
  sprintf(timeStr, "%02d:%02d", displayHour, displayMin);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(5);
  tft.drawString(timeStr, W/2, 78);

  char dateStr[24];
  struct tm ti; getLocalTime(&ti);
  sprintf(dateStr, "%s, %d %s %d",
          dayNames[ti.tm_wday], displayDay, monthNames[displayMonth], displayYear);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString(dateStr, W/2, 120);

  tft.setTextColor(C_DIM);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("BTN1: health  hold: sleep", 8, H-8);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("BTN2 hold: reset  both: export", W-8, H-8);
}

void drawClockPartial() {
  if (displayMin == prev.minute) return;
  tft.fillRect(20, 52, W-40, 60, C_BG);
  char timeStr[8];
  sprintf(timeStr, "%02d:%02d", displayHour, displayMin);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(5);
  tft.drawString(timeStr, W/2, 78);
  if (data.healthScore != prev.healthScore) {
    tft.fillCircle(12, 12, 6, C_BG);
    tft.fillCircle(12, 12, 5, healthDot());
  }
}

// ── HEALTH DASHBOARD ─────────────────────────────────────────
void drawHealthFull() {
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
  int bx=cx+cw+gap+4, by=cy+ch-10, bw=cw-8, bh=5;
  tft.fillRect(bx, by, bw, bh, C_BG);
  tft.fillRect(bx, by, (int)(bw*prog), bh, stCol);

  // SpO2 card (replaces Stability on this dashboard)
  tft.fillRect(cx, cy+ch+gap, cw, ch, C_CARD);
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1); tft.setTextColor(C_MUTED);
  tft.drawString("SPO2", cx+6, cy+ch+gap+10);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
  if (data.spO2Valid) {
    char spo2Str[8]; sprintf(spo2Str, "%d%%", data.spO2);
    uint16_t sCol = data.spO2 >= 95 ? C_GREEN :
                    data.spO2 >= 90 ? C_ORANGE : C_RED;
    tft.setTextColor(sCol); tft.setTextSize(2);
    tft.drawString(spo2Str, cx+cw/2, cy+ch+gap+ch/2+6);
  } else {
    tft.setTextColor(C_MUTED);
    tft.drawString(data.wearing ? "reading..." : "no finger", cx+cw/2, cy+ch+gap+ch/2+6);
  }

  // Altitude card
  tft.fillRect(cx+cw+gap, cy+ch+gap, cw, ch, C_CARD);
  tft.setTextDatum(ML_DATUM); tft.setTextColor(C_MUTED);
  tft.drawString("ALT / FLOOR", cx+cw+gap+6, cy+ch+gap+10);
  tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
  if (bmpOK) {
    char altStr[20];
    sprintf(altStr, "%.0fm  F:%d", data.altitudeM, data.floorsUp);
    tft.setTextColor(C_ACCENT);
    tft.drawString(altStr, cx+cw+gap+cw/2, cy+ch+gap+ch/2+6);
  } else {
    tft.setTextColor(C_DIM);
    tft.drawString("no sensor", cx+cw+gap+cw/2, cy+ch+gap+ch/2+6);
  }
}

void drawHealthPartial() {
  int cx=4, cy=26, cw=(W-12)/2, ch=(H-cy-18)/2, gap=4;

  if ((int)data.heartRate != (int)prev.heartRate) {
    tft.fillRect(cx+1, cy+16, cw-2, ch-17, C_CARD);
    tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
    tft.setTextColor(hrStatusColor());
    tft.drawString(hrStatusLabel(), cx+cw/2, cy+ch/2+6);
  }
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
  if (data.spO2 != prev.spO2 || data.wearing != prev.wearing) {
    tft.fillRect(cx+1, cy+ch+gap+16, cw-2, ch-17, C_CARD);
    tft.setTextDatum(MC_DATUM);
    if (data.spO2Valid) {
      char spo2Str[8]; sprintf(spo2Str, "%d%%", data.spO2);
      uint16_t sCol = data.spO2 >= 95 ? C_GREEN :
                      data.spO2 >= 90 ? C_ORANGE : C_RED;
      tft.setTextColor(sCol); tft.setTextSize(2);
      tft.drawString(spo2Str, cx+cw/2, cy+ch+gap+ch/2+6);
    } else {
      tft.setTextColor(C_MUTED); tft.setTextSize(1);
      tft.drawString(data.wearing ? "reading..." : "no finger", cx+cw/2, cy+ch+gap+ch/2+6);
    }
  }
  if (data.altitudeM != prev.altitudeM) {
    tft.fillRect(cx+cw+gap+1, cy+ch+gap+16, cw-2, ch-17, C_CARD);
    tft.setTextDatum(MC_DATUM); tft.setTextSize(1);
    if (bmpOK) {
      char altStr[20];
      sprintf(altStr, "%.0fm  F:%d", data.altitudeM, data.floorsUp);
      tft.setTextColor(C_ACCENT);
      tft.drawString(altStr, cx+cw+gap+cw/2, cy+ch+gap+ch/2+6);
    }
  }
  if (data.healthScore != prev.healthScore || data.wearing != prev.wearing ||
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
    else tft.setTextColor(i==7 ? C_RED : C_TEXT);
    tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
    tft.drawString(menuItems[i], 18, y+itemH/2);
  }
  drawBottomHint("BTN1: scroll   BTN2: select");
}

// ── HEART DETAIL ─────────────────────────────────────────────
void drawHeart() {
  drawTopBar("HEART");
  tft.setTextDatum(MC_DATUM);

  tft.setTextSize(1);
  tft.setTextColor(hrStatusColor());
  tft.drawString(hrStatusLabel(), W/2, 38);

  if (data.heartRate > 0) {
    char bpmStr[16]; sprintf(bpmStr, "%.0f bpm", data.heartRate);
    tft.setTextColor(C_MUTED);
    tft.drawString(bpmStr, W/2, 54);
  }

  // Zone bar
  tft.fillRect(20, 68, W-40, 14, C_CARD);
  const char* zones[] = {"Rest","Light","Moderate","High"};
  uint16_t zoneColors[] = {C_ACCENT, C_GREEN, C_ORANGE, C_RED};
  int zw = (W-40)/4;
  for (int i=0; i<4; i++) {
    uint16_t col = (i==data.hrZone) ? zoneColors[i] : C_CARD2;
    tft.fillRect(20+i*zw, 68, zw-2, 14, col);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(i==data.hrZone ? C_BG : C_MUTED);
    tft.setTextSize(1);
    tft.drawString(zones[i], 20+i*zw+zw/2, 75);
  }

  tft.setTextColor(C_TEXT);
  tft.drawString(hrZoneLabel(), W/2, 94);

  // SpO2 line
  if (data.spO2Valid) {
    char spo2Str[20]; sprintf(spo2Str, "SpO2: %d%%", data.spO2);
    uint16_t sCol = data.spO2 >= 95 ? C_GREEN :
                    data.spO2 >= 90 ? C_ORANGE : C_RED;
    tft.setTextColor(sCol);
    tft.drawString(spo2Str, W/2, 110);
  } else {
    tft.setTextColor(C_DIM);
    tft.drawString(data.wearing ? "SpO2: reading..." : "SpO2: place finger", W/2, 110);
  }

  char rng[32]; sprintf(rng, "safe: %d - %d bpm", HR_SAFE_MIN, HR_SAFE_MAX);
  tft.setTextColor(C_DIM);
  tft.drawString(rng, W/2, 126);

  tft.setTextColor(C_MUTED);
  if (data.hrZone >= 3)
    tft.drawString("Slow down and breathe deeply.", W/2, 142);
  else if (data.hrZone == 0 && data.heartRate > 0)
    tft.drawString("Good resting heart rate.", W/2, 142);
  else
    tft.drawString("Keep going — you're doing well.", W/2, 142);

  drawBottomHint("any button: back");
}

// ── FITNESS ──────────────────────────────────────────────────
void drawFitness() {
  drawTopBar("FITNESS");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT); tft.setTextSize(1);
  tft.drawString(stepsLabel(), W/2, 38);

  char stepsStr[20]; sprintf(stepsStr, "%d of %d steps", data.steps, STEPS_GOAL);
  tft.setTextColor(C_MUTED);
  tft.drawString(stepsStr, W/2, 54);

  float prog = min((float)data.steps/STEPS_GOAL, 1.0f);
  uint16_t stCol = prog>=1.0f ? C_GREEN : prog>=0.5f ? C_ACCENT : C_TEXT;
  tft.fillRect(20, 66, W-40, 12, C_CARD);
  tft.fillRect(20, 66, (int)((W-40)*prog), 12, stCol);

  char pctStr[16]; sprintf(pctStr, "%.0f%%", prog*100);
  tft.setTextColor(stCol);
  tft.drawString(pctStr, W/2, 92);

  // Altitude row
  if (bmpOK) {
    char altStr[32];
    sprintf(altStr, "Altitude: %.0fm  Floors: %d", data.altitudeM, data.floorsUp);
    tft.setTextColor(C_ACCENT);
    tft.drawString(altStr, W/2, 108);
  }

  tft.setTextColor(C_MUTED);
  char actStr[32]; sprintf(actStr, "Active time: %d min", daily.activityMins);
  tft.drawString(actStr, W/2, bmpOK ? 124 : 108);

  tft.setTextColor(fabsf(data.tiltAngle) < 20 ? C_GREEN : C_ORANGE);
  tft.drawString(postureLabel(), W/2, bmpOK ? 140 : 124);

  if (gpsData.hasFix) {
    char distStr[32];
    if (gpsData.distanceM < 1000)
      sprintf(distStr, "%.0f m  %.1f km/h", gpsData.distanceM, gpsData.speedKmh);
    else
      sprintf(distStr, "%.2f km  %.1f km/h", gpsData.distanceM/1000.0f, gpsData.speedKmh);
    tft.setTextColor(C_ACCENT);
    tft.drawString(distStr, W/2, 156);
  } else {
    tft.setTextColor(C_DIM);
    char satStr[32]; sprintf(satStr, "GPS searching (%d sats)", gpsData.satellites);
    tft.drawString(satStr, W/2, 156);
  }

  drawBottomHint("any button: back");
}

// ── STABILITY ────────────────────────────────────────────────
void drawStability() {
  drawTopBar("STABILITY & SAFETY");
  tft.setTextDatum(MC_DATUM);

  uint16_t stabCol = data.isStable ? C_GREEN : C_ORANGE;
  tft.setTextSize(2); tft.setTextColor(stabCol);
  tft.drawString(data.isStable ? "Moving well" : "Watch your step", W/2, 50);

  tft.setTextSize(1); tft.setTextColor(C_MUTED);
  char balStr[24]; sprintf(balStr, "Balance score: %d / 100", data.balanceScore);
  tft.drawString(balStr, W/2, 72);

  uint16_t balCol = data.balanceScore>70 ? C_GREEN :
                    data.balanceScore>40 ? C_ORANGE : C_RED;
  tft.fillRect(40, 82, W-80, 10, C_CARD);
  tft.fillRect(40, 82, (int)((W-80)*(data.balanceScore/100.0f)), 10, balCol);

  tft.setTextColor(fabsf(data.tiltAngle)<20 ? C_GREEN : C_ORANGE);
  tft.drawString(postureLabel(), W/2, 104);

  tft.setTextColor(C_DIM);
  char motionStr[32]; sprintf(motionStr, "Fall detection: active  %.2fG", data.accelG);
  tft.drawString(motionStr, W/2, 120);

  char fallStr[24]; sprintf(fallStr, "Falls today: %d", daily.fallCount);
  tft.setTextColor(daily.fallCount>0 ? C_ORANGE : C_MUTED);
  tft.drawString(fallStr, W/2, 136);

  tft.setTextColor(C_DIM);
  tft.drawString(daily.fallCount>0 ? "Please take extra care today."
                                    : "Stay safe. Take your time on stairs.", W/2, 152);
  drawBottomHint("any button: back");
}

// ── DEXTERITY ────────────────────────────────────────────────
void drawDexterity() {
  drawTopBar("DEXTERITY TEST");
  tft.setTextDatum(MC_DATUM); tft.setTextSize(1); tft.setTextColor(C_MUTED);
  tft.drawString("Dexterity tests coming in v6b", W/2, 60);
  tft.drawString("with button-tap reaction test.", W/2, 78);
  drawBottomHint("any button: back");
}

// ── SUMMARY ──────────────────────────────────────────────────
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
  if (daily.hrSamples>0) {
    sprintf(s, "%.0f bpm avg", daily.avgHR); row("Heart avg:", s, C_TEXT);
  } else { row("Heart avg:", "no reading", C_MUTED); }
  if (data.spO2Valid) {
    sprintf(s, "%d%%", data.spO2);
    row("SpO2:", s, data.spO2>=95 ? C_GREEN : C_ORANGE);
  } else { row("SpO2:", "no reading", C_MUTED); }
  if (bmpOK) {
    sprintf(s, "%.0fm / %d floors", data.altitudeM, data.floorsUp);
    row("Altitude:", s, C_ACCENT);
  }
  sprintf(s, "%d min", daily.activityMins); row("Active:", s, C_TEXT);
  sprintf(s, "%d detected", daily.fallCount);
  row("Falls:", s, daily.fallCount>0 ? C_ORANGE : C_GREEN);
  tft.fillRect(0, y+4, W, 2, C_CARD);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(C_MUTED);
  if (data.steps>=STEPS_GOAL && daily.fallCount==0)
    tft.drawString("Great day! Your doctor would be pleased.", W/2, y+16);
  else if (data.steps<1000)
    tft.drawString("Try to get some steps in today.", W/2, y+16);
  else
    tft.drawString("Good effort. Rest well tonight.", W/2, y+16);
  drawBottomHint("any button: back");
}

// ── DOCTOR'S REPORT ──────────────────────────────────────────
void drawDoctor() {
  drawTopBar("DOCTOR'S REPORT");
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.drawString("Medical data — share with doctor", 10, 30);
  int y=46;
  auto drow = [&](const char* label, const char* val){
    tft.setTextColor(C_DIM);    tft.drawString(label, 10,  y);
    tft.setTextColor(C_ACCENT); tft.drawString(val,   170, y);
    y+=15;
  };
  char s[32];
  if (data.heartRate>0) sprintf(s,"%.0f bpm",data.heartRate); else strcpy(s,"no reading");
  drow("Heart Rate:", s);
  if (daily.hrSamples>0) sprintf(s,"%.0f bpm (n=%d)",daily.avgHR,daily.hrSamples);
  else strcpy(s,"no data"); drow("HR Average:", s);
  if (data.spO2Valid) sprintf(s,"%d%%",data.spO2); else strcpy(s,"no reading");
  drow("SpO2:", s);
  sprintf(s,"%d steps",data.steps);   drow("Steps:", s);
  sprintf(s,"%.2fG",data.accelG);     drow("Accel:", s);
  sprintf(s,"%.1f deg",data.tiltAngle); drow("Tilt:", s);
  sprintf(s,"%d/100",data.balanceScore); drow("Balance:", s);
  sprintf(s,"%d today",daily.fallCount); drow("Falls:", s);
  if (bmpOK) { sprintf(s,"%.0fm / %dF",data.altitudeM,data.floorsUp); drow("Alt/Floors:", s); }
  sprintf(s,"%.1fC (chip)",data.tempC); drow("Temp:", s);
  sprintf(s,"%.0f%%",data.battery);   drow("Battery:", s);
  drawBottomHint("any button: back");
}

// ── SETTINGS ─────────────────────────────────────────────────
void drawSettings() {
  drawTopBar("SETTINGS");
  tft.setTextDatum(ML_DATUM); tft.setTextSize(1);
  int y=38;
  auto srow = [&](const char* label, const char* val, uint16_t col){
    tft.setTextColor(C_TEXT);  tft.drawString(label, 16, y);
    tft.setTextColor(col);     tft.drawString(val, 130, y);
    y+=20;
  };
  char s[32];
  srow("Step goal:", "3000 steps", C_MUTED);
  sprintf(s,"%d - %d bpm",HR_SAFE_MIN,HR_SAFE_MAX); srow("HR safe range:", s, C_MUTED);
  srow("WiFi:", strlen(WIFI_SSID)>0 ? WIFI_SSID : "not set",
       strlen(WIFI_SSID)>0 ? C_GREEN : C_MUTED);
  sprintf(s,"MPU=%s MAX=%s BMP=%s",
          mpuOK?"OK":"--", maxOK?"OK":"--", bmpOK?"OK":"--");
  srow("Sensors:", s, mpuOK&&maxOK&&bmpOK ? C_GREEN : C_ORANGE);
  if (gpsData.hasFix) {
    sprintf(s,"Fix! %d sats",gpsData.satellites); srow("GPS:", s, C_GREEN);
  } else {
    sprintf(s,"Searching (%d sats)",gpsData.satellites); srow("GPS:", s, C_MUTED);
  }
  tft.setTextColor(C_DIM); tft.setTextDatum(MC_DATUM);
  tft.drawString("Full settings in companion app", W/2, H-20);
  drawBottomHint("any button: back");
}

// ── FALL CONFIRM ─────────────────────────────────────────────
void drawFallConfirm() {
  tft.fillScreen(0x8200);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT); tft.setTextSize(2);
  tft.drawString("Are you OK?", W/2, 35);
  tft.setTextSize(1); tft.setTextColor(C_YELLOW);
  tft.drawString("Press BTN2 if you're fine.", W/2, 60);
  tft.setTextSize(5); tft.setTextColor(C_TEXT);
  char cs[4]; sprintf(cs,"%d",fallCountdown);
  tft.drawString(cs, W/2, 100);
  tft.setTextSize(1); tft.setTextColor(C_YELLOW);
  tft.drawString("Alert sending if no response...", W/2, 140);
  tft.drawString("BTN2 = I'm fine", W/2, 155);
}

// ── EMERGENCY / SOS ──────────────────────────────────────────
void drawEmergency() {
  uint16_t bg = pulseOn ? C_RED : 0x9000;
  tft.fillScreen(bg);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(C_TEXT);

  if (state == STATE_SOS) {
    tft.setTextSize(3); tft.drawString("SOS", W/2, 40);
    tft.setTextSize(1); tft.setTextColor(C_YELLOW);
    tft.drawString("Help is being notified.", W/2, 75);
    tft.drawString("Stay calm. Help is coming.", W/2, 92);
  } else {
    if (data.heartRate > HR_WARN_HIGH) {
      tft.setTextSize(2); tft.drawString("HEART RATE", W/2, 38);
      tft.drawString("TOO HIGH", W/2, 62);
      char s[16]; sprintf(s,"%.0f bpm",data.heartRate);
      tft.setTextSize(1); tft.setTextColor(C_YELLOW);
      tft.drawString(s, W/2, 85);
      tft.drawString("Please sit down and rest.", W/2, 100);
    } else if (data.heartRate>0 && data.heartRate<HR_WARN_LOW) {
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

// ── Battery read (every 30s) ─────────────────────────────────
void readBattery() {
  static unsigned long lastBat = 0;
  if (millis() - lastBat > 30000) {
    lastBat = millis();
    int r = analogRead(BAT_ADC_PIN);
    float v = (r / 4095.0f) * 3.3f * 2.0f;
    data.battery = constrain((v - 3.2f) / (4.2f - 3.2f) * 100.0f, 0, 100);
  }
}
