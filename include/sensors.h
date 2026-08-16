#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// Les libs capteur ne sont incluses que si le capteur est déclaré câblé
// (flags HAS_* de platformio.ini) : sans ça, un env sans le lib_deps
// correspondant ne compile pas.
#ifdef HAS_OXYGEN
#include "DFRobot_BloodOxygen_S.h"
#endif // HAS_OXYGEN

#ifdef HAS_IMU
#include "I2Cdev.h"
#include "MPU6050.h"
#endif // HAS_IMU

class SensorManager {
private:
    // Toujours présents : ce sont eux qu'on envoie en BLE, qu'ils viennent
    // d'un vrai capteur ou de la simulation.
    uint8_t  lastHeartRate;
    uint8_t  lastSpO2;
    uint32_t stepCounter;

    // Les membres ci-dessous sont déclarés APRÈS les trois du haut pour que la
    // liste d'initialisation du constructeur reste dans l'ordre de déclaration
    // quelle que soit la combinaison de flags (sinon warning -Wreorder).
#ifdef HAS_OXYGEN
    DFRobot_BloodOxygen_S_I2C* pMAX30102;
#endif // HAS_OXYGEN

#ifdef HAS_IMU
    MPU6050* pMPU6050;
    int16_t lastAx, lastAy, lastAz;
    float accelMagnitude;
    bool isMoving;
    const float STEP_THRESHOLD = 15.0f;
#endif // HAS_IMU

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

#ifdef HAS_IMU
    float getAccelMagnitude() const { return accelMagnitude; }
    void detectSteps();
#endif // HAS_IMU

    void resetSteps() { stepCounter = 0; }
};

#endif // SENSORS_H