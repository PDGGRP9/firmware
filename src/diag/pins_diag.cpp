// Hardware diagnostic sketch - NOT the bracelet firmware.
// Only built in the "esp32-s3-diag" env (see platformio.ini).
//
// Goal: separate a hardware problem from a software one before touching the
// firmware. It prints the raw D9 button state in the three pull modes and
// blinks the D10 LED HIGH then LOW to settle its polarity.
//
// Expected reading, button NOT wired:  pullup=1  pulldown=0  (float jumps around)
//   GPIO8 shorted to GND  -> pullup=0
//   GPIO8 shorted to 3V3  -> pulldown=1
// If the LED lights up in NEITHER state, the fault is hardware (no LED, wired
// backwards, no resistor, or not connected to D10).

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(3000);  // give the USB CDC monitor time to attach
  Serial.println("\n=== PINS DIAG: button D9 (GPIO8) / LED D10 (GPIO9) ===");
  Serial.println("Short GPIO8 to GND then to 3V3 and watch the values move.");
  Serial.println("Watch the LED: it must light up in ONE of the two states.\n");
}

// Reads D9 in the three modes. The pin mode is reconfigured on every read:
// slow, but it gives the three answers at once without recompiling.
void readButton() {
  pinMode(D9, INPUT);
  int floating = digitalRead(D9);
  pinMode(D9, INPUT_PULLUP);
  int pulledUp = digitalRead(D9);
  pinMode(D9, INPUT_PULLDOWN);
  int pulledDown = digitalRead(D9);

  Serial.printf("[D9] float=%d pullup=%d pulldown=%d\n",
                floating, pulledUp, pulledDown);
}

void loop() {
  pinMode(D10, OUTPUT);

  digitalWrite(D10, HIGH);
  Serial.println("--- D10 = HIGH (LED on?) ---");
  for (uint8_t i = 0; i < 5; ++i) { readButton(); delay(300); }

  digitalWrite(D10, LOW);
  Serial.println("--- D10 = LOW (LED on?) ---");
  for (uint8_t i = 0; i < 5; ++i) { readButton(); delay(300); }
}
