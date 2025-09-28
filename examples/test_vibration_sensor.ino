const int vibPin = 43; // GPIO43
void setup() {
  Serial.begin(115200);
  pinMode(vibPin, INPUT);
}

void loop() {
  int val = digitalRead(vibPin);
  Serial.println(val);  // HIGH when triggered
  delay(200);
}
