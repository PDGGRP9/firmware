#include "ble_manager.h"
#include "config.h"
#include <Arduino.h>

// ==================== GLOBAL INSTANCE FOR THE CALLBACKS ====================
static BLEManager* g_pBLEManager = nullptr;

// ==================== SERVER CALLBACKS ====================
class MyServerCallbacks : public NimBLEServerCallbacks {

  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    // Already-paired phone: NimBLE re-encrypts the link on its own from the
    // stored key (LTK), without replaying a pairing. So onAuthenticationComplete
    // is never called and, without this check, the bracelet would stay at conn=0
    // forever and ignore the START (log of 2026-08-31: the phone ends up
    // dropping the link with HCI reason 531).
    if (connInfo.isEncrypted()) {
      Serial.println("[BLE] Link encrypted right away (peer already paired)");
      if (g_pBLEManager) {
        g_pBLEManager->handleAuthComplete(true, connInfo.getConnHandle());
      }
      return;
    }
    Serial.println("[BLE] Physical link up (waiting for authentication)");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.print("[BLE] Disconnected, reason: ");
    Serial.println(reason);
    if (g_pBLEManager) {
      g_pBLEManager->handleDisconnect();
    }
  }

  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = g_pBLEManager ? g_pBLEManager->getPassKey() : BLE_STATIC_PASSKEY;
    Serial.println("\n==============================");
    Serial.print("   BLE PAIRING CODE: ");
    Serial.println(passkey);
    Serial.println("==============================\n");
    return passkey;
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    bool success = connInfo.isEncrypted();
    if (g_pBLEManager) {
      g_pBLEManager->handleAuthComplete(success, connInfo.getConnHandle());
    }
  }
};

// ==================== CHARACTERISTIC CALLBACKS ====================
// Careful: this runs on the BLE host task. Handling the command would write to
// flash (LittleFS + NVS), way too slow for that task. So we only memorize, and
// BLEManager::tick() does the work from loop().
class MyControlCallbacks : public NimBLECharacteristicCallbacks {

  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    if (!g_pBLEManager) return;
    std::string value = pChar->getValue();

    // Getting a write on a WRITE_ENC characteristic proves the link is
    // encrypted: the BLE stack would have refused it otherwise. It can happen
    // BEFORE NimBLE tells us authentication is done (that is what happened with
    // the TIME received at conn=0). So we treat the client as connected right
    // now, else the command that follows would be handled for nothing.
    if (!g_pBLEManager->isConnected() && connInfo.isEncrypted()) {
      g_pBLEManager->handleAuthComplete(true, connInfo.getConnHandle());
    }

    // Always trace: writes are rare (time + sync commands), and this is the
    // only way to know whether the app really reaches the bracelet when the
    // sync does not start.
    Serial.print("[BLE] Write received on ...");
    Serial.print(pChar->getUUID().toString().substr(4, 4).c_str());
    Serial.print(" (");
    Serial.print(value.size());
    Serial.println(" bytes)");

    if (pChar->getUUID().equals(NimBLEUUID(SYNC_CTRL_UUID))) {
      // START and STOP are 1 byte; the ACK is 3 and carries the number of the
      // packet it acknowledges: [0x02][seqLo][seqHi].
      if (value.size() != 1 && value.size() != 3) {
        Serial.println("[BLE] SYNC command with invalid size, ignored");
        return;
      }
      const uint8_t* c = (const uint8_t*)value.data();
      uint16_t seq = 0;
      if (value.size() == 3) {
        seq = (uint16_t)c[1] | ((uint16_t)c[2] << 8);
      }
      g_pBLEManager->onSyncCommand(c[0], seq);
    } else if (pChar->getUUID().equals(NimBLEUUID(TIME_UUID))) {
      // 4 bytes = epoch only (legacy). 8 bytes = epoch + local UTC offset (int32 LE),
      // needed to reset the daily step counter at *local* midnight.
      if (value.size() != 4 && value.size() != 8) {
        Serial.println("[BLE] TIME with invalid size, ignored");
        return;
      }
      const uint8_t* b = (const uint8_t*)value.data();
      uint32_t epoch = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
      int32_t offset = 0;
      if (value.size() == 8) {
        offset = (int32_t)((uint32_t)b[4] | ((uint32_t)b[5] << 8) |
                           ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24));
      }
      g_pBLEManager->onTimeWrite(epoch, offset);
    }
  }
};

