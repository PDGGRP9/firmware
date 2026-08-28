#ifndef STORAGE_H
#define STORAGE_H

#include <Preferences.h>

#include "config.h"
#include "logic/measurement.h"
#include "logic/ring_index.h"

// Stockage des mesures qui n'ont pas encore été acquittées par l'app.
//
// Deux étages :
//   1. un tampon RAM de RAM_BATCH mesures. On n'écrit pas en flash pour une
//      seule mesure toutes les 4 s, ça userait la flash pour rien.
//   2. un fichier de taille fixe en LittleFS (STORAGE_FILE) utilisé comme
//      tampon circulaire : on se place par seek() au slot que donne RingIndex
//      et on écrit 8 octets. Pas de réécriture du fichier entier.
//
// Les index head/count vivent en NVS (Preferences) pour survivre au reboot et
// au deep sleep. Sans ça, un redémarrage perdrait tout le backlog.
//
// L'invariante du protocole : rien n'est purgé avant confirm(), et confirm()
// n'est appelé qu'après l'ACK de l'app.
class Storage {
public:
    Storage() : ring_(RING_CAPACITY), ramBuffer_{} {}

    // Monte LittleFS, crée/agrandit le fichier si besoin, recharge les index.
    bool begin();

    // Ajoute une mesure (va d'abord en RAM, flush automatique quand plein).
    bool append(const Measurement& m);

    // Force l'écriture du tampon RAM en flash.
    bool flush();

    // Lit jusqu'à `max` mesures parmi les plus anciennes, SANS les supprimer.
    // Retourne le nombre lu.
    uint8_t readBatch(Measurement* out, uint8_t max);

    // Supprime les n plus anciennes. À n'appeler QU'APRÈS l'ACK de l'app.
    void confirm(uint8_t n);

    uint32_t pending() const { return ring_.count() + ramCount_; }
    uint32_t pendingInFlash() const { return ring_.count(); }
    uint32_t ramPending() const { return ramCount_; }
    uint32_t dropped() const { return ring_.dropped(); }
    bool isReady() const { return ready_; }

private:
    void saveIndex();

    RingIndex ring_;
    Preferences prefs_;
    Measurement ramBuffer_[RAM_BATCH];
    uint8_t ramCount_ = 0;
    bool ready_ = false;
};

#endif // STORAGE_H
