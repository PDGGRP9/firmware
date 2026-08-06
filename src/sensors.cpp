#include "sensors.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

// ==================== CONSTRUCTEUR / DESTRUCTEUR ====================
SensorManager::SensorManager()
    : pMAX30102(nullptr), pMPU6050(nullptr),
      lastHeartRate(0), lastSpO2(0), stepCounter(0),
      lastAx(0), lastAy(0), lastAz(0), accelMagnitude(0.0f),
      isMoving(false) {
}

SensorManager::~SensorManager() {
    if (pMAX30102) delete pMAX30102;
    if (pMPU6050) delete pMPU6050;
}

// ==================== INITIALISATION I2C ====================
bool SensorManager::initI2C(uint8_t sda, uint8_t scl) {
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
}

// ==================== INITIALISATION MAX30102 ====================
bool SensorManager::initMAX30102() {
    Serial.print("[SensorManager] Initialisation MAX30102 (Oxymètre)... ");
    
    pMAX30102 = new DFRobot_BloodOxygen_S_I2C(&Wire, I2C_ADDRESS);
    
    if (!pMAX30102->begin()) {
        Serial.println("[FAIL]");
        delete pMAX30102;
        pMAX30102 = nullptr;
        return false;
    }
    
    // Démarrer la collecte de données
    pMAX30102->sensorStartCollect();
    Serial.println("[OK]");
    return true;
}

// ==================== INITIALISATION MPU6050 ====================
bool SensorManager::initMPU6050() {
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
}

// ==================== INITIALISATION GÉNÉRALE ====================
bool SensorManager::initialize(uint8_t sda, uint8_t scl) {
    Serial.println("\n--- Initialisation Capteurs ---");
    
    bool success = true;
    
    // Initialiser I2C
    if (!initI2C(sda, scl)) {
        Serial.println("[FAIL] Bus I2C non initialisé");
        return false;
    }
    
    // Initialiser MAX30102
    if (!initMAX30102()) {
        Serial.println("[FAIL] Vérifiez le câblage du MAX30102");
        success = false;
    }
    
    // Initialiser MPU6050
    if (!initMPU6050()) {
        Serial.println("[FAIL] Vérifiez le câblage du MPU6050");
        success = false;
    }
    
    if (success) {
        Serial.println("[OK] Tous les capteurs initialisés");
    }
    
    return success;
}

// ==================== MISE À JOUR DES LECTURES ====================
void SensorManager::updateReadings() {
    // Lire MAX30102
    if (pMAX30102) {
        pMAX30102->getHeartbeatSPO2();
        lastHeartRate = pMAX30102->_sHeartbeatSPO2.Heartbeat;
        lastSpO2 = pMAX30102->_sHeartbeatSPO2.SPO2;
    }
    
    // Lire MPU6050 et détecter les pas
    if (pMPU6050) {
        int16_t ax, ay, az;
        pMPU6050->getAcceleration(&ax, &ay, &az);
        
        // Mettre à jour les dernières valeurs
        lastAx = ax;
        lastAy = ay;
        lastAz = az;
        
        // Détecter les pas
        detectSteps();
    }
}

// ==================== DÉTECTION DE PAS ====================
void SensorManager::detectSteps() {
    // Calculer la magnitude de l'accélération
    // Formule : sqrt(ax² + ay² + az²)
    accelMagnitude = sqrt(
        (float)lastAx * lastAx +
        (float)lastAy * lastAy +
        (float)lastAz * lastAz
    );
    
    // Détecter un pic d'accélération (passage du pied)
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