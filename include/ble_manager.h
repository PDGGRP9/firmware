#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>

#include "logic/time_source.h"
#include "storage.h"

// Where the bracelet stands with the app (see the state diagram in the README).
//   SYNC_IDLE     : no paired app, or the app has not said START yet.
//   SYNC_WAIT_ACK : a history packet was sent, waiting for its ACK.
//                   Nothing is dropped until it arrives.
//   SYNC_LIVE     : backlog empty, measurements are streamed as they come, no ACK.
enum SyncState { SYNC_IDLE, SYNC_WAIT_ACK, SYNC_LIVE };

// BLE side of the bracelet: advertising, pairing, and the sync protocol that
// drains the backlog then streams live measurements.
class BLEManager {
private:
    NimBLEServer*         pServer;
    NimBLECharacteristic* pCharData;
    NimBLECharacteristic* pCharHistory;
    NimBLECharacteristic* pCharSyncCtrl;
    NimBLECharacteristic* pCharTime;
    bool deviceConnected;
    bool advertisingActive;

    // Storage and time live in main.cpp: we only use them.
    Storage*    storage_;
    TimeSource* time_;

    SyncState syncState;
    uint8_t  lastBatchCount;    // size of the packet waiting for an ACK
    // Sequence number of the current packet. It goes out in the HISTORY header
    // and the app echoes it in its ACK, which lets us reject the ACK of an
    // already-acked packet. Only equality is tested, so the wrap at 65535 is harmless.
    uint16_t batchSeq;
    uint32_t lastBatchSentMs;
    uint32_t lastStateLogMs;

    // Written from the BLE task, read from loop(): see tick().
    volatile uint8_t  pendingCmd;      // 0 = nothing pending
    volatile uint16_t pendingAckSeq;   // sequence carried by the last ACK received
    volatile uint32_t pendingEpoch;
    volatile int32_t  pendingOffset;   // local UTC offset in seconds (0 if the app didn't send one)
    volatile bool     hasPendingTime;
    volatile bool     pendingFlush;
    uint16_t connHandle;   // to query the MTU negotiated with this phone

    void sendNextBatch();
    // The one line that says everything: who is connected, who waits for what,
    // what is left in storage. `reason` = what triggered it.
    void logState(const char* reason);

public:
    BLEManager();
    ~BLEManager();

    // storage and time must outlive the BLEManager.
    bool initialize(Storage* storage, TimeSource* time);
    bool startAdvertising();
    bool stopAdvertising();

    bool isConnected() const { return deviceConnected; }
    bool isLive() const { return syncState == SYNC_LIVE; }

    void sendSensorData(uint8_t hr, uint8_t spo2, uint32_t steps);

    // Sends one measurement live. false = it did not go out, the caller must
    // store it in flash (it becomes backlog).
    bool sendLive(const Measurement& m);

    // Call it on every loop(): this is where the protocol moves forward.
    // NimBLE callbacks run on the BLE host task and cannot write flash (way too
    // slow): they only record the command received, tick() handles it.
    void tick();

    // Called from the NimBLE callbacks - they only memorize.
    // `seq` is only meaningful for SYNC_CMD_ACK; 0 for START and STOP.
    void onSyncCommand(uint8_t cmd, uint16_t seq);
    // `offsetSeconds` = local UTC offset the app sent alongside the epoch (0 when the app
    // only wrote the legacy 4-byte payload). The bracelet needs it to reset the daily step
    // counter at local midnight.
    void onTimeWrite(uint32_t epoch, int32_t offsetSeconds);

    void handleConnect();
    void handleDisconnect();
    void handleAuthComplete(bool success, uint16_t handle);
    uint32_t getPassKey() const;
};

#endif