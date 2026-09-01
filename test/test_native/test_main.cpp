#include <unity.h>

#include "logic/measurement.h"
#include "logic/ring_index.h"
#include "logic/time_source.h"

void setUp(void) {}
void tearDown(void) {}

void test_sanity(void) {
  TEST_ASSERT_TRUE(true);
}

// --- Measurement ------------------------------------------------------------

void test_encode_decode_roundtrip(void) {
    Measurement in{1755950400u, 72, 98, 1234};
    uint8_t buf[MEASUREMENT_SIZE];
    encodeMeasurement(in, buf);
    Measurement out = decodeMeasurement(buf);

    TEST_ASSERT_EQUAL_UINT32(in.ts, out.ts);
    TEST_ASSERT_EQUAL_UINT8(in.hr, out.hr);
    TEST_ASSERT_EQUAL_UINT8(in.spo2, out.spo2);
    TEST_ASSERT_EQUAL_UINT16(in.steps, out.steps);
}

// Android lit ces octets dans cet ordre exact : si ce test casse, l'app
// affichera n'importe quoi.
void test_encode_is_little_endian(void) {
    Measurement m{0x11223344u, 0xAA, 0xBB, 0xCCDD};
    uint8_t buf[MEASUREMENT_SIZE];
    encodeMeasurement(m, buf);

    TEST_ASSERT_EQUAL_HEX8(0x44, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x33, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x22, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, buf[7]);
}

void test_zero_values_survive(void) {
    // hr=0 / spo2=0 = « pas de lecture », ts=0 = « heure inconnue ».
    // Ces cas passent par le même chemin, il ne faut pas qu'ils soient filtrés.
    Measurement in{0, 0, 0, 0};
    uint8_t buf[MEASUREMENT_SIZE];
    encodeMeasurement(in, buf);
    Measurement out = decodeMeasurement(buf);

    TEST_ASSERT_EQUAL_UINT32(0, out.ts);
    TEST_ASSERT_EQUAL_UINT8(0, out.hr);
}

void test_history_packet_framing(void) {
    Measurement items[3] = {{100, 60, 95, 1}, {104, 61, 96, 2}, {108, 62, 97, 3}};
    uint8_t buf[64];
    size_t len = buildHistoryPacket(items, 3, 0x0102, buf);

    TEST_ASSERT_EQUAL_UINT32(HISTORY_HEADER_SIZE + 3 * MEASUREMENT_SIZE, len);
    TEST_ASSERT_EQUAL_HEX8(HISTORY_TYPE_DATA, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(3, buf[1]);
    // Le numéro de séquence part en little-endian, comme le reste du protocole :
    // l'app le relit tel quel pour l'ACK.
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[3]);

    Measurement third = decodeMeasurement(buf + HISTORY_HEADER_SIZE + 2 * MEASUREMENT_SIZE);
    TEST_ASSERT_EQUAL_UINT32(108, third.ts);
    TEST_ASSERT_EQUAL_UINT16(3, third.steps);
}

void test_history_end_packet(void) {
    uint8_t buf[4];
    size_t len = buildHistoryEndPacket(buf);

    TEST_ASSERT_EQUAL_UINT32(4, len);
    TEST_ASSERT_EQUAL_HEX8(HISTORY_TYPE_END, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[1]);
    // Ce paquet n'est pas acquitté : pas de numéro de séquence.
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[3]);
}

// Le MTU BLE négocié est 185 : un paquet plein doit tenir dedans, sinon
// NimBLE tronque silencieusement la notification.
void test_full_packet_fits_in_mtu(void) {
    const size_t batch = 20;
    size_t maxLen = HISTORY_HEADER_SIZE + batch * MEASUREMENT_SIZE;
    TEST_ASSERT_EQUAL_UINT32(164, maxLen);
    TEST_ASSERT_TRUE(maxLen <= 185 - 3);  // 3 octets d'en-tête ATT
}

// --- RingIndex --------------------------------------------------------------

