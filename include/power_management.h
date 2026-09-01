#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>
#include "esp_sleep.h"

#define WAKEUP_GPIO GPIO_NUM_8  // D9 on the XIAO ESP32S3 (must stay == BUTTON_PIN)

// Deep sleep entry and wake-up source (button on WAKEUP_GPIO).
class PowerManager {
  public:
    void init();
    void enterDeepSleep();  // sensor prepareSleep() is done by the caller
};

#endif
