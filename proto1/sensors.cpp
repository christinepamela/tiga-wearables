#include "sensors.h"

// ----------- VARIABLES -----------
MPU6050 mpu;
static unsigned long lastStepTime = 0;
static int stepCount = 0;
static bool lastVibrationState = false;
static float pulseValues[PULSE_SAMPLES];
static int pulseIndex = 0;

// Circular buffer for MPU readings
#define MPU_BUFFER_SIZE 50
static int16_t accelX[MPU_BUFFER_SIZE];
static int16_t accelY[MPU_BUFFER_SIZE];
static int16_t accelZ[MPU_BUFFER_SIZE];
static int mpuBufferIndex = 0;

void initSensors() {
    Serial.println("Initializing sensors...");

    // Initialize I2C for MPU6050
    Wire.begin(18, 17);  // SDA=GPIO18, SCL=GPIO17
    Wire.setClock(400000); // Fast mode
    
    // Initialize MPU6050
    mpu.initialize();
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8);
    mpu.setDLPFMode(MPU6050_DLPF_BW_20);
    mpu.setRate(MPU_SAMPLE_RATE - 1);
    
    // Set up pins
    pinMode(PULSE_PIN, INPUT);
    pinMode(SW420_PIN, INPUT);
    pinMode(TTP223_PIN, INPUT);
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);

    // Test MPU6050 connection
    if (mpu.testConnection()) {
        Serial.println("MPU6050 connected successfully");
        // Self-test MPU
        int16_t ax, ay, az;
        mpu.getAcceleration(&ax, &ay, &az);
        if (ax == 0 && ay == 0 && az == 0) {
            Serial.println("WARNING: MPU readings are zero!");
        }
    } else {
        Serial.println("ERROR: MPU6050 connection failed!");
    }

    // Initialize pulse sensor array
    for (int i = 0; i < PULSE_SAMPLES; i++) {
        pulseValues[i] = 0;
    }

    Serial.println("All sensors initialized");
}

float readHeartRate() {
    static unsigned long lastBeat = 0;
    static int beatCount = 0;
    static float lastValidBPM = 75.0;

    int rawValue = analogRead(PULSE_PIN);
    unsigned long now = millis();

    // Store in circular buffer
    pulseValues[pulseIndex] = rawValue;
    pulseIndex = (pulseIndex + 1) % PULSE_SAMPLES;

    // Calculate average and threshold
    float avg = 0;
    for (int i = 0; i < PULSE_SAMPLES; i++) {
        avg += pulseValues[i];
    }
    avg /= PULSE_SAMPLES;
    
    float threshold = avg + 100; // Adjustable threshold

    // Detect beats
    if (rawValue > threshold) {
        if (now - lastBeat > 300) { // Minimum 300ms between beats
            beatCount++;
            unsigned long beatTime = now - lastBeat;
            float instantBPM = 60000.0 / beatTime;
            
            // Validate BPM
            if (instantBPM >= MIN_BPM && instantBPM <= MAX_BPM) {
                lastValidBPM = instantBPM;
            }
            lastBeat = now;
        }
    }

    // Reset if no beats for too long
    if (now - lastBeat > PULSE_TIMEOUT) {
        beatCount = 0;
        lastBeat = now;
    }

    return lastValidBPM;
}

int readSteps() {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    
    // Store in circular buffer
    accelX[mpuBufferIndex] = ax;
    accelY[mpuBufferIndex] = ay;
    accelZ[mpuBufferIndex] = az;
    mpuBufferIndex = (mpuBufferIndex + 1) % MPU_BUFFER_SIZE;

    // Calculate magnitude
    float accel = sqrt(ax*ax + ay*ay + az*az);
    
    // Step detection with debounce and threshold
    if (accel > ACCEL_THRESHOLD && millis() - lastStepTime > 300) {
        stepCount++;
        lastStepTime = millis();
    }
    
    return stepCount;
}

