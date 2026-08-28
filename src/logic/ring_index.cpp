#include "logic/ring_index.h"

uint32_t RingIndex::push() {
    uint32_t slot = (head_ + count_) % capacity_;
    if (count_ < capacity_) {
        count_++;
    } else {
        // Plein : le slot qu'on écrit est justement celui du plus ancien,
        // donc head_ avance d'un cran et cette mesure-là est perdue.
        head_ = (head_ + 1) % capacity_;
        dropped_++;
    }
    return slot;
}

uint32_t RingIndex::slotAt(uint32_t offset) const {
    return (head_ + offset) % capacity_;
}

void RingIndex::release(uint32_t n) {
    if (n > count_) n = count_;
    head_ = (head_ + n) % capacity_;
    count_ -= n;
}

void RingIndex::restore(uint32_t head, uint32_t count) {
    if (head >= capacity_ || count > capacity_) {
        head_ = 0;
        count_ = 0;
        return;
    }
    head_ = head;
    count_ = count;
}
