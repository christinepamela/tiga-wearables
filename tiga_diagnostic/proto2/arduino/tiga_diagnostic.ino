// ============================================================
// TIGA — Sensor Diagnostic Sketch
// Tests each sensor independently, prints results to Serial
// Upload this BEFORE the full tiga.ino
//
// Wiring (confirmed):
//   MPU6050  SDA → GPIO18 | SCL → GPIO17 | VCC → 3V | GND → GND
//   Pulse    S   → GPIO01 | +   → 3V     | -   → GND
//   SW-420   DO  → GPIO03 | VCC → 3V     | GND → GND
//   TTP223   I/O → GPIO13 | VCC → 3V     | GND → GND
//   Button1  → GPIO21 → GND (PULLUP)
//   Button2  → GPIO16 → GND (PULLUP)
// ============================================================

#include <Wire.h>
#include <MPU6050.h>     // Install: "MPU6050" by Electronic Cats

// ── PIN DEFINITIONS ──────────────────────────────────────────
#define PULSE_PIN     1   // GPIO01 — ADC1_CH0 — analog pulse sensor
#define SW420_PIN     3   // GPIO03 — digital vibration sensor
#define TTP223_PIN   13   // GPIO13 — digital touch sensor
#define BUTTON1_PIN  21   // GPIO21 — scroll button (INPUT_PULLUP)
#define BUTTON2_PIN  16   // GPIO16 — enter button  (INPUT_PULLUP)
#define I2C_SDA      18   // GPIO18 — MPU6050 SDA (LilyGO confirmed)
#define I2C_SCL      17   // GPIO17 — MPU6050 SCL (LilyGO confirmed)
#define BAT_ADC_PIN   4   // GPIO04 — battery voltage (internal)
#define LCD_PWR_PIN  15   // GPIO15 — must HIGH for battery power

// ── MPU6050 ──────────────────────────────────────────────────
MPU6050 mpu;

