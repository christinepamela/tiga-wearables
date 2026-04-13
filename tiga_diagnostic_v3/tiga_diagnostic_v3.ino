// ============================================================
// TIGA — Sensor Diagnostic v3
// Slowed down: 5s per sensor, clear prompts, MPU I2C retry
// Board: LilyGo T-Display-S3 | 115200 baud
// ============================================================

#include <Wire.h>
#include <MPU6050.h>  // Electronic Cats

#define PULSE_PIN     1
#define SW420_PIN     3
#define TTP223_PIN   13
#define BUTTON1_PIN  21
#define BUTTON2_PIN  16
#define I2C_SDA      18
#define I2C_SCL      17
#define BAT_ADC_PIN   4
#define LCD_PWR_PIN  15

MPU6050 mpu;
bool mpuOK = false;

// ── helpers ──────────────────────────────────────────────────
void banner(const char* title) {
  Serial.println();
  Serial.println("══════════════════════════════════════");
  Serial.println(title);
  Serial.println("══════════════════════════════════════");
}

void waitMsg(const char* msg, int seconds) {
  Serial.println(msg);
  for (int i = seconds; i > 0; i--) {
    Serial.printf("  %d...\n", i);
    delay(1000);
  }
}

// ── setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(3000);  // Give Serial Monitor time to open after upload

  pinMode(LCD_PWR_PIN, OUTPUT);
  digitalWrite(LCD_PWR_PIN, HIGH);
  pinMode(SW420_PIN,   INPUT);
  pinMode(TTP223_PIN,  INPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  Serial.println("\n\n");
  Serial.println("██████████████████████████████████████");
  Serial.println("  TIGA — Sensor Diagnostic v3");
  Serial.println("  5 seconds per sensor. Read carefully.");
  Serial.println("██████████████████████████████████████");
  delay(2000);

  // ── TEST 1: I2C scan ───────────────────────────────────────
  banner("TEST 1/7 — I2C Bus Scan");
  Serial.println("Initialising I2C on SDA=GPIO18, SCL=GPIO17...");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(500);

  int found = 0;
  Serial.println("Scanning addresses 0x01 to 0x7F...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  ✓ Device found at 0x%02X", addr);
      if (addr == 0x68) Serial.print("  ← MPU6050 (AD0=LOW)");
      if (addr == 0x69) Serial.print("  ← MPU6050 (AD0=HIGH)");
      Serial.println();
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  ✗ NO devices found on I2C bus.");
    Serial.println("  Check: SDA wire → GPIO18 pin on board");
    Serial.println("  Check: SCL wire → GPIO17 pin on board");
    Serial.println("  Check: MPU6050 VCC → 3V (NOT 5V pin)");
    Serial.println("  Check: MPU6050 GND → GND");
  } else {
    Serial.printf("  Found %d device(s)\n", found);
  }
  delay(3000);

  // ── TEST 2: MPU6050 ────────────────────────────────────────
  banner("TEST 2/7 — MPU6050 Accelerometer");
  mpu.initialize();
  delay(200);

  mpuOK = mpu.testConnection();
  if (mpuOK) {
    Serial.println("  ✓ MPU6050 connected!");
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    Serial.printf("  Accel raw — X:%6d  Y:%6d  Z:%6d\n", ax, ay, az);
    Serial.println("  Now TILT the breadboard and watch values change:");
    for (int i = 0; i < 5; i++) {
      mpu.getAcceleration(&ax, &ay, &az);
      float g = sqrtf((float)ax*ax+(float)ay*ay+(float)az*az)/16384.0f;
      Serial.printf("  X:%6d  Y:%6d  Z:%6d  |  Magnitude: %.2f G\n", ax, ay, az, g);
      delay(1000);
    }
  } else {
    Serial.println("  ✗ MPU6050 NOT responding.");
    Serial.println("  Most likely cause: I2C wires above also failed.");
    Serial.println("  Double-check physical wire connections.");
    Serial.println("  Try pressing MPU6050 module firmly into breadboard.");
  }
  delay(2000);

  // ── TEST 3: Pulse sensor ───────────────────────────────────
  banner("TEST 3/7 — Pulse Sensor (GPIO01)");
  waitMsg("Place your FINGER on the pulse sensor now.", 3);

  int mn=4095, mx=0, sm=0, ct=0;
  Serial.println("  Reading for 5 seconds...");
  unsigned long t = millis();
  while (millis()-t < 5000) {
    int v = analogRead(PULSE_PIN);
    if (v<mn) mn=v;
    if (v>mx) mx=v;
    sm+=v; ct++;
    // Print every 500ms so you can watch it
    static unsigned long lastPrint = 0;
    if (millis()-lastPrint > 500) {
      Serial.printf("  Raw: %4d\n", v);
      lastPrint = millis();
    }
    delay(10);
  }
  int range = mx - mn;
  Serial.printf("  Min:%d  Max:%d  Avg:%d  Range:%d\n", mn, mx, sm/ct, range);
  if      (mx < 100)   Serial.println("  ✗ Flat — check S→GPIO01, VCC→3V");
  else if (range < 50) Serial.println("  ~ Weak signal — press finger more firmly");
  else                 Serial.println("  ✓ Signal detected!");
  delay(2000);

  // ── TEST 4: SW-420 vibration ──────────────────────────────
  banner("TEST 4/7 — SW-420 Vibration (GPIO03)");
  waitMsg("TAP or SHAKE the breadboard now.", 2);

  int vibs = 0;
  bool last = digitalRead(SW420_PIN);
  Serial.printf("  Starting state: %s\n", last ? "HIGH" : "LOW");
  Serial.println("  Reading for 5 seconds...");
  t = millis();
  while (millis()-t < 5000) {
    bool cur = digitalRead(SW420_PIN);
    if (cur != last) {
      vibs++;
      last = cur;
      Serial.printf("  → State change! Now %s  (total: %d)\n",
                    cur ? "HIGH" : "LOW", vibs);
    }
    delay(5);
  }
  if (vibs == 0) Serial.println("  ~ None detected — try tapping harder, or check DO→GPIO03");
  else           Serial.printf("  ✓ %d transitions detected\n", vibs);
  delay(2000);

  // ── TEST 5: TTP223 touch ──────────────────────────────────
  banner("TEST 5/7 — TTP223 Touch (GPIO13)");
  waitMsg("TOUCH the red pad on TTP223 now.", 2);

  int taps = 0;
  bool touching = false;
  Serial.println("  Reading for 5 seconds...");
  t = millis();
  while (millis()-t < 5000) {
    bool cur = digitalRead(TTP223_PIN);
    if (cur && !touching) {
      taps++;
      touching = true;
      Serial.printf("  → Touch! (total: %d)\n", taps);
    }
    if (!cur) touching = false;
    delay(20);
  }
  if (taps == 0) Serial.println("  ~ None — touch the metal pad, or check I/O→GPIO13");
  else           Serial.printf("  ✓ %d touch event(s)\n", taps);
  delay(2000);

  // ── TEST 6: Buttons ───────────────────────────────────────
  banner("TEST 6/7 — Buttons");

  Serial.println("  Press BUTTON 1 (Scroll, GPIO21) — 5 seconds...");
  bool b1 = false;
  t = millis();
  while (millis()-t < 5000) {
    if (digitalRead(BUTTON1_PIN) == LOW) {
      Serial.println("  ✓ Button 1 pressed!");
      b1 = true;
      delay(500);
      break;
    }
    delay(20);
  }
  if (!b1) Serial.println("  ~ Not pressed / check wire GPIO21→GND via button");

  Serial.println("  Press BUTTON 2 (Enter, GPIO16) — 5 seconds...");
  bool b2 = false;
  t = millis();
  while (millis()-t < 5000) {
    if (digitalRead(BUTTON2_PIN) == LOW) {
      Serial.println("  ✓ Button 2 pressed!");
      b2 = true;
      delay(500);
      break;
    }
    delay(20);
  }
  if (!b2) Serial.println("  ~ Not pressed / check wire GPIO16→GND via button");
  delay(2000);

  // ── TEST 7: Battery ───────────────────────────────────────
  banner("TEST 7/7 — Battery Voltage");
  int raw = analogRead(BAT_ADC_PIN);
  float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
  int pct = constrain((int)((voltage - 3.2f) / (4.2f - 3.2f) * 100.0f), 0, 100);
  Serial.printf("  Raw ADC: %d\n", raw);
  Serial.printf("  Voltage: %.2f V\n", voltage);
  Serial.printf("  Approx:  %d%%\n", pct);
  if (raw < 200) Serial.println("  ~ Low reading — running on USB without LiPo? That's OK.");
  else           Serial.println("  ✓ Battery reading OK");
  delay(3000);

  // ── Summary ───────────────────────────────────────────────
  Serial.println();
  Serial.println("██████████████████████████████████████");
  Serial.println("  SETUP TESTS COMPLETE");
  Serial.println("  Loop now prints 1 reading per second.");
  Serial.println("  Open Serial Plotter to see pulse wave.");
  Serial.println("██████████████████████████████████████");
  Serial.println();
}

// ── loop — 1 reading per second ──────────────────────────────
void loop() {
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();

  int pulse = analogRead(PULSE_PIN);
  bool vib   = digitalRead(SW420_PIN);
  bool touch = digitalRead(TTP223_PIN);
  bool btn1  = !digitalRead(BUTTON1_PIN);
  bool btn2  = !digitalRead(BUTTON2_PIN);

  float g = 0.0f;
  if (mpuOK) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    g = sqrtf((float)ax*ax+(float)ay*ay+(float)az*az)/16384.0f;
  }

  Serial.printf("Pulse:%4d  AccelG:%.2f  Vib:%d  Touch:%d  Btn1:%d  Btn2:%d\n",
                pulse, g, vib, touch, btn1, btn2);
}
