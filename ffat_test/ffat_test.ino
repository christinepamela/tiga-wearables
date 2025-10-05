#include "FFat.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!FFat.begin(true)) { // true = format if failed
    Serial.println("FFat mount failed!");
    return;
  }
  Serial.println("FFat mounted successfully.");

  File f = FFat.open("/test.json", "r");
  if (!f) {
    Serial.println("Failed to open /test.json");
    return;
  }

  Serial.println("Contents of /test.json:");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
}

void loop() {}
