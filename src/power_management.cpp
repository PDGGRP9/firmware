#include "power_management.h"
#include "config.h"
#include "driver/rtc_io.h"

void PowerManager::init() {
    Serial.println("[PowerManager] Initialized");
}

// Waits for the button to be released, then arms the ext0 wake-up and sleeps.
void PowerManager::enterDeepSleep() {
    Serial.println("[PowerManager] Waiting for button release...");

    // Without this the board would wake up immediately: a button still pressed
    // at esp_deep_sleep_start() already sits at the wake-up level.
    while (digitalRead(BUTTON_PIN) == BUTTON_ACTIVE_LEVEL) {
        delay(50);
    }
    delay(200);  // a little extra debounce

    Serial.println("[PowerManager] Entering DEEP SLEEP now...");
    Serial.flush();
    // On USB CDC, flush() does not block until the bytes are really sent like on
    // a UART: without this delay the last message is lost when deep sleep cuts
    // USB, and it looks like the board crashed.
    delay(100);

    // Internal GPIO pull-ups/downs are disabled in deep sleep. Active low, the
    // line would start floating, ext0 would see a spurious low level and the
    // board would wake up in a loop. The RTC domain pull-up survives.
    rtc_gpio_pullup_en(WAKEUP_GPIO);
    rtc_gpio_pulldown_dis(WAKEUP_GPIO);

    // 0 = wake up on a falling line, consistent with BUTTON_ACTIVE_LEVEL LOW.
    esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
    esp_deep_sleep_start();
}
