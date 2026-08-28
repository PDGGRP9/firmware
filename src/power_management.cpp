#include "power_management.h"
#include "config.h"
#include "driver/rtc_io.h"

void PowerManager::init() {
    Serial.println("[PowerManager] Initialized");
}

void PowerManager::enterDeepSleep() {
    Serial.println("[PowerManager] Attente du relâchement du bouton...");

    // Sans ça la carte se rendormirait aussitôt réveillée : le bouton encore
    // enfoncé au moment du esp_deep_sleep_start() est déjà au niveau de réveil.
    while (digitalRead(BUTTON_PIN) == BUTTON_ACTIVE_LEVEL) {
        delay(50);
    }
    delay(200);  // petit debounce supplémentaire

    Serial.println("[PowerManager] Entering DEEP SLEEP now...");
    Serial.flush();
    // Sur USB CDC, flush() ne bloque pas jusqu'à l'émission réelle comme sur un
    // UART : sans ce délai le dernier message part à la poubelle quand le deep
    // sleep coupe l'USB, et on croit que la carte a planté.
    delay(100);

    // Les pull-up/down internes des GPIO sont coupés en deep sleep. En actif bas
    // la ligne se mettrait à flotter, ext0 verrait un niveau bas parasite et la
    // carte se réveillerait en boucle. Le pull-up du domaine RTC, lui, survit.
    rtc_gpio_pullup_en(WAKEUP_GPIO);
    rtc_gpio_pulldown_dis(WAKEUP_GPIO);

    // 0 = réveil quand la ligne descend, cohérent avec BUTTON_ACTIVE_LEVEL LOW.
    esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
    esp_deep_sleep_start();
}
