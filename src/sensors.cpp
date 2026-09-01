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
            Serial.print("[STEP] Step detected! Total: ");
            Serial.println(stepCounter);
            #endif
        }
    } else {
        isMoving = false;
    }
}
#endif // HAS_IMU