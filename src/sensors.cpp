#include "sensors.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

// ==================== CONSTRUCTEUR / DESTRUCTEUR ====================
SensorManager::SensorManager()
#ifndef TEST_INTEGRATION
    : pMAX30102(nullptr), pMPU6050(nullptr),
      lastHeartRate(0), lastSpO2(0), stepCounter(0),
      lastAx(0), lastAy(0), lastAz(0), accelMagnitude(0.0f),
      isMoving(false) {
#else
    : lastHeartRate(0), lastSpO2(0), stepCounter(0) {
#endif
}

SensorManager::~SensorManager() {
#ifndef TEST_INTEGRATION
    if (pMAX30102) delete pMAX30102;
    if (pMPU6050) delete pMPU6050;
#endif
}

// ==================== INITIALISATION I2C ====================
bool SensorManager::initI2C(uint8_t sda, uint8_t scl) {
#ifdef TEST_INTEGRATION
    Serial.println("[SensorManager] Mode TEST_INTEGRATION -> I2C ignoré");
    return true;
#else
    Serial.print("[SensorManager] Initialisation I2C... ");
    bool success = Wire.begin(sda, scl);
    delay(100);
    if (success) {
        Serial.println("[OK]");
        return true;
    } else {
        Serial.println("[FAIL]");
        return false;
    }
#endif
}

// ==================== INITIALISATION MAX30102 ====================
bool SensorManager::initMAX30102() {
#ifdef TEST_INTEGRATION
    Serial.println("[SensorManager] Mode TEST_INTEGRATION -> MAX30102 ignoré");
    return true;
#else
    Serial.print("[SensorManager] Initialisation MAX30102 (Oxymètre)... ");
    pMAX30102 = new DFRobot_BloodOxygen_S_I2C(&Wire, I2C_ADDRESS);
    if (!pMAX30102->begin()) {
        Serial.println("[FAIL]");
        delete pMAX30102;
        pMAX30102 = nullptr;
        return false;
    }
    pMAX30102->sensorStartCollect();
    Serial.println("[OK]");
    return true;
#endif
}

// ==================== INITIALISATION MPU6050 ====================
bool SensorManager::initMPU6050() {
#ifdef TEST_INTEGRATION
    Serial.println("[SensorManager] Mode TEST_INTEGRATION -> MPU6050 ignoré");
    return true;
#else
    Serial.print("[SensorManager] Initialisation MPU6050 (Accéléromètre)... ");
    pMPU6050 = new MPU6050(MPU6050_ADDRESS);
    pMPU6050->initialize();
    if (!pMPU6050->testConnection()) {
        Serial.println("[FAIL]");
        delete pMPU6050;
        pMPU6050 = nullptr;
        return false;
    }
    Serial.println("[OK]");
    return true;
#endif
}

// ==================== PRÉPARATION À LA VEILLE ====================
void SensorManager::prepareSleep() {
    Serial.println("[SensorManager] Préparation à la veille...");
#ifndef TEST_INTEGRATION
    if (pMAX30102) {
        pMAX30102->sensorEndCollect();
        Serial.println("[SensorManager] MAX30102 -> arrêt collecte");
    }
    if (pMPU6050) {
        pMPU6050->setSleepEnabled(true);
        Serial.println("[SensorManager] MPU6050 -> SLEEP");
    }
#else
    Serial.println("[SensorManager] Mode TEST_INTEGRATION -> rien à faire");
#endif
}

// ==================== MISE À JOUR DES LECTURES ====================
void SensorManager::updateReadings() {
#ifdef TEST_INTEGRATION
    // Valeurs simulées pour les tests d'intégration BLE / appli mobile
    lastHeartRate = 75;
    lastSpO2 = 98;
    stepCounter += 10;
#else
    if (pMAX30102) {
        pMAX30102->getHeartbeatSPO2();
        lastHeartRate = pMAX30102->_sHeartbeatSPO2.Heartbeat;
        lastSpO2 = pMAX30102->_sHeartbeatSPO2.SPO2;
    }

    if (pMPU6050) {
        int16_t ax, ay, az;
        pMPU6050->getAcceleration(&ax, &ay, &az);
        lastAx = ax;
        lastAy = ay;
        lastAz = az;
        detectSteps();
    }
#endif
}

// ==================== DÉTECTION DE PAS ====================
#ifndef TEST_INTEGRATION
void SensorManager::detectSteps() {
    accelMagnitude = sqrt(
        (float)lastAx * lastAx +
        (float)lastAy * lastAy +
        (float)lastAz * lastAz
    );

    if (accelMagnitude > STEP_THRESHOLD) {
        if (!isMoving) {
            stepCounter++;
            isMoving = true;
            #ifdef DEBUG_STEPS
            Serial.print("[STEP] Pas détecté! Total: ");
            Serial.println(stepCounter);
            #endif
        }
    } else {
        isMoving = false;
    }
}
#endif