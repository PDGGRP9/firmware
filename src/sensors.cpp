#include "sensors.h"
#include "logic/measurement.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

// ==================== CONSTRUCTOR / DESTRUCTOR ====================
// The comma leads each optional block so that no flag combination leaves an
// orphan comma at the end of the init list.
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

// ==================== I2C INIT ====================
// The bus is only started when at least one I2C sensor is wired.
bool SensorManager::initI2C(uint8_t sda, uint8_t scl) {
#if !defined(HAS_OXYGEN) && !defined(HAS_IMU)
    (void)sda;
    (void)scl;
    Serial.println("[SensorManager] No I2C sensor declared -> I2C skipped");
    return true;
#else
    Serial.print("[SensorManager] I2C init... ");
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

// ==================== MAX30102 INIT ====================
bool SensorManager::initMAX30102() {
#ifndef HAS_OXYGEN
    Serial.println("[SensorManager] HAS_OXYGEN missing -> MAX30102 skipped");
    return true;
#else
    Serial.print("[SensorManager] MAX30102 init (oximeter)... ");
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

// ==================== MPU6050 INIT ====================
bool SensorManager::initMPU6050() {
#ifndef HAS_IMU
    Serial.println("[SensorManager] HAS_IMU missing -> MPU6050 skipped");
    return true;
#else
    Serial.print("[SensorManager] MPU6050 init (accelerometer)... ");
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

// ==================== SLEEP PREPARATION ====================
void SensorManager::prepareSleep() {
    Serial.println("[SensorManager] Preparing for sleep...");
#ifdef HAS_OXYGEN
    if (pMAX30102) {
        pMAX30102->sensorEndCollect();
        Serial.println("[SensorManager] MAX30102 -> collection stopped");
    }
#endif
#ifdef HAS_IMU
    if (pMPU6050) {
        pMPU6050->setSleepEnabled(true);
        Serial.println("[SensorManager] MPU6050 -> SLEEP");
    }
#endif
#if !defined(HAS_OXYGEN) && !defined(HAS_IMU)
    Serial.println("[SensorManager] No sensor declared -> nothing to do");
#endif
}

// ==================== READING UPDATE ====================
// Each sensor has its own independent half: without its flag we fall back to
// simulated values (handy to test BLE / the app without the hardware).
void SensorManager::updateReadings() {
#ifdef HAS_OXYGEN
    if (pMAX30102) {
        pMAX30102->getHeartbeatSPO2();
        // The sensor returns -1 when it has nothing valid: without going
        // through sanitizeReading(), that -1 becomes 255 in a uint8_t and
        // travels as is up to the backend. The protocol says 0 = no reading.
        lastHeartRate = sanitizeReading(pMAX30102->_sHeartbeatSPO2.Heartbeat, MAX_PLAUSIBLE_HR);
        lastSpO2 = sanitizeReading(pMAX30102->_sHeartbeatSPO2.SPO2, MAX_PLAUSIBLE_SPO2);
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

// ==================== STEP DETECTION ====================
#ifdef HAS_IMU
void SensorManager::detectSteps() {
float gx = lastAx / accelResolution;
    float gy = lastAy / accelResolution;
    float gz = lastAz / accelResolution;

    float magnitude_g = sqrt(gx*gx + gy*gy + gz*gz);

    unsigned long now = millis();

    // === Filtrage passe-bas avec coefficient adaptatif ===
    // Coefficient plus élevé quand le mouvement est important pour suivre rapidement
    float dynamicAlpha = LOWPASS_ALPHA;
    if (abs(magnitude_g - 1.0f) > 0.5f) {
        dynamicAlpha = 0.6f;  // Réponse plus rapide aux mouvements brusques
    }
    
    filteredMagnitude = (dynamicAlpha * magnitude_g) + ((1.0f - dynamicAlpha) * filteredMagnitude);
    
    // === Estimation de la gravité avec filtre plus lent ===
    gravityEstimate = (GRAVITY_ALPHA * filteredMagnitude) + ((1.0f - GRAVITY_ALPHA) * gravityEstimate);

    // === Calcul de l'accélération dynamique ===
    float dynamicAccel = filteredMagnitude - gravityEstimate;
    
    // === Détection de pas basée sur le croisement de seuil ===
    // avec hystérésis et machine à états simple
    
    static bool stepInProgress = false;
    static float peakValue = 0.0f;
    static unsigned long cycleStartTime = 0;
    
    // Seuil adaptatif basé sur l'amplitude moyenne des pics récents
    static float avgPeakAmplitude = STEP_THRESHOLD_HIGH_G;
    static int peakCount = 0;
    static float peakSum = 0.0f;
    
    // === MACHINE À ÉTATS ===
    if (!stepInProgress) {
        // État : ATTENTE D'UN PIC
        if (dynamicAccel > STEP_THRESHOLD_HIGH_G) {
            // Début d'un pic potentiel
            stepInProgress = true;
            peakValue = dynamicAccel;
            cycleStartTime = now;
            
            #ifdef DEBUG_STEPS
            Serial.printf("[STEP] Pic détecté: %.3fg\n", dynamicAccel);
            #endif
        }
    } else {
        // État : PIC EN COURS
        if (dynamicAccel > peakValue) {
            // Le pic continue de monter
            peakValue = dynamicAccel;
        }
        
        // Vérifier si le pic est terminé (retour sous le seuil bas)
        if (dynamicAccel < STEP_THRESHOLD_LOW_G) {
            // Pic terminé
            unsigned long cycleDuration = now - cycleStartTime;
            
            // === VALIDATION DU PAS ===
            // Critères de validation :
            // 1. Durée minimale entre les pas (anti-rebond)
            // 2. Amplitude du pic suffisante
            // 3. Durée du cycle dans une plage raisonnable (0.3s à 2s)
            
            bool isStepValid = false;
            
            if ((now - lastStepTime) >= MIN_STEP_INTERVAL_MS) {
                if (cycleDuration >= 200 && cycleDuration <= 2000) {
                    // Vérifier si l'amplitude est suffisante
                    if (peakValue > STEP_THRESHOLD_HIGH_G) {
                        isStepValid = true;
                        
                        // Mettre à jour la moyenne des pics
                        if (peakCount < 10) {
                            peakCount++;
                            peakSum += peakValue;
                        } else {
                            // Fenêtre glissante simple
                            peakSum = peakSum * 0.9f + peakValue * 0.1f;
                        }
                        avgPeakAmplitude = peakSum / peakCount;
                    }
                }
            }
            
            if (isStepValid) {
                // Incrémenter le compteur de pas
                stepCounter++;
                lastStepTime = now;
                
                #ifdef DEBUG_STEPS
                Serial.printf("[STEP] ✅ VALIDÉ | dynAccel=%.3fg | durée=%lums | total=%lu\n", 
                              peakValue, cycleDuration, stepCounter);
                #endif
                
                // === DÉTECTION D'ACTIVITÉ ===
                // Si plusieurs pas rapprochés, c'est de la marche rapide ou course
                if (cycleDuration < 500) {
                    // Marche rapide ou course
                    #ifdef DEBUG_STEPS
                    Serial.println("[STEP] 🏃 Activité intense détectée");
                    #endif
                }
            } else {
                #ifdef DEBUG_STEPS
                const char* reason = "inconnue";
                if ((now - lastStepTime) < MIN_STEP_INTERVAL_MS) {
                    reason = "intervalle trop court";
                } else if (cycleDuration < 200) {
                    reason = "cycle trop court";
                } else if (cycleDuration > 2000) {
                    reason = "cycle trop long";
                } else if (peakValue <= STEP_THRESHOLD_HIGH_G) {
                    reason = "amplitude insuffisante";
                }
                Serial.printf("[STEP] ❌ REJETÉ (%s) | dynAccel=%.3fg | durée=%lums\n", 
                              reason, peakValue, cycleDuration);
                #endif
            }
            
            // Réinitialiser la machine à états
            stepInProgress = false;
            peakValue = 0.0f;
        }
        
        // Timeout de sécurité pour éviter les blocages
        if ((now - cycleStartTime) > 2500) {
            stepInProgress = false;
            peakValue = 0.0f;
            #ifdef DEBUG_STEPS
            Serial.println("[STEP] ⏱️ Timeout - cycle abandonné");
            #endif
        }
    }
}
#endif // HAS_IMU