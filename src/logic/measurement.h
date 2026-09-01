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

// Un paquet d'historique = [type][count][seqLo][seqHi] puis les mesures.
// Le numéro de séquence est renvoyé tel quel dans l'ACK : sans lui, un ACK en
// retard serait pris pour celui du paquet courant et purgerait des mesures que
// l'app n'a jamais reçues.
constexpr size_t  HISTORY_HEADER_SIZE = 4;
// 0x11 et pas 0x01 : une app d'avant le numéro de séquence lirait les mesures
// deux octets trop tôt et acquitterait des données décalées, sans rien voir.
// Avec un type qu'elle ne connaît pas, elle refuse le paquet et ça se voit.
constexpr uint8_t HISTORY_TYPE_DATA   = 0x11;  // il reste des mesures
constexpr uint8_t HISTORY_TYPE_END    = 0xFF;  // stock vide, l'app peut passer en live

// Commandes reçues sur la caractéristique SYNC_CTRL.
// START et STOP tiennent en 1 octet, ACK en fait 3 : [0x02][seqLo][seqHi].
constexpr uint8_t SYNC_CMD_START = 0x01;
constexpr uint8_t SYNC_CMD_ACK   = 0x02;
constexpr uint8_t SYNC_CMD_STOP  = 0x03;

// Sous ce seuil, `ts` n'est pas un epoch mais l'uptime du bracelet en secondes :
// la mesure a été prise avant que l'app ait donné l'heure. Un epoch réel est
// forcément au-dessus (sept. 2020), un uptime ne l'atteint jamais. L'app Android
// applique la même règle (BraceletMeasurementCodec.TS_EPOCH_MIN).
constexpr uint32_t TS_EPOCH_MIN = 1600000000u;

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

// Écrit [0x11][count][seqLo][seqHi] + count mesures dans `out`.
// Retourne le nombre d'octets écrits.
size_t buildHistoryPacket(const Measurement* items, uint8_t count, uint16_t seq, uint8_t* out);

// Écrit [0xFF][0][0][0] : « je n'ai plus rien en stock ». Retourne 4.
// Ce paquet n'est jamais acquitté, sa séquence vaut donc 0.
size_t buildHistoryEndPacket(uint8_t* out);

#endif // MEASUREMENT_H
