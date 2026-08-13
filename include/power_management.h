#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>
#include "esp_sleep.h"

#define WAKEUP_GPIO GPIO_NUM_1  // D0 sur XIAO ESP32S3

class PowerManager {
  public:
    void init();
    void enterDeepSleep();  // Le prepareSleep() des capteurs est fait à l'extérieur
};

#endif