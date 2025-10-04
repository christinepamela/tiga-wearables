// sensors.h
#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "PulseSensor.h"
#include "MPU6050.h"
#include "TTP223.h"
#include "DHT.h"  // if temperature is from DHT sensor, example

// Initialize your sensors here
void initSensors() {
  // e.g. mpu.begin(), pulseSensor.begin(), etc.
}

// Example functions
float readHeartRate() {
  // replace with actual sensor logic
  return random(70, 100);  
}

int readSteps() {
  return random(1000, 3000);
}

float readTemperature() {
  return 25.0 + random(-3, 3) * 0.1;
}

bool detectFall() {
  return false;
}

int readGripStrength() {
  return random(50, 100);
}

#endif
