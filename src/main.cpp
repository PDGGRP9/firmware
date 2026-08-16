#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "ble_manager.h"
#include "sensors.h"
#include "power_management.h"

// ==================== OBJETS GLOBAUX ====================
BLEManager bleManager;
SensorManager sensorManager;
PowerManager powerManager;

bool init_success = true;
uint8_t error_code = 0x1;
unsigned long lastReadTime = 0;

// ==================== BOUTON / LONG PRESS ====================
// Tout ce bloc dépend du bouton câblé sur D0 : sans HAS_POWER_BUTTON, il n'y a
// rien à lire et la veille n'est jamais déclenchée.
#ifdef HAS_POWER_BUTTON
#define BUTTON_PIN D0
#define LONG_PRESS_DURATION 3000 // 3 secondes

bool buttonPressed = false;
unsigned long buttonPressStart = 0;

void checkButtonForSleep() {
  bool currentState = digitalRead(BUTTON_PIN);

  if (currentState == HIGH) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressStart = millis();
    } else {
      if (millis() - buttonPressStart >= LONG_PRESS_DURATION) { // Appui long détecté
        Serial.println("[Main] Appui long détecté (3s) -> Mise en veille prolongée");

        digitalWrite(LED_BUILTIN, LOW);
        delay(200);

        // Mise en veille prolongée
        sensorManager.prepareSleep();
        powerManager.enterDeepSleep();
      }
    }
  } else { // Bouton relâché trop tôt
    buttonPressed = false;
  }
}
#endif // HAS_POWER_BUTTON

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
  delay(3000); // Attente pour le moniteur série

  // Vérifier la cause du réveil
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[Setup] Réveil depuis veille prolongée (bouton)");
  } else {
    Serial.println("[Setup] Démarrage normal");
  }

  Serial.println("\n=====================================");
  Serial.println("   INITIALISATION BRASCO - v1.0      ");
  Serial.println("=====================================");
  Serial.print("Device ID: ");
  Serial.println(DEV_ID);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
#ifdef HAS_POWER_BUTTON
  pinMode(BUTTON_PIN, INPUT);
#endif

  powerManager.init();

  // === Initialisation I2C et capteurs ===
  if (!sensorManager.initI2C(SDA_PIN, SCL_PIN)) {
    init_success = false;
    error_code = ERR_I2C;
  }

  if (!sensorManager.initMAX30102()) {
    init_success = false;
    error_code = ERR_MAX30102;
  }

  if (!sensorManager.initMPU6050()) {
    init_success = false;
    error_code = ERR_MPU6050;
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
  // Vérifier l'appui long à chaque itération, même en cas d'erreur
#ifdef HAS_POWER_BUTTON
  checkButtonForSleep();
#endif

  // === Gestion des erreurs et réinitialisation ===
  while (!init_success) {
    for (int i = 0; i < error_code; ++i) {
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
#ifdef HAS_POWER_BUTTON
      checkButtonForSleep(); // on garde la main sur le bouton même en erreur
#endif
    }

    switch (error_code) {
      case ERR_I2C:
        if (!sensorManager.initI2C(SDA_PIN, SCL_PIN)) {
          init_success = false;
          error_code = ERR_I2C;
        } else {
          init_success = true;
        }
        break;
      case ERR_MAX30102:
        if (!sensorManager.initMAX30102()) {
          init_success = false;
          error_code = ERR_MAX30102;
        } else {
          init_success = true;
        }
        break;
      case ERR_MPU6050:
        if (!sensorManager.initMPU6050()) {
          init_success = false;
          error_code = ERR_MPU6050;
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

    // Les mesures sont toujours affichées sur le série, connecté ou non :
    // c'est le seul moyen de vérifier les capteurs sans téléphone appairé.
    if (bleManager.isConnected()) {
      bleManager.sendSensorData(hr, spo2, steps);
      Serial.print("[BLE] Data sent");
    } else {
      Serial.print("[BLE] No device connected");
    }
    Serial.print(" -> HR: ");
    Serial.print(hr);
    Serial.print(" | SpO2: ");
    Serial.print(spo2);
    Serial.print(" | Steps: ");
    Serial.println(steps);
  }
}