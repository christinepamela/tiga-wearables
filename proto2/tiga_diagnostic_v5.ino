/*
 * TIGA I2C Scanner — diagnostic sketch
 * Upload this to confirm MPU6050 is detected on the I2C bus.
 * Expected: device found at 0x68 (or 0x69 if AD0 is tied high).
 *
 * Board: LilyGO T-Display-S3
 * SDA = GPIO18, SCL = GPIO17
 *
 * After uploading, open Serial Monitor at 115200 baud.
 */

#include <Wire.h>

#define SDA_PIN 18
#define SCL_PIN 17

void setup() {
  Serial.begin(115200);
  delay(2000);  // give Serial Monitor time to connect

  Serial.println();
  Serial.println("=================================");
  Serial.println("TIGA I2C Scanner");
  Serial.println("=================================");
  Serial.printf("SDA = GPIO%d, SCL = GPIO%d\n", SDA_PIN, SCL_PIN);
  Serial.println("Starting I2C bus...");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);  // 100kHz — slow and reliable for diagnosis

  delay(500);
  Serial.println("Scanning 0x01 to 0x7F...");
  Serial.println();
}

void loop() {
  byte error, address;
  int deviceCount = 0;

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("  >> Device FOUND at 0x%02X", address);
      if (address == 0x68) Serial.print("  (MPU6050 default address)");
      if (address == 0x69) Serial.print("  (MPU6050 alt address — AD0 high)");
      Serial.println();
      deviceCount++;
    } else if (error == 4) {
      Serial.printf("  ?? Unknown error at 0x%02X\n", address);
    }
  }

  Serial.println();
  if (deviceCount == 0) {
    Serial.println("  !! NO I2C DEVICES FOUND !!");
    Serial.println("  Check:");
    Serial.println("   1. MPU6050 VCC connected to 3.3V (not 5V)");
    Serial.println("   2. MPU6050 GND connected to board GND");
    Serial.println("   3. SDA wire on GPIO18 and MPU SDA pin");
    Serial.println("   4. SCL wire on GPIO17 and MPU SCL pin");
    Serial.println("   5. Wires fully seated (breadboard can be loose)");
  } else {
    Serial.printf("  Total: %d device(s) found.\n", deviceCount);
  }

  Serial.println();
  Serial.println("Rescanning in 5 seconds...");
  Serial.println("---------------------------------");
  delay(5000);
}