void test_ring_push_and_read_order(void) {
    RingIndex ring(4);
    TEST_ASSERT_EQUAL_UINT32(0, ring.push());
    TEST_ASSERT_EQUAL_UINT32(1, ring.push());
    TEST_ASSERT_EQUAL_UINT32(2, ring.push());

    TEST_ASSERT_EQUAL_UINT32(3, ring.count());
    TEST_ASSERT_EQUAL_UINT32(0, ring.slotAt(0));  // le plus ancien
    TEST_ASSERT_EQUAL_UINT32(2, ring.slotAt(2));
}

void test_ring_release_only_after_ack(void) {
    RingIndex ring(4);
    ring.push();
    ring.push();
    ring.push();

    ring.release(2);
    TEST_ASSERT_EQUAL_UINT32(1, ring.count());
    TEST_ASSERT_EQUAL_UINT32(2, ring.head());
    TEST_ASSERT_EQUAL_UINT32(2, ring.slotAt(0));

    // Libérer plus que ce qu'on a ne doit pas passer en négatif.
    ring.release(99);
    TEST_ASSERT_EQUAL_UINT32(0, ring.count());
}

void test_ring_wraps_and_counts_dropped(void) {
    RingIndex ring(3);
    ring.push();  // slot 0
    ring.push();  // slot 1
    ring.push();  // slot 2 -> plein
    TEST_ASSERT_TRUE(ring.isFull());
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());

    uint32_t slot = ring.push();  // écrase le plus ancien
    TEST_ASSERT_EQUAL_UINT32(0, slot);
    TEST_ASSERT_EQUAL_UINT32(1, ring.dropped());
    TEST_ASSERT_EQUAL_UINT32(3, ring.count());
    TEST_ASSERT_EQUAL_UINT32(1, ring.head());
    TEST_ASSERT_EQUAL_UINT32(1, ring.slotAt(0));
}

void test_ring_restore_rejects_garbage(void) {
    RingIndex ring(100);
    ring.restore(42, 10);
    TEST_ASSERT_EQUAL_UINT32(42, ring.head());
    TEST_ASSERT_EQUAL_UINT32(10, ring.count());

    // NVS vierge ou corrompue : on repart vide au lieu de lire hors du fichier.
    ring.restore(500, 10);
    TEST_ASSERT_EQUAL_UINT32(0, ring.head());
    TEST_ASSERT_EQUAL_UINT32(0, ring.count());
}

// --- TimeSource -------------------------------------------------------------

// Avant la synchro on renvoie l'uptime en secondes, pas 0 : sinon toutes les
// mesures du backlog partagent le même ts et la dédup de l'app n'en garde qu'une.
void test_time_uptime_before_sync(void) {
    TimeSource ts;
    TEST_ASSERT_FALSE(ts.isSynced());
    TEST_ASSERT_EQUAL_UINT32(123, ts.now(123456));
    // Sous TS_EPOCH_MIN : c'est ce qui permet de le distinguer d'un vrai epoch.
    TEST_ASSERT_TRUE(ts.now(123456) < TS_EPOCH_MIN);
}

void test_time_after_sync(void) {
    TimeSource ts;
    ts.sync(1755950400u, 10000);
    TEST_ASSERT_TRUE(ts.isSynced());
    TEST_ASSERT_EQUAL_UINT32(1755950400u, ts.now(10000));
    TEST_ASSERT_EQUAL_UINT32(1755950405u, ts.now(15000));
}

// millis() repasse à 0 après ~49 jours : la soustraction en uint32_t doit
// continuer à donner le bon écart.
void test_time_survives_millis_wrap(void) {
    TimeSource ts;
    uint32_t justBeforeWrap = 0xFFFFF000u;
    ts.sync(1000, justBeforeWrap);
    uint32_t afterWrap = justBeforeWrap + 5000;  // wrappe
    TEST_ASSERT_EQUAL_UINT32(1005, ts.now(afterWrap));
}


