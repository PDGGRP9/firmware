#ifndef TIME_SOURCE_H
#define TIME_SOURCE_H

#include <stdint.h>

#include "measurement.h"

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

    // Tant que l'app n'a pas donné l'heure, on renvoie l'uptime en secondes
    // plutôt que 0. Deux raisons : des mesures toutes horodatées 0 partagent le
    // même ts, et la dédup de l'app par (deviceUid, ts) n'en garderait qu'une ;
    // et l'uptime est justement ce qu'il faut à resolve() pour retrouver leur
    // vraie heure une fois la synchro faite. TS_EPOCH_MIN les distingue.
    uint32_t now(uint32_t nowMs) const {
        if (!synced_) return nowMs / 1000u;
        // Soustraction en uint32_t : correcte même quand millis() a wrappé.
        uint32_t elapsedMs = nowMs - msBase_;
        return epochBase_ + elapsedMs / 1000u;
    }

    // Rend son epoch réel à une mesure prise avant la synchro : elle porte son
    // uptime, et on connaît maintenant l'uptime du point de synchro, donc on
    // remonte le temps depuis ce point. Un ts déjà epoch ressort inchangé.
    //
    // Limite connue, non gérée : un uptime postérieur au point de synchro vient
    // forcément d'un boot précédent (bracelet redémarré avec du stock non vidé),
    // et sa base n'existe plus. On le laisse tel quel — l'app lui donnera son
    // heure de réception.
    uint32_t resolve(uint32_t ts) const {
        if (!synced_ || ts >= TS_EPOCH_MIN) return ts;
        uint32_t syncUptime = msBase_ / 1000u;
        if (ts > syncUptime) return ts;
        return epochBase_ - (syncUptime - ts);
    }

    bool isSynced() const { return synced_; }

private:
    uint32_t epochBase_ = 0;
    uint32_t msBase_ = 0;
    bool synced_ = false;
};

#endif // TIME_SOURCE_H
