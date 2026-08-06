#include <Arduino.h>
#include "message.h"

void setup() {
  Serial.begin(9600);
}

void loop() {
  Serial.println(getMessage());
  delay(500);
}
