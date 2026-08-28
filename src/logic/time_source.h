#ifndef TIME_SOURCE_H
#define TIME_SOURCE_H

#include <stdint.h>

// Le bracelet n'a pas d'horloge : au boot il ne sait pas quelle heure il est.
// L'app lui écrit l'epoch UTC sur la caractéristique TIME à chaque connexion ;
// entre deux synchros on extrapole avec millis().
//
// Header-only et sans Arduino : millis() est passé en paramètre, ce qui permet
// de tester le wrap sur PC sans attendre 49 jours.
class TimeSource {
public:
    // Appelé quand l'app écrit TIME. `nowMs` = millis() au même instant.
    void sync(uint32_t epochSeconds, uint32_t nowMs) {
        epochBase_ = epochSeconds;
        msBase_ = nowMs;
        synced_ = true;
    }

    // 0 tant que l'app n'a jamais donné l'heure. Les mesures horodatées 0
    // partent quand même : c'est l'app qui leur assignera une heure.
    uint32_t now(uint32_t nowMs) const {
        if (!synced_) return 0;
        // Soustraction en uint32_t : correcte même quand millis() a wrappé.
        uint32_t elapsedMs = nowMs - msBase_;
        return epochBase_ + elapsedMs / 1000u;
    }

    bool isSynced() const { return synced_; }

private:
    uint32_t epochBase_ = 0;
    uint32_t msBase_ = 0;
    bool synced_ = false;
};

#endif // TIME_SOURCE_H
