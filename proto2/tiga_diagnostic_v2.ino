// ============================================================
// TIGA — Sensor Diagnostic v2
// Fixed: explicit Wire.begin before MPU init
// Board: LilyGo T-Display-S3
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

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LCD_PWR_PIN, OUTPUT);
  digitalWrite(LCD_PWR_PIN, HIGH);

  pinMode(SW420_PIN,   INPUT);
  pinMode(TTP223_PIN,  INPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  // CRITICAL: Wire.begin MUST come before mpu.initialize()
  // and must specify the T-Display-S3 I2C pins explicitly
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(200);

  Serial.println("\n========================================");
  Serial.println("  TIGA Diagnostic v2");
  Serial.println("========================================\n");

  // --- I2C Scan first ---
  Serial.println("[1] I2C Bus Scan (SDA=18, SCL=17)");
  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("    Device at 0x%02X", addr);
      if (addr == 0x68) Serial.print(" <- MPU6050 (AD0 LOW)");
      if (addr == 0x69) Serial.print(" <- MPU6050 (AD0 HIGH)");
      Serial.println();
      found++;
    }
  }
  if (found == 0) {
    Serial.println("    NONE found. Check SDA->GPIO18, SCL->GPIO17, VCC->3V");
  }

  // --- MPU6050 ---
  Serial.println("\n[2] MPU6050");
  mpu.initialize();
  if (mpu.testConnection()) {
    Serial.println("    Connected OK");
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    Serial.printf("    Accel raw: X=%d Y=%d Z=%d\n", ax, ay, az);
  } else {
    Serial.println("    FAILED - not responding");
  }

  // --- Pulse sensor ---
  Serial.println("\n[3] Pulse Sensor (GPIO01) - reading 2s, place finger");
  delay(500);
  int mn=4095, mx=0, sm=0, ct=0;
  unsigned long t = millis();
  while (millis()-t < 2000) {
    int v = analogRead(PULSE_PIN);
    if (v<mn) mn=v; if(v>mx) mx=v; sm+=v; ct++;
    delay(10);
  }
  Serial.printf("    Min:%d Max:%d Avg:%d Range:%d\n", mn, mx, sm/ct, mx-mn);
  Serial.println(mx<100 ? "    CHECK wiring" : (mx-mn<50 ? "    Weak - press harder" : "    OK"));

  // --- SW-420 ---
  Serial.println("\n[4] SW-420 Vibration (GPIO03) - tap it for 2s");
  delay(300);
  int vibs=0; bool last=digitalRead(SW420_PIN);
  t=millis();
  while(millis()-t<2000){
    bool cur=digitalRead(SW420_PIN);
    if(cur!=last){vibs++;last=cur;}
    delay(5);
  }
  Serial.printf("    Transitions: %d %s\n", vibs, vibs>0?"OK":"(tap harder or check DO->GPIO03)");

  // --- TTP223 ---
  Serial.println("\n[5] TTP223 Touch (GPIO13) - touch it for 2s");
  delay(300);
  int taps=0; bool touching=false;
  t=millis();
  while(millis()-t<2000){
    bool cur=digitalRead(TTP223_PIN);
    if(cur&&!touching){taps++;touching=true;}
    if(!cur) touching=false;
    delay(20);
  }
  Serial.printf("    Taps: %d %s\n", taps, taps>0?"OK":"(touch pad or check I/O->GPIO13)");

  // --- Buttons ---
  Serial.println("\n[6] Press Button 1 (GPIO21) now...");
  t=millis(); bool b1=false;
  while(millis()-t<3000){if(digitalRead(BUTTON1_PIN)==LOW){b1=true;break;}delay(20);}
  Serial.println(b1?"    Button 1 OK":"    Button 1 not detected");

  Serial.println("    Press Button 2 (GPIO16) now...");
  t=millis(); bool b2=false;
  while(millis()-t<3000){if(digitalRead(BUTTON2_PIN)==LOW){b2=true;break;}delay(20);}
  Serial.println(b2?"    Button 2 OK":"    Button 2 not detected");

  // --- Battery ---
  Serial.println("\n[7] Battery ADC (GPIO04)");
  int raw=analogRead(BAT_ADC_PIN);
  float v=(raw/4095.0f)*3.3f*2.0f;
  Serial.printf("    Raw:%d Voltage:%.2fV\n", raw, v);

  Serial.println("\n========================================");
  Serial.println("  Setup done. Loop printing live data.");
  Serial.println("  Open Serial Plotter to see pulse wave");
  Serial.println("========================================\n");
}

void loop() {
  static unsigned long last=0;
  if(millis()-last<200) return;
  last=millis();

  int16_t ax,ay,az;
  if(mpu.testConnection()){
    mpu.getAcceleration(&ax,&ay,&az);
  } else { ax=ay=az=0; }
  float g=sqrtf((float)ax*ax+(float)ay*ay+(float)az*az)/16384.0f;

  Serial.printf("Pulse:%d\tAccelG:%.2f\tVib:%d\tTouch:%d\tBtn1:%d\tBtn2:%d\n",
    analogRead(PULSE_PIN), g,
    digitalRead(SW420_PIN), digitalRead(TTP223_PIN),
    !digitalRead(BUTTON1_PIN), !digitalRead(BUTTON2_PIN));
}
