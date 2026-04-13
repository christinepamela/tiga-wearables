// ============================================================
// TIGA — Sensor Diagnostic v4
// Runs all tests ONCE, prints a clean summary, then STOPS.
// No scrolling. Read the report at the end.
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

// Results stored here
bool  r_i2c    = false;
bool  r_mpu    = false;
float r_mpuG   = 0;
int   r_pulseMin, r_pulseMax, r_pulseRange;
int   r_vibs   = 0;
int   r_taps   = 0;
bool  r_btn1   = false;
bool  r_btn2   = false;
float r_batV   = 0;
int   r_batPct = 0;

void countdown(int seconds) {
  for (int i = seconds; i > 0; i--) {
    Serial.printf("\r  %d... ", i);
    delay(1000);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  pinMode(LCD_PWR_PIN, OUTPUT);
  digitalWrite(LCD_PWR_PIN, HIGH);
  pinMode(SW420_PIN,   INPUT);
  pinMode(TTP223_PIN,  INPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(300);

  Serial.println("\n\n========================================");
  Serial.println("  TIGA Diagnostic v4 — starting in 3s");
  Serial.println("  Tests run once. Summary printed at end.");
  Serial.println("========================================");
  delay(3000);

  // ── 1. I2C scan ───────────────────────────────────────────
  Serial.println("\n[1/7] I2C scan...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      r_i2c = true;
      Serial.printf("      Found device at 0x%02X\n", addr);
    }
  }

  // ── 2. MPU6050 ────────────────────────────────────────────
  Serial.println("[2/7] MPU6050...");
  mpu.initialize();
  delay(200);
  r_mpu = mpu.testConnection();
  if (r_mpu) {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    r_mpuG = sqrtf((float)ax*ax+(float)ay*ay+(float)az*az)/16384.0f;
  }

  // ── 3. Pulse sensor ───────────────────────────────────────
  Serial.println("[3/7] Pulse sensor — place finger on sensor");
  countdown(3);
  int mn=4095, mx=0, sm=0, ct=0;
  unsigned long t = millis();
  while (millis()-t < 5000) {
    int v = analogRead(PULSE_PIN);
    if (v<mn) mn=v; if(v>mx) mx=v;
    sm+=v; ct++;
    delay(10);
  }
  r_pulseMin   = mn;
  r_pulseMax   = mx;
  r_pulseRange = mx - mn;
  Serial.println("      Done.");

  // ── 4. SW-420 vibration ───────────────────────────────────
  Serial.println("[4/7] Vibration — TAP the breadboard now");
  countdown(3);
  bool last = digitalRead(SW420_PIN);
  t = millis();
  while (millis()-t < 5000) {
    bool cur = digitalRead(SW420_PIN);
    if (cur != last) { r_vibs++; last = cur; }
    delay(5);
  }
  Serial.println("      Done.");

  // ── 5. TTP223 touch ───────────────────────────────────────
  Serial.println("[5/7] Touch — TAP the red TTP223 pad now");
  countdown(3);
  bool touching = false;
  t = millis();
  while (millis()-t < 5000) {
    bool cur = digitalRead(TTP223_PIN);
    if (cur && !touching) { r_taps++; touching = true; }
    if (!cur) touching = false;
    delay(20);
  }
  Serial.println("      Done.");

  // ── 6. Buttons ────────────────────────────────────────────
  Serial.println("[6/7] Buttons — press BUTTON 1 (GPIO21)");
  countdown(3);
  t = millis();
  while (millis()-t < 5000) {
    if (digitalRead(BUTTON1_PIN) == LOW) { r_btn1 = true; break; }
    delay(20);
  }
  Serial.println("      Now press BUTTON 2 (GPIO16)");
  countdown(3);
  t = millis();
  while (millis()-t < 5000) {
    if (digitalRead(BUTTON2_PIN) == LOW) { r_btn2 = true; break; }
    delay(20);
  }
  Serial.println("      Done.");

  // ── 7. Battery ────────────────────────────────────────────
  Serial.println("[7/7] Battery ADC...");
  int raw = analogRead(BAT_ADC_PIN);
  r_batV   = (raw / 4095.0f) * 3.3f * 2.0f;
  r_batPct = constrain((int)((r_batV - 3.2f) / (4.2f - 3.2f) * 100.0f), 0, 100);

  // ── FINAL REPORT ──────────────────────────────────────────
  delay(500);
  Serial.println();
  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║     TIGA DIAGNOSTIC — RESULTS        ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf ("║ I2C Bus      : %-22s║\n", r_i2c ? "FOUND device(s)" : "NOTHING FOUND");
  Serial.printf ("║ MPU6050      : %-22s║\n", r_mpu ? "OK" : "NOT RESPONDING");
  if (r_mpu)
    Serial.printf("║   Accel Mag  : %-5.2f G                 ║\n", r_mpuG);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf ("║ Pulse Min    : %-22d║\n", r_pulseMin);
  Serial.printf ("║ Pulse Max    : %-22d║\n", r_pulseMax);
  Serial.printf ("║ Pulse Range  : %-5d %-17s║\n", r_pulseRange,
                  r_pulseRange > 50 ? "(OK)" : r_pulseMax < 100 ? "(CHECK WIRING)" : "(WEAK - PRESS HARDER)");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf ("║ SW-420 Vibs  : %-5d %-17s║\n", r_vibs,   r_vibs > 0  ? "(OK)" : "(NONE - TAP HARDER)");
  Serial.printf ("║ TTP223 Taps  : %-5d %-17s║\n", r_taps,   r_taps > 0  ? "(OK)" : "(NONE - CHECK WIRING)");
  Serial.printf ("║ Button 1     : %-22s║\n", r_btn1 ? "OK" : "NOT DETECTED");
  Serial.printf ("║ Button 2     : %-22s║\n", r_btn2 ? "OK" : "NOT DETECTED");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf ("║ Battery      : %-5.2f V  ~%-3d%%           ║\n", r_batV, r_batPct);
  Serial.println("╚══════════════════════════════════════╝");
  Serial.println();
  Serial.println("  Tests complete. Screenshot this report.");
  Serial.println("  Loop is STOPPED — no more scrolling.");
  Serial.println();
}

// ── loop does nothing — report is the final output ───────────
void loop() {
  // Intentionally empty. Open Serial Plotter separately if needed.
  delay(10000);
}
