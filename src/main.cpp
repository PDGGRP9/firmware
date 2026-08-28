#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "ble_manager.h"
#include "sensors.h"
#include "power_management.h"
#include "storage.h"
#include "logic/measurement.h"
#include "logic/time_source.h"

// ==================== OBJETS GLOBAUX ====================
BLEManager bleManager;
SensorManager sensorManager;
PowerManager powerManager;
Storage storage;
// Le bracelet n'a pas d'horloge : l'app lui écrit l'heure à chaque connexion.
TimeSource timeSource;

bool init_success = true;
uint8_t error_code = 0x1;
unsigned long lastReadTime = 0;

// ==================== LED D'ÉTAT (D10) ====================
// LED externe câblée en actif haut (voir STATUS_LED_ON dans config.h) : allumée
// en fonctionnement normal, 4 clignotements au passage en veille. Sans
// HAS_STATUS_LED (LED pas câblée), ces fonctions ne font rien : les appels dans
// setup/loop restent identiques.
// À ne pas confondre avec LED_BUILTIN (GPIO21), la LED orange de la carte, qui
// elle est actif BAS : LOW l'allume, HIGH l'éteint.
#ifdef HAS_STATUS_LED
void statusLedInit() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, STATUS_LED_OFF);
}

void statusLedOn()  { digitalWrite(STATUS_LED_PIN, STATUS_LED_ON); }
void statusLedOff() { digitalWrite(STATUS_LED_PIN, STATUS_LED_OFF); }

// Clignotement « signe de vie », appelé à chaque tour de loop(). Contrairement à
// blinkSleepSignal(), interdit de bloquer ici : le BLE et les capteurs doivent
// continuer à tourner. Une LED qui alterne = la loop tourne ; une LED figée
// (allumée ou éteinte) = la carte est coincée quelque part.
unsigned long lastBlinkMs = 0;
bool statusLedState = false;

void statusLedBlinkTick() {
  if (millis() - lastBlinkMs < STATUS_BLINK_PERIOD) return;
  lastBlinkMs = millis();
  statusLedState = !statusLedState;
  digitalWrite(STATUS_LED_PIN, statusLedState ? STATUS_LED_ON : STATUS_LED_OFF);
}

// Le tick inverse l'état à chaque demi-période : sans ça, après statusLedOn()
// le premier toggle rallumerait au lieu d'éteindre.
void statusLedMarkOn() { statusLedState = true; lastBlinkMs = millis(); }

// 4 clignotements = « je passe en veille ». Bloquant volontairement : on est
// déjà en train de s'endormir, plus rien d'autre ne tourne.
void blinkSleepSignal() {
  for (uint8_t i = 0; i < SLEEP_BLINK_COUNT; ++i) {
    statusLedOff();
    delay(SLEEP_BLINK_PERIOD);
    statusLedOn();
    delay(SLEEP_BLINK_PERIOD);
  }
  statusLedOff();
}
#else
inline void statusLedInit() {}
inline void statusLedOn() {}
inline void statusLedOff() {}
inline void statusLedBlinkTick() {}
inline void statusLedMarkOn() {}
inline void blinkSleepSignal() {}
#endif // HAS_STATUS_LED

// ==================== BOUTON / LONG PRESS ====================
// Tout ce bloc dépend du bouton câblé sur D9 (GPIO8) vers GND : sans
// HAS_POWER_BUTTON, il n'y a rien à lire et la veille n'est jamais déclenchée.
#ifdef HAS_POWER_BUTTON
#define LONG_PRESS_DURATION 3000 // 3 secondes

bool buttonPressed = false;
unsigned long buttonPressStart = 0;
// Compteur d'appuis : il dit d'un coup d'oeil si un seul appui a produit
// plusieurs fronts (rebond mécanique) ou si la pin lit du bruit.
uint32_t buttonTouchCount = 0;
unsigned long lastReleaseMs = 0;

