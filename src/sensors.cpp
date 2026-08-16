#include "sensors.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

// ==================== CONSTRUCTEUR / DESTRUCTEUR ====================
// La virgule est en tête de chaque bloc optionnel : comme ça aucune
// combinaison de flags ne laisse une virgule orpheline en fin de liste.
SensorManager::SensorManager()
    : lastHeartRate(0), lastSpO2(0), stepCounter(0)
#ifdef HAS_OXYGEN
      , pMAX30102(nullptr)
#endif
#ifdef HAS_IMU
      , pMPU6050(nullptr),
      lastAx(0), lastAy(0), lastAz(0), accelMagnitude(0.0f),
      isMoving(false)
#endif
{
}

SensorManager::~SensorManager() {
#ifdef HAS_OXYGEN
    if (pMAX30102) delete pMAX30102;
#endif
#ifdef HAS_IMU
    if (pMPU6050) delete pMPU6050;
#endif
}

// ==================== INITIALISATION I2C ====================
// Le bus n'est démarré que s'il y a au moins un capteur I2C câblé.
bool SensorManager::initI2C(uint8_t sda, uint8_t scl) {
#if !defined(HAS_OXYGEN) && !defined(HAS_IMU)
    (void)sda;
    (void)scl;
    Serial.println("[SensorManager] Aucun capteur I2C déclaré -> I2C ignoré");
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
#ifndef HAS_OXYGEN
    Serial.println("[SensorManager] HAS_OXYGEN absent -> MAX30102 ignoré");
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
#ifndef HAS_IMU
    Serial.println("[SensorManager] HAS_IMU absent -> MPU6050 ignoré");
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
#ifdef HAS_OXYGEN
    if (pMAX30102) {
        pMAX30102->sensorEndCollect();
        Serial.println("[SensorManager] MAX30102 -> arrêt collecte");
    }
#endif
#ifdef HAS_IMU
    if (pMPU6050) {
        pMPU6050->setSleepEnabled(true);
        Serial.println("[SensorManager] MPU6050 -> SLEEP");
    }
#endif
#if !defined(HAS_OXYGEN) && !defined(HAS_IMU)
    Serial.println("[SensorManager] Aucun capteur déclaré -> rien à faire");
#endif
}

// ==================== MISE À JOUR DES LECTURES ====================
// Chaque capteur a sa moitié indépendante : sans son flag, on retombe sur les
// valeurs simulées (utiles pour tester le BLE / l'appli sans le matériel).
void SensorManager::updateReadings() {
#ifdef HAS_OXYGEN
    if (pMAX30102) {
        pMAX30102->getHeartbeatSPO2();
        lastHeartRate = pMAX30102->_sHeartbeatSPO2.Heartbeat;
        lastSpO2 = pMAX30102->_sHeartbeatSPO2.SPO2;
    }
#else
    lastHeartRate = 75;
    lastSpO2 = 98;
#endif

#ifdef HAS_IMU
    if (pMPU6050) {
        int16_t ax, ay, az;
        pMPU6050->getAcceleration(&ax, &ay, &az);
        lastAx = ax;
        lastAy = ay;
        lastAz = az;
        detectSteps();
    }
#else
    stepCounter += 10;
#endif
}

// ==================== DÉTECTION DE PAS ====================
#ifdef HAS_IMU
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
#endif // HAS_IMU