// ── SETUP ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1500);  // Give Serial monitor time to connect

  // Battery power: keep display alive
  pinMode(LCD_PWR_PIN, OUTPUT);
  digitalWrite(LCD_PWR_PIN, HIGH);

  Serial.println("\n\n========================================");
  Serial.println("  TIGA — Sensor Diagnostic v1.0");
  Serial.println("========================================");
  Serial.println("Each sensor will be tested in sequence.");
  Serial.println("Watch Serial Monitor at 115200 baud.\n");

  // GPIO setup
  pinMode(SW420_PIN,   INPUT);
  pinMode(TTP223_PIN,  INPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  // I2C — must specify pins for T-Display-S3
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(100);

  // ── TEST 1: MPU6050 ────────────────────────────────────────
  Serial.println("─────────────────────────────────────");
  Serial.println("TEST 1: MPU6050 (Accelerometer/Gyro)");
  Serial.println("─────────────────────────────────────");

  mpu.initialize();

  if (mpu.testConnection()) {
    Serial.println("✓ MPU6050 connected successfully (I2C addr 0x68)");
  } else {
    Serial.println("✗ MPU6050 NOT found. Check:");
    Serial.println("  - SDA wire → GPIO18");
    Serial.println("  - SCL wire → GPIO17");
    Serial.println("  - VCC → 3V, GND → GND");
  }

  // ── TEST 2: I2C Bus Scan ───────────────────────────────────
  Serial.println("\n─────────────────────────────────────");
  Serial.println("TEST 2: I2C Bus Scan");
  Serial.println("─────────────────────────────────────");
  Serial.println("Scanning for I2C devices...");

  int deviceCount = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("  Found device at 0x%02X", addr);
      if (addr == 0x68) Serial.print("  ← MPU6050");
      if (addr == 0x69) Serial.print("  ← MPU6050 (alt addr)");
      Serial.println();
      deviceCount++;
    }
  }
  if (deviceCount == 0) {
    Serial.println("  No I2C devices found — check wiring");
  } else {
    Serial.printf("  %d device(s) found\n", deviceCount);
  }

  // ── TEST 3: Pulse Sensor ───────────────────────────────────
  Serial.println("\n─────────────────────────────────────");
  Serial.println("TEST 3: Pulse Sensor (Analog)");
  Serial.println("─────────────────────────────────────");
  Serial.println("Reading GPIO01 (ADC1_CH0) for 3 seconds...");
  Serial.println("Place finger on sensor now.");

  unsigned long start = millis();
  int minVal = 4095, maxVal = 0, sumVal = 0, count = 0;

  while (millis() - start < 3000) {
    int raw = analogRead(PULSE_PIN);
    if (raw < minVal) minVal = raw;
    if (raw > maxVal) maxVal = raw;
    sumVal += raw;
    count++;
    delay(10);
  }

  int avgVal = (count > 0) ? sumVal / count : 0;
  int range  = maxVal - minVal;

  Serial.printf("  Min: %d | Max: %d | Avg: %d | Range: %d\n",
                minVal, maxVal, avgVal, range);

  if (maxVal < 100) {
    Serial.println("  ✗ Flat signal — check S wire → GPIO01, VCC → 3V");
  } else if (range < 50) {
    Serial.println("  ~ Weak signal — try pressing finger more firmly");
  } else {
    Serial.println("  ✓ Signal detected — looks alive!");
  }

  // ── TEST 4: SW-420 Vibration ───────────────────────────────
  Serial.println("\n─────────────────────────────────────");
  Serial.println("TEST 4: SW-420 Vibration Sensor");
  Serial.println("─────────────────────────────────────");
  Serial.println("Tap or shake the sensor for 3 seconds...");

  start = millis();
  int vibCount = 0;
  bool lastState = digitalRead(SW420_PIN);

  while (millis() - start < 3000) {
    bool current = digitalRead(SW420_PIN);
    if (current != lastState) {
      vibCount++;
      lastState = current;
      Serial.printf("  Vibration event! State: %s\n", current ? "HIGH" : "LOW");
    }
    delay(5);
  }

  if (vibCount == 0) {
    Serial.println("  ~ No vibration detected — try tapping harder");
    Serial.println("    Or check DO wire → GPIO03, VCC → 3V");
  } else {
    Serial.printf("  ✓ %d transitions detected — sensor working!\n", vibCount);
  }

  // ── TEST 5: TTP223 Touch ──────────────────────────────────
  Serial.println("\n─────────────────────────────────────");
  Serial.println("TEST 5: TTP223 Capacitive Touch");
  Serial.println("─────────────────────────────────────");
  Serial.println("Touch the sensor pad for 3 seconds...");

  start = millis();
  int touchCount = 0;
  bool touching = false;

  while (millis() - start < 3000) {
    bool current = digitalRead(TTP223_PIN);
    if (current && !touching) {
      touchCount++;
      touching = true;
      Serial.println("  Touch detected!");
    } else if (!current) {
      touching = false;
    }
    delay(20);
  }

  if (touchCount == 0) {
    Serial.println("  ~ No touch detected — try touching pad firmly");
    Serial.println("    Check I/O wire → GPIO13, VCC → 3V");
  } else {
    Serial.printf("  ✓ %d touch event(s) — sensor working!\n", touchCount);
  }

  // ── TEST 6: Buttons ───────────────────────────────────────
  Serial.println("\n─────────────────────────────────────");
  Serial.println("TEST 6: Buttons");
  Serial.println("─────────────────────────────────────");
  Serial.println("Press Button 1 (Scroll, GPIO21)...");

  start = millis();
  bool btn1Found = false;
  while (millis() - start < 3000) {
    if (digitalRead(BUTTON1_PIN) == LOW) {
      Serial.println("  ✓ Button 1 pressed!");
      btn1Found = true;
      delay(300);
      break;
    }
    delay(20);
  }
  if (!btn1Found) Serial.println("  ~ Button 1 not pressed (or check GPIO21 → GND)");

  Serial.println("Press Button 2 (Enter, GPIO16)...");
  start = millis();
  bool btn2Found = false;
  while (millis() - start < 3000) {
    if (digitalRead(BUTTON2_PIN) == LOW) {
      Serial.println("  ✓ Button 2 pressed!");
      btn2Found = true;
      delay(300);
      break;
    }
    delay(20);
  }
  if (!btn2Found) Serial.println("  ~ Button 2 not pressed (or check GPIO16 → GND)");

  // ── TEST 7: Battery ADC ───────────────────────────────────
  Serial.println("\n─────────────────────────────────────");
  Serial.println("TEST 7: Battery Voltage (ADC)");
  Serial.println("─────────────────────────────────────");

  int batRaw = analogRead(BAT_ADC_PIN);
  // T-Display-S3: voltage divider halves battery voltage before ADC
  // ADC range 0–4095 = 0–3.3V, battery is divided by 2
  float voltage = (batRaw / 4095.0f) * 3.3f * 2.0f;
  int pct = (int)((voltage - 3.2f) / (4.2f - 3.2f) * 100.0f);
  pct = max(0, min(100, pct));

  Serial.printf("  Raw ADC: %d | Voltage: %.2fV | ~%d%%\n",
                batRaw, voltage, pct);

  if (batRaw < 100) {
    Serial.println("  ~ Very low reading — running on USB (no battery)?");
  } else {
    Serial.println("  ✓ Battery ADC reading OK");
  }

  // ── SUMMARY ───────────────────────────────────────────────
  Serial.println("\n========================================");
  Serial.println("  Diagnostic complete.");
  Serial.println("  Loop below prints live sensor data.");
  Serial.println("  Open Serial Plotter to see pulse wave.");
  Serial.println("========================================\n");
}

// ── LOOP — live data ─────────────────────────────────────────
void loop() {
  static unsigned long lastPrint = 0;

  if (millis() - lastPrint >= 200) {
    lastPrint = millis();

    // MPU6050
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    float accelMag = sqrtf((float)ax*ax + (float)ay*ay + (float)az*az) / 16384.0f;

    // Pulse sensor raw (for Serial Plotter)
    int pulse = analogRead(PULSE_PIN);

    // Digital sensors
    bool vib   = digitalRead(SW420_PIN);
    bool touch = digitalRead(TTP223_PIN);
    bool btn1  = !digitalRead(BUTTON1_PIN);
    bool btn2  = !digitalRead(BUTTON2_PIN);

    // Print tab-separated for Serial Plotter compatibility
    // Format: Pulse  AccelG  Vib  Touch  Btn1  Btn2
    Serial.printf("Pulse:%d\tAccelG:%.2f\tVib:%d\tTouch:%d\tBtn1:%d\tBtn2:%d\n",
                  pulse, accelMag, vib, touch, btn1, btn2);
  }
}
