#ifndef RING_INDEX_H
#define RING_INDEX_H

#include <stdint.h>

// Arithmétique d'un tampon circulaire : quel slot écrire, quel slot lire.
// Aucun accès flash ici, volontairement — c'est ce qui rend la classe testable
// sur PC (`pio test -e native`). Storage se charge des vraies lectures/écritures.
//
// head_ = index du plus ancien élément non acquitté. Le tail est déduit
// (head_ + count_), on ne le stocke pas : deux compteurs à garder cohérents,
// c'est un de trop.
class RingIndex {
public:
    explicit RingIndex(uint32_t capacity) : capacity_(capacity) {}

    // Réserve le slot suivant. Si le ring est plein, on écrase la plus vieille
    // mesure (le récent vaut mieux que l'ancien) et on l'enregistre dans dropped_.
    uint32_t push();

    // Slot du n-ième plus ancien élément (offset 0 = le plus ancien).
    uint32_t slotAt(uint32_t offset) const;

    // Libère les n plus anciens : appelé UNIQUEMENT après l'ACK de l'app.
    void release(uint32_t n);

    // Recharge l'état depuis la NVS après un reboot. Valeurs incohérentes
    // (flash corrompue, capacité changée) -> on repart d'un ring vide plutôt
    // que de lire n'importe où dans le fichier.
    void restore(uint32_t head, uint32_t count);

    uint32_t head() const { return head_; }
    uint32_t count() const { return count_; }
    uint32_t capacity() const { return capacity_; }
    uint32_t dropped() const { return dropped_; }
    bool isFull() const { return count_ >= capacity_; }

private:
    uint32_t capacity_;
    uint32_t head_ = 0;
    uint32_t count_ = 0;
    uint32_t dropped_ = 0;  // mesures perdues par écrasement, remontées dans les logs
};

#endif // RING_INDEX_H
