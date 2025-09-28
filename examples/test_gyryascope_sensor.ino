#include <Wire.h>

void setup() {
  Wire.begin(18, 17); // SDA, SCL
  Serial.begin(115200);
  delay(1000);
  Serial.println("Scanning I2C devices...");
}

void loop() {
  byte error, address;
  int count = 0;

  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      count++;
      delay(10);
    }
  }

  if (count == 0) Serial.println("No I2C devices found\n");
  else Serial.println("I2C scan complete\n");

  delay(2000);
}
