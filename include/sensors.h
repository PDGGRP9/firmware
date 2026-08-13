#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

#ifndef TEST_INTEGRATION
#include "DFRobot_BloodOxygen_S.h"
#include "I2Cdev.h"
#include "MPU6050.h"
#endif // TEST_INTEGRATION

class SensorManager {
private:
#ifndef TEST_INTEGRATION
    DFRobot_BloodOxygen_S_I2C* pMAX30102;
    MPU6050* pMPU6050;
    int16_t lastAx, lastAy, lastAz;
    float accelMagnitude;
    bool isMoving;
    const float STEP_THRESHOLD = 15.0f;
#endif // TEST_INTEGRATION

    uint8_t  lastHeartRate;
    uint8_t  lastSpO2;
    uint32_t stepCounter;

public:
    // Constructeur et destructeur
    SensorManager();
    ~SensorManager();

    // Initialisation des capteurs
    bool initI2C(uint8_t sda, uint8_t scl);
    bool initMAX30102();
    bool initMPU6050();

    // Préparer les capteurs pour la mise en veille
    void prepareSleep();

    // Mise à jour des lectures des capteurs
    void updateReadings();

    // Getters
    uint8_t getHeartRate() const { return lastHeartRate; }
    uint8_t getSpO2() const { return lastSpO2; }
    uint32_t getSteps() const { return stepCounter; }

#ifndef TEST_INTEGRATION
    float getAccelMagnitude() const { return accelMagnitude; }
    void detectSteps();
#endif // TEST_INTEGRATION

    void resetSteps() { stepCounter = 0; }
};

#endif // SENSORS_H