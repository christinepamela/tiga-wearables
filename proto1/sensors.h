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

// ----------- MPU6050 Constants -----------
#define ACCEL_THRESHOLD 15000    // For step detection
#define FALL_THRESHOLD 25000     // For fall detection
#define STABLE_THRESHOLD 8000    // For stability detection
#define MPU_SAMPLE_RATE 100      // Hz

// ----------- PULSE Constants -----------
#define PULSE_SAMPLES 10
#define PULSE_TIMEOUT 30000      // 30 seconds timeout
#define MIN_BPM 40
#define MAX_BPM 180

// ----------- OBJECTS -----------
extern MPU6050 mpu;

// ----------- Function Declarations -----------
void initSensors();
float readHeartRate();
int readSteps();
bool detectFall();
bool readGripTouch();
bool readVibration();
bool readButton1();
bool readButton2();
float readTemperature();
void resetStepCounter();
bool isSensorStable();
float getActivityLevel();

#endif
