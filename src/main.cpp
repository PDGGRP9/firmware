#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "config.h"
#include "ble_manager.h"
#include "sensors.h"

// ==================== OBJETS GLOBAUX ====================
BLEManager bleManager;
SensorManager sensorManager;

bool init_success = true;
uint8_t error_code = 0x1;
unsigned long lastReadTime = 0;

// ==================== UTILS ====================
void logResult(const char* stepName, bool passed, const char* detail) {
  Serial.print("[");
  Serial.print(passed ? "OK" : "FAIL");
  Serial.print("] ");
  Serial.print(stepName);
  Serial.print(" - ");
  Serial.println(detail);
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n=====================================");
  Serial.println("   INITIALISATION BRASCO - v1.0      ");
  Serial.println("=====================================");
  Serial.print("Device ID: ");
  Serial.println(DEV_ID);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // === Initialisation Capteurs ===
  if (!sensorManager.initialize(SDA_PIN, SCL_PIN)) {
    init_success = false;
    error_code = ERR_SENSORS;
  }

  // === Initialisation BLE ===
  if (!bleManager.initialize()) {
    init_success = false;
    error_code = ERR_BLE_INIT;
  }

  if (!bleManager.startAdvertising()) {
    init_success = false;
    error_code = ERR_BLE_ADV;
  }

  // === Résumé ===
  Serial.println("\n=====================================");
  if (init_success) {
    Serial.println("  ✓ SYSTÈME PRÊT");
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    Serial.print("  ✗ ERREUR: 0x");
    Serial.println(error_code, HEX);
  }
  Serial.println("=====================================\n");

  lastReadTime = millis();
}

// ==================== LOOP ====================
void loop() {
  // === Gestion des erreurs et réinitialisation ===
  while (!init_success) {
    // Clignoter en cas d'erreur
    for (int i = 0; i < error_code; ++i) {
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
    }

    switch (error_code) {
      case ERR_SENSORS:
        if (!sensorManager.initialize(SDA_PIN, SCL_PIN)) {
          init_success = false;
          error_code = ERR_SENSORS;
        } else {
          init_success = true;
        }
        break;
      case ERR_BLE_INIT:
        if (!bleManager.initialize()) {
          init_success = false;
          error_code = ERR_BLE_INIT;
        } else {
          init_success = true;
        }
        break;
      case ERR_BLE_ADV:
        if (!bleManager.startAdvertising()) {
          init_success = false;
          error_code = ERR_BLE_ADV;
        } else {
          init_success = true;
        }
        break;
      default:
        Serial.println("Erreur inconnue.");
        break;
    }

    if (init_success) {
      Serial.println("Système réinitialisé avec succès.");
    } else {
      Serial.print("Nouvelle tentative échouée. Code d'erreur: 0x");
      Serial.println(error_code, HEX);
    }
    delay(3000);
  }

  // === Lecture et envoi des données ===
  if (millis() - lastReadTime >= READ_INTERVAL) {
    lastReadTime = millis();

    sensorManager.updateReadings();

    uint8_t hr = sensorManager.getHeartRate();
    uint8_t spo2 = sensorManager.getSpO2();
    uint32_t steps = sensorManager.getSteps();

    if (bleManager.isConnected()) {
      bleManager.sendSensorData(hr, spo2, steps);

      Serial.print("JSON envoyé -> HR: ");
      Serial.print(hr);
      Serial.print(" | SpO2: ");
      Serial.print(spo2);
      Serial.print(" | Steps: ");
      Serial.println(steps);
    } else {
      Serial.println("Aucun appareil BLE connecté.");
    }
  }
}