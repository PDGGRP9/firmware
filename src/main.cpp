#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "ble_manager.h"
#include "sensors.h"
#include "power_management.h"
#include "storage.h"
#include "logic/measurement.h"
#include "logic/time_source.h"

// ==================== GLOBAL OBJECTS ====================
BLEManager bleManager;
SensorManager sensorManager;
PowerManager powerManager;
Storage storage;
// The bracelet has no clock: the app writes the time on every connection.
TimeSource timeSource;

bool init_success = true;
uint8_t error_code = 0x1;
unsigned long lastReadTime = 0;
// Safety-net timer for the daily step total: deep sleep persists it explicitly,
// this covers an unexpected power loss (see STEP_PERSIST_INTERVAL).
unsigned long lastStepPersistMs = 0;
// Local calendar day of the last measurement, to reset the step counter at local
// midnight. INT32_MIN = "not known yet" (set on the first synced reading, no reset).
int32_t lastLocalDay = INT32_MIN;

// ==================== STATUS LED (D7) ====================
#ifdef HAS_STATUS_LED
void statusLedInit() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, STATUS_LED_OFF);
}

void statusLedOn()  { digitalWrite(STATUS_LED_PIN, STATUS_LED_ON); }
void statusLedOff() { digitalWrite(STATUS_LED_PIN, STATUS_LED_OFF); }

unsigned long lastBlinkMs = 0;
bool statusLedState = false;

void statusLedBlinkTick() {
  if (millis() - lastBlinkMs < STATUS_BLINK_PERIOD) return;
  lastBlinkMs = millis();
  statusLedState = !statusLedState;
  digitalWrite(STATUS_LED_PIN, statusLedState ? STATUS_LED_ON : STATUS_LED_OFF);
}

void statusLedMarkOn() { statusLedState = true; lastBlinkMs = millis(); }

// 4 blinks = "going to sleep". Blocking on purpose: we are already falling
// asleep, nothing else runs any more.
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

// ==================== BUTTON / LONG PRESS ====================
// This whole block needs the button wired on D9 (GPIO8) to GND: without
// HAS_POWER_BUTTON there is nothing to read and sleep is never triggered.
#ifdef HAS_POWER_BUTTON
#define LONG_PRESS_DURATION 3000 // 3 seconds

bool buttonPressed = false;
unsigned long buttonPressStart = 0;
// Press counter: tells at a glance whether one press produced several edges
// (mechanical bounce) or whether the pin is reading noise.
uint32_t buttonTouchCount = 0;
unsigned long lastReleaseMs = 0;

void checkButtonForSleep() {
  int currentState = digitalRead(BUTTON_PIN);
  
  
  if (currentState == BUTTON_ACTIVE_LEVEL) {
    if (!buttonPressed) {
      // Edge ignored when it follows the previous release too closely: that is
      // bounce, not a new press.
      if (millis() - lastReleaseMs < BUTTON_DEBOUNCE_MS) return;

      buttonPressed = true;
      buttonPressStart = millis();
      ++buttonTouchCount;
      // Without this trace an unresponsive button is impossible to debug: we
      // cannot tell "no contact" from "press too short". It shows up next to
      // the [STATE] lines of the BLE heartbeat.
      Serial.printf("[BTN] touched (n=%lu, t=%lu ms) -> hold 3 s to sleep\n",
                    (unsigned long)buttonTouchCount, buttonPressStart);
    } else {
      if (millis() - buttonPressStart >= LONG_PRESS_DURATION) { // Long press detected
        Serial.println("[Main] Long press detected (3s) -> entering deep sleep");

        digitalWrite(LED_BUILTIN, HIGH);  // active low: HIGH turns the board LED off
        blinkSleepSignal();  // immediate visual feedback on the long press
        // The RAM buffer must reach flash: it is not kept across deep sleep.
        storage.flush();
        // Same for the daily step total: RAM only, wiped by deep sleep.
        sensorManager.persistSteps(lastLocalDay);
        delay(200);

        sensorManager.prepareSleep();
        powerManager.enterDeepSleep();
      }
    }
  } else if (buttonPressed) { // Button released too early
    Serial.print("[Main] Press released after ");
    Serial.print(millis() - buttonPressStart);
    Serial.println(" ms (< 3000) -> no sleep");
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
  delay(3000); // wait for the serial monitor to attach

  // Why did we boot: cold start or button wake-up?
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[Setup] Woke up from deep sleep (button)");
  } else {
    Serial.println("[Setup] Normal start");
  }

  Serial.println("\n=====================================");
  Serial.println("   BRACECO INIT                     ");
  Serial.println("=====================================");
  Serial.print("Device ID: ");
  Serial.println(DEV_ID);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // active low: off while init runs
  statusLedInit();  // stays off until init succeeds
#ifdef HAS_POWER_BUTTON
  // PULLUP is mandatory: as plain INPUT the pin floats and reads noise, which
  // made the long press either undetectable or randomly triggered.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#endif

  powerManager.init();

  // === I2C and sensors ===
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

  // === Storage ===
  if (!storage.begin()) {
    init_success = false;
    error_code = ERR_STORAGE;
  }

  // === Daily step total ===
  // Restored from NVS: it must survive deep sleep and power loss. The saved day
  // seeds lastLocalDay, so a bracelet powered off across local midnight resets
  // the counter on its first synced reading in loop() (see the READ block).
  lastLocalDay = sensorManager.restoreSteps();

  // === BLE ===
  if (!bleManager.initialize(&storage, &timeSource)) {
    init_success = false;
    error_code = ERR_BLE_INIT;
  }

  if (!bleManager.startAdvertising()) {
    init_success = false;
    error_code = ERR_BLE_ADV;
  }

  // === Summary ===
  Serial.println("\n=====================================");
  if (init_success) {
    Serial.println("  OK - SYSTEM READY");
    digitalWrite(LED_BUILTIN, LOW);  // active low: LOW turns it on
    statusLedOn();      // normal mode
    statusLedMarkOn();  // the loop() blink restarts from this state
  } else {
    Serial.print("  ERROR: 0x");
    Serial.println(error_code, HEX);
  }
  Serial.println("=====================================\n");

  lastReadTime = millis();
}

