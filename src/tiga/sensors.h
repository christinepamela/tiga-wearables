#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "MPU6050.h"

// ----------- SENSOR PINS -----------
#define PULSE_PIN 3        // GPIO03 (Analog)
#define SW420_PIN 43       // GPIO43 (Digital)
#define TTP223_PIN 44      // GPIO44 (Digital)
#define BUTTON1_PIN 21     // GPIO21
#define BUTTON2_PIN 16     // GPIO16

// ----------- OBJECTS -----------
MPU6050 mpu;

// ----------- VARIABLES -----------
unsigned long lastStepTime = 0;
int stepCount = 0;
bool lastVibrationState = false;

// ----------- INITIALIZATION -----------
void initSensors() {
  Serial.println("Initializing sensors...");

  Wire.begin(18, 17);  // SDA, SCL for MPU6050
  mpu.initialize();

  pinMode(PULSE_PIN, INPUT);
  pinMode(SW420_PIN, INPUT);
  pinMode(TTP223_PIN, INPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  if (mpu.testConnection()) {
    Serial.println("MPU6050 connected.");
  } else {
    Serial.println("MPU6050 connection failed!");
  }

  Serial.println("All sensors initialized.");
}

// ----------- HEART RATE (Analog Sensor) -----------
float readHeartRate() {
  int sensorValue = analogRead(PULSE_PIN);
  // Simple placeholder logic (replace with pulse averaging later)
  float bpm = map(sensorValue, 0, 4095, 60, 120);
  return bpm;
}

// ----------- STABILITY / STEPS VIA MPU6050 -----------
int readSteps() {
  static int steps = 0;
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float accel = sqrt(ax * ax + ay * ay + az * az);

  // crude step detection
  if (accel > 15000 && millis() - lastStepTime > 300) {
    steps++;
    lastStepTime = millis();
  }
  return steps;
}

bool detectFall() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float accel = sqrt(ax * ax + ay * ay + az * az);
  return (accel > 25000); // sudden spike = possible fall
}

// ----------- TOUCH / GRIP (TTP223) -----------
bool readGripTouch() {
  return digitalRead(TTP223_PIN) == HIGH;
}

// ----------- VIBRATION SENSOR (SW-420) -----------
bool readVibration() {
  bool state = digitalRead(SW420_PIN);
  bool detected = false;

  // detect transitions (from stable to vibrating)
  if (state != lastVibrationState && state == HIGH) {
    detected = true;
  }
  lastVibrationState = state;
  return detected;
}

// ----------- BUTTONS -----------
bool readButton1() {
  return digitalRead(BUTTON1_PIN) == LOW; // pressed
}

bool readButton2() {
  return digitalRead(BUTTON2_PIN) == LOW; // pressed
}

// ----------- TEMPERATURE (Simulated, from MPU6050 or placeholder) -----------
float readTemperature() {
  // If MPU temperature is needed:
  int16_t tempRaw = mpu.getTemperature();
  float tempC = (tempRaw / 340.0) + 36.53;
  return tempC;
}

#endif