// ==================== CONSTRUCTOR / DESTRUCTOR ====================
BLEManager::BLEManager()
  : pServer(nullptr), pCharData(nullptr),
    pCharHistory(nullptr), pCharSyncCtrl(nullptr), pCharTime(nullptr),
    deviceConnected(false), advertisingActive(false),
    storage_(nullptr), time_(nullptr),
    syncState(SYNC_IDLE), lastBatchCount(0), batchSeq(0),
    lastBatchSentMs(0), lastStateLogMs(0),
    pendingCmd(0), pendingAckSeq(0), pendingEpoch(0), pendingOffset(0), hasPendingTime(false),
    pendingFlush(false),
    connHandle(BLE_HS_CONN_HANDLE_NONE) {
  g_pBLEManager = this;
}

BLEManager::~BLEManager() {
  NimBLEDevice::deinit(true);
}

// ==================== INIT ====================
bool BLEManager::initialize(Storage* storage, TimeSource* time) {
  Serial.println("\n--- BLE Manager init ---");

  storage_ = storage;
  time_ = time;

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setMTU(185);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pCharData = pService->createCharacteristic(
      DATA_UUID,
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

  // Initial value: zeroed binary payload (4 bytes)
  uint8_t initialPayload[4] = {0, 0, 0, 0};
  pCharData->setValue(initialPayload, sizeof(initialPayload));

  // The three characteristics of the backlog catch-up. Everything is
  // encrypted: without pairing the app reads nothing and writes nothing.
  pCharHistory = pService->createCharacteristic(
      HISTORY_UUID,
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

  // WRITE_ENC is a PERMISSION (0x1000), not the WRITE property (0x0008):
  // without both, the characteristic is not declared writable and Android
  // refuses the write without even reporting an error.
  pCharSyncCtrl = pService->createCharacteristic(
      SYNC_CTRL_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);

  pCharTime = pService->createCharacteristic(
      TIME_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);

  // pCharData is already dereferenced just above: only the three new ones are
  // checked here.
  if (!pCharHistory || !pCharSyncCtrl || !pCharTime) {
    Serial.println("[FAIL] BLE - characteristic creation");
    return false;
  }

  MyControlCallbacks* pCtrl = new MyControlCallbacks();
  pCharSyncCtrl->setCallbacks(pCtrl);
  pCharTime->setCallbacks(pCtrl);


  Serial.println("[OK] BLE Manager initialized");
  return true;
}

// ==================== ADVERTISING ====================

bool BLEManager::startAdvertising() {
  if (advertisingActive) {
    return true;
  }

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

  // Main packet: just the flags + the service UUID
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06); // LE General Discoverable + BR/EDR Not Supported
  advData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  pAdvertising->setAdvertisementData(advData);

  // Scan response: the device name (separate packet, another 31 bytes)
  NimBLEAdvertisementData scanData;
  scanData.setName(BLE_DEVICE_NAME);
  pAdvertising->setScanResponseData(scanData);

  bool ok = pAdvertising->start();
  if (ok) {
    advertisingActive = true;
    Serial.println("[OK] BLE advertising started");
  } else {
    Serial.println("[FAIL] Advertising");
  }
  return ok;
}

// ==================== BINARY SEND ====================
void BLEManager::sendSensorData(uint8_t hr, uint8_t spo2, uint32_t steps) {
  if (!deviceConnected || !pCharData) return;

  // Binary payload: [BPM(1)][SpO2(1)][Steps_LSB(1)][Steps_MSB(1)]
  // Steps clamped to uint16_t (0-65535) to fit in 2 bytes, little-endian
  uint16_t steps16 = (steps > 65535) ? 65535 : (uint16_t)steps;

  uint8_t payload[4];
  payload[0] = hr;
  payload[1] = spo2;
  payload[2] = (uint8_t)((steps16 >> 8) & 0xFF); // MSB (big-endian)
  payload[3] = (uint8_t)(steps16 & 0xFF);        // LSB (little-endian)

  pCharData->setValue(payload, sizeof(payload));
  pCharData->notify();

  #ifdef DEBUG_BLE
  Serial.printf("[BLE BIN] HR=%u SpO2=%u Steps=%u | Bytes: %02X %02X %02X %02X\n",
                hr, spo2, steps16,
                payload[0], payload[1], payload[2], payload[3]);
  #endif
}

// ==================== INTERNAL CALLBACKS ====================
void BLEManager::handleConnect() {
  // We can come here twice for the same connection (link already encrypted,
  // then authentication done). Without this early return, the second pass would
  // reset syncState to IDLE while a drain may already be running.
  if (deviceConnected) return;
  deviceConnected = true;
  syncState = SYNC_IDLE;  // waiting for the app's START
  Serial.println("[BLE] Client connected and authenticated!");
  logState("connected");
}

void BLEManager::handleDisconnect() {
  deviceConnected = false;
  advertisingActive = false;
  syncState = SYNC_IDLE;
  // Without this, getPeerMTU() would query a dead handle on reconnection and
  // return 0 -> packets sized on an imaginary MTU (see sendNextBatch).
  connHandle = BLE_HS_CONN_HANDLE_NONE;
  // Nothing is dropped: the un-ACKed packet is sent again on reconnection.
  if (lastBatchCount > 0) {
    Serial.print("[SYNC] Link lost with ");
    Serial.print(lastBatchCount);
    Serial.println(" un-ACKed measurements -> they will be sent again");
    lastBatchCount = 0;
  }
  // The RAM buffer must go to flash, but not here: we are on the BLE host
  // task. tick() takes care of it on the next loop().
  pendingFlush = true;
  Serial.println("[BLE] Client disconnected");
  logState("disconnected");
  startAdvertising();
}

void BLEManager::handleAuthComplete(bool success, uint16_t handle) {
  if (success) {
    connHandle = handle;
    handleConnect();
  } else {
    Serial.println("[BLE] Authentication failed -> disconnecting");
    deviceConnected = false;
    if (pServer) {
      pServer->disconnect(handle);
    }
  }
}

uint32_t BLEManager::getPassKey() const {
  return BLE_STATIC_PASSKEY;
}

// ==================== STOP ADVERTISING ====================
bool BLEManager::stopAdvertising() {
  if (!advertisingActive) return true;
  bool ok = NimBLEDevice::getAdvertising()->stop();
  if (ok) {
    advertisingActive = false;
    Serial.println("[BLE] Advertising stopped");
  }
  return ok;
}

// ==================== COMMANDS RECEIVED (only memorized) ====================
void BLEManager::onSyncCommand(uint8_t cmd, uint16_t seq) {
  pendingCmd = cmd;
  pendingAckSeq = seq;
}

void BLEManager::onTimeWrite(uint32_t epoch, int32_t offsetSeconds) {
  pendingEpoch = epoch;
  pendingOffset = offsetSeconds;
  hasPendingTime = true;
}

// ==================== SYNC PROTOCOL (driven from loop) ====================
void BLEManager::tick() {
  uint32_t now = millis();

  // 1. The app gave us the time: the bracelet has no clock, without it every
  //    measurement would go out with ts = 0.
  if (hasPendingTime) {
    uint32_t epoch = pendingEpoch;
    int32_t offset = pendingOffset;
    hasPendingTime = false;
    time_->sync(epoch, offset, now);
    Serial.print("[BLE] Time received from the app: epoch=");
    Serial.print(epoch);
    Serial.print(" offset=");
    Serial.print(offset);
    Serial.println("s");
    logState("time-sync");
  }

  // 2. RAM buffer flush asked for by the disconnection (see handleDisconnect).
  if (pendingFlush) {
    pendingFlush = false;
    storage_->flush();
  }

  // 3. Pending sync command.
  uint8_t cmd = pendingCmd;
  if (cmd != 0) {
    // Link not ready yet: we KEEP the command instead of consuming it. Before,
    // sendNextBatch() returned silently and the START vanished without leaving
    // any trace in the logs.
    if (!deviceConnected) {
      Serial.println("[SYNC] Command received before the link was ready -> kept");
      return;
    }
    pendingCmd = 0;
    switch (cmd) {
      case SYNC_CMD_START:
        Serial.print("[SYNC] START received -> draining storage (");
        Serial.print(storage_->pending());
        Serial.println(" measurements pending)");
        batchSeq = 0;
        sendNextBatch();
        break;

      case SYNC_CMD_ACK:
        if (syncState != SYNC_WAIT_ACK) {
          // Typically the ACK of a packet already resent after a timeout:
          // harmless, we ignore it.
          Serial.println("[SYNC] ACK received while not waiting, ignored");
          break;
        }
        if (pendingAckSeq != batchSeq) {
          // ACK of an already-acked packet (duplicate, or arrived after a
          // resend). Without this check it would pass for the current packet's
          // ACK and drop measurements the app never received.
          Serial.printf("[SYNC] ACK #%u while waiting for #%u -> ignored\n",
                        (unsigned)pendingAckSeq, (unsigned)batchSeq);
          break;
        }
        Serial.print("[SYNC] ACK packet #");
        Serial.print(batchSeq);
        Serial.print(" -> dropping ");
        Serial.print(lastBatchCount);
        Serial.println(" measurements");
        storage_->confirm(lastBatchCount);
        lastBatchCount = 0;
        sendNextBatch();
        break;

      case SYNC_CMD_STOP:
        Serial.println("[SYNC] STOP received -> back to idle (nothing dropped)");
        syncState = SYNC_IDLE;
        lastBatchCount = 0;
        logState("stop");
        break;

      default:
        Serial.println("[SYNC] Unknown command, ignored");
        break;
    }
  }

  // 4. No ACK in time: we resend the same packet. Safe, nothing was dropped and
  //    the app dedups by ts.
  // millis() is read again here instead of the `now` from the top of tick():
  // sendNextBatch() may have run just above (step 3) and set lastBatchSentMs
  // AFTER `now`. The uint32_t subtraction would then underflow (~4.29 billion)
  // and every packet would be resent right after being sent.
  if (syncState == SYNC_WAIT_ACK && (millis() - lastBatchSentMs) >= SYNC_ACK_TIMEOUT) {
    Serial.print("[SYNC] No ACK -> resending packet #");
    Serial.print(batchSeq);
    Serial.println(" (nothing was dropped)");
    batchSeq--;  // sendNextBatch re-increments it: the number stays stable
    sendNextBatch();
  }

  // 5. We are LIVE but backlog piled up (a missed notify for instance): back to
  //    draining, as the README state diagram says.
  if (syncState == SYNC_LIVE && deviceConnected && storage_->pending() > 0) {
    Serial.print("[SYNC] ");
    Serial.print(storage_->pending());
    Serial.println(" measurements to drain while LIVE -> back to draining");
    sendNextBatch();
  }

  // 6. Heartbeat: even when nothing moves, we want to know where we stand.
  if ((now - lastStateLogMs) >= STATE_LOG_INTERVAL) {
    logState("heartbeat");
  }
}

void BLEManager::sendNextBatch() {
  if (!deviceConnected) {
    syncState = SYNC_IDLE;
    return;
  }

  // The MTU is negotiated by the phone: if it did not ask for more than the
  // default 23 bytes, a full packet would be silently truncated. So we size the
  // packet to what really fits.
  uint8_t maxBatch = HISTORY_BATCH;
  uint16_t mtu = pServer->getPeerMTU(connHandle);
  // Unknown MTU (0): fall back to the BLE minimum, 23 bytes. Falling back to
  // the best case would silently truncate the packet.
  if (mtu < 23) mtu = 23;
  {
    uint16_t fits = (mtu - 3 - HISTORY_HEADER_SIZE) / MEASUREMENT_SIZE;
    if (fits < 1) fits = 1;
    if (fits < maxBatch) {
      maxBatch = (uint8_t)fits;
      Serial.print("[SYNC] MTU=");
      Serial.print(mtu);
      Serial.print(" -> packets reduced to ");
      Serial.print(maxBatch);
      Serial.println(" measurements");
    }
  }

  Measurement batch[HISTORY_BATCH];
  uint8_t n = storage_->readBatch(batch, maxBatch);

  // Measurements taken before the sync carry their uptime: now that we have the
  // time, we give them their epoch back. Only in the outgoing packet - flash
  // keeps the uptime, rewriting the ring would wear it for nothing, and a resend
  // after a timeout redoes the exact same computation.
  if (time_) {
    for (uint8_t i = 0; i < n; ++i) {
      batch[i].ts = time_->resolve(batch[i].ts);
    }
  }

  if (n == 0) {
    // Storage empty: the app can go live.
    uint8_t packet[HISTORY_HEADER_SIZE];
    size_t len = buildHistoryEndPacket(packet);
    pCharHistory->setValue(packet, len);
    pCharHistory->notify();

    syncState = SYNC_LIVE;
    lastBatchCount = 0;
    Serial.println("[SYNC] Storage empty -> switching to LIVE");
    logState("storage-empty");
    return;
  }

  // Incremented BEFORE building: this is the number that goes in the header and
  // that the app must echo. The timeout resend does batchSeq-- just before
  // calling here, so a resent packet keeps its number.
  batchSeq++;

  uint8_t packet[HISTORY_HEADER_SIZE + HISTORY_BATCH * MEASUREMENT_SIZE];
  size_t len = buildHistoryPacket(batch, n, batchSeq, packet);
  pCharHistory->setValue(packet, len);
  pCharHistory->notify();

  lastBatchCount = n;
  lastBatchSentMs = millis();
  syncState = SYNC_WAIT_ACK;

  Serial.print("[SYNC] Packet #");
  Serial.print(batchSeq);
  Serial.print(" sent (");
  Serial.print(n);
  Serial.print(" measurements, ");
  Serial.print((unsigned)len);
  Serial.println(" bytes) -> waiting for ACK");
  logState("packet-sent");
}

// ==================== LIVE SEND (same 8-byte record) ====================
bool BLEManager::sendLive(const Measurement& m) {
  if (!deviceConnected || syncState != SYNC_LIVE || !pCharData) return false;

  uint8_t payload[MEASUREMENT_SIZE];
  encodeMeasurement(m, payload);
  pCharData->setValue(payload, MEASUREMENT_SIZE);
  return pCharData->notify();
}

// ==================== THE LINE THAT SAYS IT ALL ====================
// Printed on every state transition and every STATE_LOG_INTERVAL ms. This is the
// one to paste in a ticket: who is connected, who waits for what, and how many
// measurements are still stored.
void BLEManager::logState(const char* reason) {
  lastStateLogMs = millis();

  const char* stateName = "IDLE";
  if (syncState == SYNC_WAIT_ACK)  stateName = "WAIT_ACK";
  else if (syncState == SYNC_LIVE) stateName = "LIVE";

  Serial.printf("[STATE] conn=%d sync=%s batch=%lu wait=%u ram=%lu flash=%lu "
                "pend=%lu drop=%lu ts=%lu (%s)\n",
                deviceConnected ? 1 : 0, stateName, (unsigned long)batchSeq,
                lastBatchCount,
                (unsigned long)(storage_ ? storage_->ramPending() : 0),
                (unsigned long)(storage_ ? storage_->pendingInFlash() : 0),
                (unsigned long)(storage_ ? storage_->pending() : 0),
                (unsigned long)(storage_ ? storage_->dropped() : 0),
                (unsigned long)(time_ ? time_->now(millis()) : 0), reason);
}
