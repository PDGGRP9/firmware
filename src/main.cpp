#include <Arduino.h>

const char* getMessage() {
  return "Hello World!";
}

void setup() {
  Serial.begin(9600);
}

void loop() {
  Serial.println(getMessage());
  delay(500);
}
