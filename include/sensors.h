#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "DFRobot_BloodOxygen_S.h"
#include "I2Cdev.h"
#include "MPU6050.h"

class SensorManager {
private:
    DFRobot_BloodOxygen_S_I2C* pMAX30102;
    MPU6050* pMPU6050;
    
    // Données des capteurs
    uint8_t  lastHeartRate;
    uint8_t  lastSpO2;
    uint32_t stepCounter;
    
    // Variables pour détection de pas
    int16_t lastAx, lastAy, lastAz;
    float accelMagnitude;
    bool isMoving;
    const float STEP_THRESHOLD = 15.0f;  // Seuil d'accélération
    
    // Initialisation interne
    bool initI2C(uint8_t sda, uint8_t scl);
    bool initMAX30102();
    bool initMPU6050();
    
public:
    SensorManager();
    ~SensorManager();
    
    // Initialisation générale
    bool initialize(uint8_t sda, uint8_t scl);
    
    // Lecture des capteurs
    void updateReadings();
    
    // Getters
    uint8_t getHeartRate() const { return lastHeartRate; }
    uint8_t getSpO2() const { return lastSpO2; }
    uint32_t getSteps() const { return stepCounter; }
    float getAccelMagnitude() const { return accelMagnitude; }
    
    // Détection de pas
    void detectSteps();
    void resetSteps() { stepCounter = 0; }
};

#endif // SENSORS_H