bool detectFall() {
    static unsigned long lastFallCheck = 0;
    static bool inFallState = false;
    const unsigned long FALL_CHECK_INTERVAL = 50; // Check every 50ms
    
    unsigned long now = millis();
    if (now - lastFallCheck < FALL_CHECK_INTERVAL) {
        return inFallState;
    }
    lastFallCheck = now;

    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float accel = sqrt(ax*ax + ay*ay + az*az);

    // Fall detection algorithm
    if (accel > FALL_THRESHOLD) {
        // Check for impact followed by stillness
        delay(100); // Wait briefly
        mpu.getAcceleration(&ax, &ay, &az);
        float postFallAccel = sqrt(ax*ax + ay*ay + az*az);
        
        if (postFallAccel < STABLE_THRESHOLD) {
            inFallState = true;
            return true;
        }
    }
    
    inFallState = false;
    return false;
}

bool readGripTouch() {
    static unsigned long lastTouch = 0;
    static bool touchState = false;
    
    bool currentState = digitalRead(TTP223_PIN) == HIGH;
    unsigned long now = millis();

    // Debounce
    if (currentState != touchState && now - lastTouch > 50) {
        touchState = currentState;
        lastTouch = now;
    }
    
    return touchState;
}

bool readVibration() {
    static unsigned long lastVibCheck = 0;
    const unsigned long VIB_CHECK_INTERVAL = 100; // Check every 100ms
    
    unsigned long now = millis();
    if (now - lastVibCheck < VIB_CHECK_INTERVAL) {
        return false;
    }
    lastVibCheck = now;

    bool currentState = digitalRead(SW420_PIN);
    bool detected = false;

    // Detect transitions with improved filtering
    if (currentState != lastVibrationState) {
        // Confirm reading
        delay(5);
        if (digitalRead(SW420_PIN) == currentState) {
            if (currentState == HIGH) {
                detected = true;
            }
            lastVibrationState = currentState;
        }
    }

    return detected;
}

bool readButton1() {
    static unsigned long lastDebounce1 = 0;
    static bool buttonState1 = false;
    const unsigned long DEBOUNCE_DELAY = 50;
    
    bool reading = digitalRead(BUTTON1_PIN) == LOW;
    unsigned long now = millis();
    
    if (reading != buttonState1 && now - lastDebounce1 > DEBOUNCE_DELAY) {
        buttonState1 = reading;
        lastDebounce1 = now;
    }
    
    return buttonState1;
}

bool readButton2() {
    static unsigned long lastDebounce2 = 0;
    static bool buttonState2 = false;
    const unsigned long DEBOUNCE_DELAY = 50;
    
    bool reading = digitalRead(BUTTON2_PIN) == LOW;
    unsigned long now = millis();
    
    if (reading != buttonState2 && now - lastDebounce2 > DEBOUNCE_DELAY) {
        buttonState2 = reading;
        lastDebounce2 = now;
    }
    
    return buttonState2;
}

float readTemperature() {
    static float lastValidTemp = 36.5;
    static unsigned long lastTempRead = 0;
    const unsigned long TEMP_READ_INTERVAL = 1000; // Read every second
    
    unsigned long now = millis();
    if (now - lastTempRead < TEMP_READ_INTERVAL) {
        return lastValidTemp;
    }
    lastTempRead = now;

    // Read from MPU6050 internal temp sensor
    int16_t tempRaw = mpu.getTemperature();
    float tempC = (tempRaw / 340.0) + 36.53;
    
    // Validate reading
    if (tempC >= 35.0 && tempC <= 42.0) {
        lastValidTemp = tempC;
    }
    
    return lastValidTemp;
}

void resetStepCounter() {
    stepCount = 0;
}

bool isSensorStable() {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float accel = sqrt(ax*ax + ay*ay + az*az);
    return accel < STABLE_THRESHOLD;
}

float getActivityLevel() {
    float sum = 0;
    for (int i = 0; i < MPU_BUFFER_SIZE; i++) {
        float accel = sqrt(accelX[i]*accelX[i] + 
                         accelY[i]*accelY[i] + 
                         accelZ[i]*accelZ[i]);
        sum += accel;
    }
    return sum / MPU_BUFFER_SIZE;
}
