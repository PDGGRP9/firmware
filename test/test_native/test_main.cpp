#include <unity.h>

#include <math.h>

#include "logic/measurement.h"
#include "logic/ring_index.h"
#include "logic/step_detector.h"
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

// Android reads these bytes in this exact order: if this test breaks, the app
// will display nonsense.
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
    // hr=0 / spo2=0 = "no reading", ts=0 = "time unknown". These cases go
    // through the same path and must not be filtered out.
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
    // The sequence number goes out little-endian like the rest of the protocol:
    // the app reads it back as is for the ACK.
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
    // This packet is never ACKed: no sequence number.
    TEST_ASSERT_EQUAL_UINT8(0, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[3]);
}

// The negotiated BLE MTU is 185: a full packet must fit in it, otherwise
// NimBLE silently truncates the notification.
void test_full_packet_fits_in_mtu(void) {
    const size_t batch = 20;
    size_t maxLen = HISTORY_HEADER_SIZE + batch * MEASUREMENT_SIZE;
    TEST_ASSERT_EQUAL_UINT32(164, maxLen);
    TEST_ASSERT_TRUE(maxLen <= 185 - 3);  // 3 bytes of ATT header
}

// --- RingIndex --------------------------------------------------------------

void test_ring_push_and_read_order(void) {
    RingIndex ring(4);
    TEST_ASSERT_EQUAL_UINT32(0, ring.push());
    TEST_ASSERT_EQUAL_UINT32(1, ring.push());
    TEST_ASSERT_EQUAL_UINT32(2, ring.push());

    TEST_ASSERT_EQUAL_UINT32(3, ring.count());
    TEST_ASSERT_EQUAL_UINT32(0, ring.slotAt(0));  // the oldest one
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

    // Releasing more than we have must not go negative.
    ring.release(99);
    TEST_ASSERT_EQUAL_UINT32(0, ring.count());
}

void test_ring_wraps_and_counts_dropped(void) {
    RingIndex ring(3);
    ring.push();  // slot 0
    ring.push();  // slot 1
    ring.push();  // slot 2 -> full
    TEST_ASSERT_TRUE(ring.isFull());
    TEST_ASSERT_EQUAL_UINT32(0, ring.dropped());

    uint32_t slot = ring.push();  // overwrites the oldest one
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

    // Blank or corrupted NVS: start empty instead of reading outside the file.
    ring.restore(500, 10);
    TEST_ASSERT_EQUAL_UINT32(0, ring.head());
    TEST_ASSERT_EQUAL_UINT32(0, ring.count());
}

// --- TimeSource -------------------------------------------------------------

// Before the sync we return the uptime in seconds, not 0: otherwise every
// backlog measurement shares the same ts and the app's dedup keeps only one.
void test_time_uptime_before_sync(void) {
    TimeSource ts;
    TEST_ASSERT_FALSE(ts.isSynced());
    TEST_ASSERT_EQUAL_UINT32(123, ts.now(123456));
    // Below TS_EPOCH_MIN: that is what tells it apart from a real epoch.
    TEST_ASSERT_TRUE(ts.now(123456) < TS_EPOCH_MIN);
}

void test_time_after_sync(void) {
    TimeSource ts;
    ts.sync(1755950400u, 10000);
    TEST_ASSERT_TRUE(ts.isSynced());
    TEST_ASSERT_EQUAL_UINT32(1755950400u, ts.now(10000));
    TEST_ASSERT_EQUAL_UINT32(1755950405u, ts.now(15000));
}

// millis() goes back to 0 after ~49 days: the uint32_t subtraction must still
// give the right delta.
void test_time_survives_millis_wrap(void) {
    TimeSource ts;
    uint32_t justBeforeWrap = 0xFFFFF000u;
    ts.sync(1000, justBeforeWrap);
    uint32_t afterWrap = justBeforeWrap + 5000;  // wraps
    TEST_ASSERT_EQUAL_UINT32(1005, ts.now(afterWrap));
}