// Une mesure prise avant la synchro porte son uptime. Une fois l'heure reçue,
// resolve() doit retrouver son epoch en remontant depuis le point de synchro.
void test_resolve_gives_back_the_real_epoch(void) {
    TimeSource ts;
    // Synchro à l'uptime 100 s (millis = 100000) avec l'epoch 1755950400.
    ts.sync(1755950400u, 100000);

    // Mesure prise à l'uptime 40 s, soit 60 s avant la synchro.
    TEST_ASSERT_EQUAL_UINT32(1755950340u, ts.resolve(40));
    // Mesure prise à l'instant même de la synchro.
    TEST_ASSERT_EQUAL_UINT32(1755950400u, ts.resolve(100));
}

// Un ts déjà epoch ne doit surtout pas être retouché.
void test_resolve_leaves_a_real_epoch_alone(void) {
    TimeSource ts;
    ts.sync(1755950400u, 100000);
    TEST_ASSERT_EQUAL_UINT32(1755950405u, ts.resolve(1755950405u));
    TEST_ASSERT_EQUAL_UINT32(TS_EPOCH_MIN, ts.resolve(TS_EPOCH_MIN));
}

// Sans synchro on n'a aucune base : l'uptime ressort tel quel.
void test_resolve_without_sync_returns_input(void) {
    TimeSource ts;
    TEST_ASSERT_EQUAL_UINT32(40, ts.resolve(40));
}

// Limite connue, volontairement non gérée : un uptime postérieur au point de
// synchro vient d'un boot précédent (bracelet redémarré avec du stock non vidé).
// Sa base n'existe plus, on le laisse tel quel plutôt que d'inventer une heure.
void test_resolve_leaves_a_previous_boot_uptime_alone(void) {
    TimeSource ts;
    ts.sync(1755950400u, 100000);  // synchro à l'uptime 100 s
    TEST_ASSERT_EQUAL_UINT32(500, ts.resolve(500));
}

// Le driver de l'oxymètre renvoie -1 quand il n'a pas de lecture : ça devient
// 255 en uint8_t et ça remontait jusqu'au backend, qui refusait la mesure.
void test_sanitize_reading(void) {
    TEST_ASSERT_EQUAL_UINT8(0, sanitizeReading(-1, MAX_PLAUSIBLE_HR));    // pas de lecture
    TEST_ASSERT_EQUAL_UINT8(0, sanitizeReading(255, MAX_PLAUSIBLE_HR));   // le -1 déjà tronqué
    TEST_ASSERT_EQUAL_UINT8(0, sanitizeReading(101, MAX_PLAUSIBLE_SPO2)); // pas un pourcentage
    TEST_ASSERT_EQUAL_UINT8(72, sanitizeReading(72, MAX_PLAUSIBLE_HR));   // lecture normale
    TEST_ASSERT_EQUAL_UINT8(97, sanitizeReading(97, MAX_PLAUSIBLE_SPO2));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_sanity);
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_encode_is_little_endian);
    RUN_TEST(test_zero_values_survive);
    RUN_TEST(test_history_packet_framing);
    RUN_TEST(test_history_end_packet);
    RUN_TEST(test_full_packet_fits_in_mtu);

    RUN_TEST(test_ring_push_and_read_order);
    RUN_TEST(test_ring_release_only_after_ack);
    RUN_TEST(test_ring_wraps_and_counts_dropped);
    RUN_TEST(test_ring_restore_rejects_garbage);

    RUN_TEST(test_time_uptime_before_sync);
    RUN_TEST(test_time_after_sync);
    RUN_TEST(test_resolve_gives_back_the_real_epoch);
    RUN_TEST(test_resolve_leaves_a_real_epoch_alone);
    RUN_TEST(test_resolve_without_sync_returns_input);
    RUN_TEST(test_resolve_leaves_a_previous_boot_uptime_alone);
    RUN_TEST(test_time_survives_millis_wrap);
    RUN_TEST(test_sanitize_reading);

    return UNITY_END();
}
