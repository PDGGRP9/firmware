#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stddef.h>
#include <stdint.h>

// Une mesure du bracelet, telle qu'elle est stockée en flash ET envoyée en BLE.
// C'est le seul format de données du protocole : le live et l'historique
// utilisent exactement le même enregistrement.
struct Measurement {
    uint32_t ts;     // epoch UTC en secondes ; 0 = l'app n'a pas encore donné l'heure
    uint8_t  hr;     // BPM, 0 = pas de lecture
    uint8_t  spo2;   // %, 0 = pas de lecture
    uint16_t steps;  // cumul de pas, tronqué à 65535
};

// Taille sur le fil et en flash. On l'écrit à la main plutôt que sizeof() :
// le compilateur peut ajouter du padding, et ça décalerait tout le fichier ring.
constexpr size_t MEASUREMENT_SIZE = 8;

// Un paquet d'historique = [type][count] puis les mesures.
constexpr size_t  HISTORY_HEADER_SIZE = 2;
constexpr uint8_t HISTORY_TYPE_DATA   = 0x01;  // il reste des mesures
constexpr uint8_t HISTORY_TYPE_END    = 0xFF;  // stock vide, l'app peut passer en live

// Commandes reçues sur la caractéristique SYNC_CTRL (1 octet).
constexpr uint8_t SYNC_CMD_START = 0x01;
constexpr uint8_t SYNC_CMD_ACK   = 0x02;
constexpr uint8_t SYNC_CMD_STOP  = 0x03;

// Le driver du MAX30102 renvoie -1 quand il n'a pas de lecture valable (doigt
// absent, signal trop bruité). Rangé dans un uint8_t, ce -1 devient 255 : une
// valeur que le protocole n'a jamais prévue, et que le backend refuse. On
// ramène donc toute lecture aberrante à 0, le « pas de lecture » du contrat.
// `maxPlausible` : 250 BPM et 100 % SpO2, au-delà c'est du bruit.
uint8_t sanitizeReading(int32_t raw, uint8_t maxPlausible);

constexpr uint8_t MAX_PLAUSIBLE_HR   = 250;  // au-dessus, c'est le capteur qui délire
constexpr uint8_t MAX_PLAUSIBLE_SPO2 = 100;  // un pourcentage, forcément

// Little-endian explicite, octet par octet : le firmware et Android doivent
// lire pareil quelle que soit l'architecture. `out` doit faire MEASUREMENT_SIZE.
void encodeMeasurement(const Measurement& m, uint8_t* out);
Measurement decodeMeasurement(const uint8_t* in);

// Écrit [0x01][count] + count mesures dans `out`.
// Retourne le nombre d'octets écrits.
size_t buildHistoryPacket(const Measurement* items, uint8_t count, uint8_t* out);

// Écrit [0xFF][0] : « je n'ai plus rien en stock ». Retourne 2.
size_t buildHistoryEndPacket(uint8_t* out);

#endif // MEASUREMENT_H
