#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// Sensor libs are included only when the sensor is declared as wired
// (HAS_* flags in platformio.ini): otherwise an env without the matching
// lib_deps would not compile.
#ifdef HAS_OXYGEN
#include "DFRobot_BloodOxygen_S.h"
#endif // HAS_OXYGEN

#ifdef HAS_IMU
#include "I2Cdev.h"
#include "MPU6050.h"
#include "logic/step_detector.h"
#endif // HAS_IMU

// Reads the I2C sensors (MAX30102 for HR/SpO2, MPU6050 for steps) and keeps
// the last values. When a sensor is not wired, its values are simulated.
class SensorManager {
private:
    // Always present: these are what we send over BLE, whether they come from
    // a real sensor or from the simulation.
    uint8_t  lastHeartRate;
    uint8_t  lastSpO2;
    uint32_t stepCounter;

    // Declared AFTER the three above so the constructor init list stays in
    // declaration order whatever the flag combination is (else -Wreorder).
#ifdef HAS_OXYGEN
    DFRobot_BloodOxygen_S_I2C* pMAX30102;
#endif // HAS_OXYGEN

#ifdef HAS_IMU
    MPU6050* pMPU6050;
    int16_t lastAx, lastAy, lastAz;
    float accelMagnitude;          // |accel| in g of the last sample (~1.0 at rest)

    // Step counting is delegated to a pure, unit-tested detector fed at ~50 Hz
    // by sampleMotion() (see step_detector.h). No detection state lives here.
    StepDetector stepDetector_;
    uint32_t lastAccelSampleMs_ = 0;
#endif // HAS_IMU

public:
    SensorManager();
    ~SensorManager();

    // Sensor init
    bool initI2C(uint8_t sda, uint8_t scl);
    bool initMAX30102();
    bool initMPU6050();

    // Put the sensors in a safe state before deep sleep
    void prepareSleep();

    // Refresh HR/SpO2 (and, without an IMU, the simulated step count).
    // Call once per READ_INTERVAL.
    void updateReadings();

    // Poll the accelerometer and run the step detector. Call on every loop():
    // it rate-limits itself to ACCEL_SAMPLE_INTERVAL_MS internally.
    void sampleMotion();

    // Getters
    uint8_t getHeartRate() const { return lastHeartRate; }
    uint8_t getSpO2() const { return lastSpO2; }
    uint32_t getSteps() const { return stepCounter; }

#ifdef HAS_IMU
    float getAccelMagnitude() const { return accelMagnitude; }
#endif // HAS_IMU

    void resetSteps() { stepCounter = 0; }
};

#endif // SENSORS_H
