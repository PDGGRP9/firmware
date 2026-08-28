#ifndef CONFIG_H
#define CONFIG_H

// MATÉRIEL PRÉSENT
// Ça ne se règle pas ici mais dans les build_flags de platformio.ini :
// HAS_OXYGEN, HAS_IMU, HAS_POWER_BUTTON. Un flag absent = capteur non câblé,
// le code correspondant est compilé out et remplacé par des valeurs simulées.

// Configuration générale
#define DEV_ID "BRASCO-00"
#define BLE_DEVICE_NAME "BRASCO-00"

// Pinout XIAO ESP32S3
#define SDA_PIN 5   // D4
#define SCL_PIN 6   // D5

// Bouton pour deep sleep (appui long 3 s). D9 = GPIO8, un RTC GPIO :
// obligatoire pour le réveil ext0 (voir WAKEUP_GPIO dans power_management.h).
#define BUTTON_PIN D9

// Câblage actif bas : GPIO8 -> bouton -> GND, avec le pull-up interne (voir le
// pinMode INPUT_PULLUP dans main.cpp). Pas de résistance externe à souder, et
// la ligne au repos est franche à 3V3 au lieu de flotter.
// Une pin flottante lue en INPUT nu, c'était le bug : du bruit, jamais un appui.
// Toute la logique bouton compare à cette constante — ne la changer qu'ici,
// et penser au niveau de réveil ext0 dans power_management.cpp (0 = actif bas).
#define BUTTON_ACTIVE_LEVEL LOW

// Anti-rebond : un contact mécanique rebondit ~1 à 10 ms. Sans ce garde, un
// seul appui compte plusieurs « touched » dans le log.
#define BUTTON_DEBOUNCE_MS 30

// LED d'état externe (mode normal / passage en veille), activée par le flag
// HAS_STATUS_LED dans platformio.ini.
// Câblage actif haut : GPIO -> résistance -> anode LED, cathode GND.
// HIGH l'allume (testé sur carte : en actif bas elle restait éteinte).
// Si elle ne s'allume dans aucun des deux états, c'est le câblage : flasher
// l'env esp32-s3-diag (src/diag/pins_diag.cpp) pour trancher avant de toucher ici.
#define STATUS_LED_PIN D10
#define STATUS_LED_ON  LOW
#define STATUS_LED_OFF HIGH

// Signal de passage en veille : 4 clignotements de 150 ms.
// Assez lent pour être vu, assez court pour ne pas retarder la veille (~1,2 s).
#define SLEEP_BLINK_COUNT  4
#define SLEEP_BLINK_PERIOD 150  // ms par demi-période (on puis off)

// Clignotement « signe de vie » en fonctionnement normal. 500 ms : assez lent
// pour qu'on voie l'alternance à l'oeil, assez rapide pour qu'une carte figée
// se remarque tout de suite (LED bloquée dans un état).
#define STATUS_BLINK_PERIOD 500  // ms par demi-période

// Capteurs
#define I2C_ADDRESS      0x57
#define MAX30102_ADDRESS 0x57
#define MPU6050_ADDRESS  0x68

// Code d'appairage BLE
#define BLE_STATIC_PASSKEY 123456

// BLE UUIDs
#define SERVICE_UUID "146ef449-0083-438a-9af6-5be5bb541e2c"
#define DATA_UUID    "146ef450-0083-438a-9af6-5be5bb541e2c"
// Les trois suivantes portent le protocole de rattrapage du backlog
// (voir README, "Protocole BLE - bracelet connecté")
#define HISTORY_UUID   "146ef451-0083-438a-9af6-5be5bb541e2c"  // notify : paquet de mesures stockées
#define SYNC_CTRL_UUID "146ef452-0083-438a-9af6-5be5bb541e2c"  // write  : START / ACK / STOP
#define TIME_UUID      "146ef453-0083-438a-9af6-5be5bb541e2c"  // write  : epoch UTC uint32 LE

// JSON
#define JSON_BUFFER_SIZE 256

// Constantes de temps
#define READ_INTERVAL 4000UL  // ms

// Stockage du backlog (voir include/storage.h)
#define STORAGE_FILE    "/data.bin"
#define RING_CAPACITY   25000UL  // 25000 * 8 o = 200 Ko en flash, ~27 h de mesures
#define RAM_BATCH       32       // mesures gardées en RAM avant d'écrire en flash
#define PREFS_NAMESPACE "brasco"

// Synchro
#define HISTORY_BATCH    20      // mesures par paquet ; 20*8+2 = 162 o, tient dans le MTU 185
#define SYNC_ACK_TIMEOUT 5000UL  // ms sans ACK -> on renvoie le paquet (rien n'a été purgé)

// Ligne d'état périodique sur le série : sans ça, un bracelet silencieux est
// indébuggable (on ne sait pas s'il attend un ACK, s'il stocke, s'il est seul).
#define STATE_LOG_INTERVAL 5000UL  // ms

// Codes d'erreur
#define ERR_BLE_INIT 0x2 // Erreur d'initialisation du BLE
#define ERR_BLE_ADV  0x3 // Erreur lors du démarrage de l'advertising BLE
#define ERR_I2C      0x4 // Erreur de communication I2C
#define ERR_MAX30102 0x5 // Erreur d'initialisation du capteur MAX30102
#define ERR_MPU6050  0x6 // Erreur d'initialisation du capteur MPU6050
#define ERR_STORAGE  0x7 // Erreur de montage LittleFS / fichier de stockage

#endif // CONFIG_H