// A measurement taken before the sync carries its uptime. Once the time is
// known, resolve() must recover its epoch by walking back from the sync point.
void test_resolve_gives_back_the_real_epoch(void) {
    TimeSource ts;
    // Sync at uptime 100 s (millis = 100000) with epoch 1755950400.
    ts.sync(1755950400u, 100000);

    // Measurement taken at uptime 40 s, i.e. 60 s before the sync.
    TEST_ASSERT_EQUAL_UINT32(1755950340u, ts.resolve(40));
    // Measurement taken at the very instant of the sync.
    TEST_ASSERT_EQUAL_UINT32(1755950400u, ts.resolve(100));
}

// A ts that is already an epoch must not be touched.
void test_resolve_leaves_a_real_epoch_alone(void) {
    TimeSource ts;
    ts.sync(1755950400u, 100000);
    TEST_ASSERT_EQUAL_UINT32(1755950405u, ts.resolve(1755950405u));
    TEST_ASSERT_EQUAL_UINT32(TS_EPOCH_MIN, ts.resolve(TS_EPOCH_MIN));
}

// Without a sync we have no base: the uptime comes out unchanged.
void test_resolve_without_sync_returns_input(void) {
    TimeSource ts;
    TEST_ASSERT_EQUAL_UINT32(40, ts.resolve(40));
}

// Known limitation, deliberately not handled: an uptime later than the sync
// point comes from a previous boot (reboot with a non-empty backlog). Its base
// is gone, so we leave it as is rather than invent a time.
void test_resolve_leaves_a_previous_boot_uptime_alone(void) {
    TimeSource ts;
    ts.sync(1755950400u, 100000);  // sync at uptime 100 s
    TEST_ASSERT_EQUAL_UINT32(500, ts.resolve(500));
}

// The app now sends the local UTC offset with the epoch; localDayNumber() uses it
// so the firmware's daily step reset lands on *local* midnight, not UTC midnight.
void test_local_day_number_rolls_over_at_local_midnight(void) {
    TimeSource ts;
    // 2026-01-01 23:30 UTC, local offset +02:00 -> it is already 01:30 on Jan 2 locally.
    const uint32_t jan1_2330_utc = 1767310200u;
    ts.sync(jan1_2330_utc, 7200, 0);

    int32_t dayAtSync = ts.localDayNumber(0);
    // 40 minutes later (00:10 UTC / 02:10 local) still the same local day.
    TEST_ASSERT_EQUAL_INT32(dayAtSync, ts.localDayNumber(40u * 60u * 1000u));

    // A UTC-midnight reset would have fired at +30 min; a local one must not.
    TEST_ASSERT_EQUAL_INT32(dayAtSync, ts.localDayNumber(31u * 60u * 1000u));

    // ~23 h later we cross the next local midnight.
    TEST_ASSERT_EQUAL_INT32(dayAtSync + 1, ts.localDayNumber(23u * 60u * 60u * 1000u));
}

void test_local_day_number_is_zero_before_sync(void) {
    TimeSource ts;
    TEST_ASSERT_EQUAL_INT32(0, ts.localDayNumber(123456));
}

// The oximeter driver returns -1 when it has no reading: that becomes 255 in a
// uint8_t and used to reach the backend, which rejected the measurement.
void test_sanitize_reading(void) {
    TEST_ASSERT_EQUAL_UINT8(0, sanitizeReading(-1, MAX_PLAUSIBLE_HR));    // no reading
    TEST_ASSERT_EQUAL_UINT8(0, sanitizeReading(255, MAX_PLAUSIBLE_HR));   // the -1 already truncated
    TEST_ASSERT_EQUAL_UINT8(0, sanitizeReading(101, MAX_PLAUSIBLE_SPO2)); // not a percentage
    TEST_ASSERT_EQUAL_UINT8(72, sanitizeReading(72, MAX_PLAUSIBLE_HR));   // normal reading
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
    RUN_TEST(test_local_day_number_rolls_over_at_local_midnight);
    RUN_TEST(test_local_day_number_is_zero_before_sync);

    RUN_TEST(test_sanitize_reading);

    return UNITY_END();
}
