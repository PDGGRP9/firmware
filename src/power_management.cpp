#include "power_management.h"
#include "config.h"

void PowerManager::init() {
    Serial.println("[PowerManager] Initialized");
}

void PowerManager::enterDeepSleep() {
    Serial.println("[PowerManager] Attente du relâchement du bouton...");
    
    // Attend que le bouton soit relâché avant de dormir
    while (digitalRead(BUTTON_PIN) == HIGH) {  // adapte HIGH/LOW selon ton câblage
        delay(50);
    }
    delay(200);  // petit debounce supplémentaire

    Serial.println("[PowerManager] Entering DEEP SLEEP now...");
    Serial.flush();

    esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 1);  // adapte 0/1 selon ton câblage
    esp_deep_sleep_start();
}