// ==================== LOOP ====================
void loop() {
  // Check the long press on every iteration, even when init failed
#ifdef HAS_POWER_BUTTON
  checkButtonForSleep();
#endif

  // === Error blink and retry loop ===
  // The error code is blinked on the board LED, then the failed step is retried.
  while (!init_success) {
    for (int i = 0; i < error_code; ++i) {
      digitalWrite(LED_BUILTIN, LOW);   // on (GPIO21 is active low)
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH);  // off
      delay(200);
#ifdef HAS_POWER_BUTTON
      checkButtonForSleep(); // the button still works while in error
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
        Serial.println("Unknown error.");
        break;
    }

    if (init_success) {
      Serial.println("System successfully re-initialized.");
    } else {
      Serial.print("Retry failed. Error code: 0x");
      Serial.println(error_code, HEX);
    }
    delay(3000);
  }

  // Init OK and loop running: the status LED blinks. Placed after the error
  // loop, so a regular blink really means "all good".
  statusLedBlinkTick();

  // === BLE protocol (backlog sync, ACK, time) ===
  // Always called: the NimBLE callbacks only drop off the commands.
  bleManager.tick();

  // === Daily step total: periodic safety-net save ===
  // The explicit save is on the deep-sleep path; this only limits how many
  // steps an unexpected power loss can cost. persistSteps() is a no-op when
  // the count has not moved.
  if (millis() - lastStepPersistMs >= STEP_PERSIST_INTERVAL) {
    lastStepPersistMs = millis();
    sensorManager.persistSteps(lastLocalDay);
  }

  // === Step detection ===
  // Sampled here, every loop(), not on the READ_INTERVAL timer: a footfall lasts
  // ~0.5 s and the detector needs a steady ~50 Hz feed (it rate-limits itself).
  sensorManager.sampleMotion();

  // === Read and send the data ===
  if (millis() - lastReadTime >= READ_INTERVAL) {
    lastReadTime = millis();

    // The bracelet owns the daily step total: reset it at local midnight, using
    // the UTC offset the app sent with the time. Nothing downstream re-derives it.
    if (timeSource.isSynced()) {
      int32_t localDay = timeSource.localDayNumber(millis());
      if (lastLocalDay != INT32_MIN && localDay != lastLocalDay) {
        sensorManager.resetSteps();
        // Persist the reset right away, tagged with the new day: a power loss
        // before the next periodic save must not bring yesterday's total back.
        sensorManager.persistSteps(localDay);
        Serial.println("[Main] Local midnight -> step counter reset");
      }
      lastLocalDay = localDay;
    }

    sensorManager.updateReadings();

    uint8_t hr = sensorManager.getHeartRate();
    uint8_t spo2 = sensorManager.getSpO2();
    uint32_t steps = sensorManager.getSteps();

    // The measurement goes out live when the app is in LIVE. Otherwise - no
    // app, sync in progress, notify refused - it goes to flash and becomes
    // backlog. Nothing is ever thrown away (see README, central invariant).
    Measurement m;
    m.ts    = timeSource.now(millis());
    m.hr    = hr;
    m.spo2  = spo2;
    m.steps = (steps > 65535) ? 65535 : (uint16_t)steps;

    // Measurements are always printed on serial, connected or not: it is the
    // only way to check the sensors without a paired phone.
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