void checkButtonForSleep() {
  int currentState = digitalRead(BUTTON_PIN);
  
  
  if (currentState == BUTTON_ACTIVE_LEVEL) {
    if (!buttonPressed) {
      // Front ignoré s'il suit de trop près le relâchement précédent : c'est du
      // rebond, pas un nouvel appui.
      if (millis() - lastReleaseMs < BUTTON_DEBOUNCE_MS) return;

      buttonPressed = true;
      buttonPressStart = millis();
      ++buttonTouchCount;
      // Sans cette trace, un bouton qui ne réagit pas est indébuggable : on ne
      // sait pas distinguer « pas de contact » de « appui pas assez long ».
      // Elle s'imprime à côté des lignes [STATE] du heartbeat BLE.
      Serial.printf("[BTN] touched (n=%lu, t=%lu ms) -> maintiens 3 s pour la veille\n",
                    (unsigned long)buttonTouchCount, buttonPressStart);
    } else {
      if (millis() - buttonPressStart >= LONG_PRESS_DURATION) { // Appui long détecté
        Serial.println("[Main] Appui long détecté (3s) -> Mise en veille prolongée");

        digitalWrite(LED_BUILTIN, HIGH);  // actif bas : HIGH éteint la LED carte
        blinkSleepSignal();  // retour visuel immédiat sur l'appui long
        // Le tampon RAM doit atterrir en flash : il n'est pas conservé en veille.
        storage.flush();
        delay(200);

        // Mise en veille prolongée
        sensorManager.prepareSleep();
        powerManager.enterDeepSleep();
      }
    }
  } else if (buttonPressed) { // Bouton relâché trop tôt
    Serial.print("[Main] Appui relâché après ");
    Serial.print(millis() - buttonPressStart);
    Serial.println(" ms (< 3000) -> pas de veille");
    buttonPressed = false;
    lastReleaseMs = millis();
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
  digitalWrite(LED_BUILTIN, HIGH);  // actif bas : éteinte tant que l'init tourne
  statusLedInit();  // éteinte tant que l'init n'a pas réussi
#ifdef HAS_POWER_BUTTON
  // PULLUP obligatoire : en INPUT nu la pin flotte et lit du bruit, ce qui
  // rendait l'appui long soit indétectable soit déclenché au hasard.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
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

  // === Initialisation du stockage ===
  if (!storage.begin()) {
    init_success = false;
    error_code = ERR_STORAGE;
  }

  // === Initialisation BLE ===
  if (!bleManager.initialize(&storage, &timeSource)) {
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
    digitalWrite(LED_BUILTIN, LOW);  // actif bas : LOW l'allume
    statusLedOn();      // mode normal
    statusLedMarkOn();  // le clignotement de loop() reprend depuis cet état
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
      digitalWrite(LED_BUILTIN, LOW);   // allumée (GPIO21 actif bas)
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);  // éteinte
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
      case ERR_STORAGE:
        if (!storage.begin()) {
          init_success = false;
          error_code = ERR_STORAGE;
        } else {
          init_success = true;
        }
        break;
      case ERR_BLE_INIT:
        if (!bleManager.initialize(&storage, &timeSource)) {
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

  // Init OK et loop qui tourne : la LED d'état clignote. Placé après la boucle
  // d'erreur, donc un clignotement régulier signifie bien « tout va bien ».
  statusLedBlinkTick();

  // === Protocole BLE (synchro du backlog, ACK, heure) ===
  // Toujours appelé : les callbacks NimBLE ne font que déposer les commandes.
  bleManager.tick();

  // === Lecture et envoi des données ===
  if (millis() - lastReadTime >= READ_INTERVAL) {
    lastReadTime = millis();

    sensorManager.updateReadings();

    uint8_t hr = sensorManager.getHeartRate();
    uint8_t spo2 = sensorManager.getSpO2();
    uint32_t steps = sensorManager.getSteps();

    // La mesure part en direct si l'app est en LIVE. Sinon — pas d'app,
    // synchro en cours, notify refusé — elle va en flash et deviendra du
    // backlog. Rien n'est jamais jeté (cf. README, invariante centrale).
    Measurement m;
    m.ts    = timeSource.now(millis());
    m.hr    = hr;
    m.spo2  = spo2;
    m.steps = (steps > 65535) ? 65535 : (uint16_t)steps;

    // Les mesures sont toujours affichées sur le série, connecté ou non :
    // c'est le seul moyen de vérifier les capteurs sans téléphone appairé.
    if (bleManager.sendLive(m)) {
      Serial.print("[BLE] Data sent");
    } else {
      storage.append(m);
      Serial.print("[BLE] Stored (backlog)");
    }
    Serial.print(" -> HR: ");
    Serial.print(hr);
    Serial.print(" | SpO2: ");
    Serial.print(spo2);
    Serial.print(" | Steps: ");
    Serial.println(steps);
  }
}
