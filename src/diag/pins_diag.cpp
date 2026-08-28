// Sketch de diagnostic matériel — PAS le firmware du bracelet.
// Se compile seulement dans l'env "esp32-s3-diag" (voir platformio.ini).
//
// But : séparer le problème matériel du problème logiciel avant de toucher au
// firmware. Il affiche l'état brut du bouton D9 dans les trois modes de tirage
// et fait clignoter la LED D10 en HIGH puis LOW pour trancher sa polarité.
//
// Lecture attendue, bouton NON câblé :  pullup=1  pulldown=0  (float saute)
//   GPIO8 ponté à GND  -> pullup=0
//   GPIO8 ponté à 3V3  -> pulldown=1
// Si la LED ne s'allume dans AUCUN des deux états : le défaut est matériel
// (LED absente, montée à l'envers, pas de résistance, pas reliée à D10).

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(3000);  // le temps que le moniteur USB CDC se rattache
  Serial.println("\n=== DIAG PINS : bouton D9 (GPIO8) / LED D10 (GPIO9) ===");
  Serial.println("Ponte GPIO8 a GND puis a 3V3 et regarde les valeurs bouger.");
  Serial.println("Regarde la LED : elle doit s'allumer dans UN des deux etats.\n");
}

// Lit D9 dans les trois modes. Le mode de pin est reconfiguré à chaque lecture :
// c'est lent mais ça donne les trois réponses d'un coup sans recompiler.
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
  Serial.println("--- D10 = HIGH (LED allumee ?) ---");
  for (uint8_t i = 0; i < 5; ++i) { readButton(); delay(300); }

  digitalWrite(D10, LOW);
  Serial.println("--- D10 = LOW (LED allumee ?) ---");
  for (uint8_t i = 0; i < 5; ++i) { readButton(); delay(300); }
}
