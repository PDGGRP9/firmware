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
    float accelResolution;
    float accelMagnitude;
    bool isMoving;

    // --- Variables pour l'algo de détection de pas ---
    float filteredMagnitude = 1.0f;
    float gravityEstimate = 1.0f;
    unsigned long lastStepTime = 0;

    // === Détection par pics multiples + timeout ===
    int peakCountInWindow = 0;      // Nombre de pics dans la fenêtre temporelle
    unsigned long windowStartTime = 0;  // Début de la fenêtre d'observation

    // --- Constantes de calibration (À AJUSTER) ---
    static constexpr float STEP_THRESHOLD_HIGH_G = 0.25f;
    static constexpr float STEP_THRESHOLD_LOW_G  = 0.10f;
    static constexpr unsigned long MIN_STEP_INTERVAL_MS = 300;
    static constexpr float LOWPASS_ALPHA = 0.4f; 
    static constexpr float GRAVITY_ALPHA = 0.025f; 

    // === Nouveau: Fenêtre glissante d'observation ===
    static constexpr unsigned long PEAK_WINDOW_MS = 700;
    static constexpr int MIN_PEAKS_FOR_STEP = 2;
    static constexpr int MIN_PEAKS_FOR_RAPID = 3;

    // === Anti-rebond (debouncing) ===
    static constexpr unsigned long DEBOUNCE_WINDOW_MS = 100; 

        // === Nouvelles variables pour la détection améliorée ===
    static constexpr unsigned long MIN_CYCLE_DURATION_MS = 200;   // Durée minimale d'un cycle de pas
    static constexpr unsigned long MAX_CYCLE_DURATION_MS = 2000;  // Durée maximale d'un cycle de pas
    static constexpr float ADAPTIVE_THRESHOLD_FACTOR = 0.7f;      // Facteur pour le seuil adaptatif
    
    // Variables pour le suivi des cycles
    float avgPeakAmplitude;
    bool stepInProgress;
    float currentPeakValue;
    unsigned long currentCycleStart;
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

    // Refresh the cached readings
    void updateReadings();

    // Getters
    uint8_t getHeartRate() const { return lastHeartRate; }
    uint8_t getSpO2() const { return lastSpO2; }
    uint32_t getSteps() const { return stepCounter; }

#ifdef HAS_IMU
    float getAccelMagnitude() const { return accelMagnitude; }
    void detectSteps();
#endif // HAS_IMU

    void resetSteps() { stepCounter = 0; }
};

#endif // SENSORS_H
