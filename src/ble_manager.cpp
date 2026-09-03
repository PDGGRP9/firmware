#include "ble_manager.h"
#include "config.h"
#include <Arduino.h>

// ==================== GLOBAL INSTANCE FOR THE CALLBACKS ====================
static BLEManager* g_pBLEManager = nullptr;

// ==================== SERVER CALLBACKS ====================
class MyServerCallbacks : public NimBLEServerCallbacks {

  // Called when the physical link comes up, possibly already encrypted (reconnect of a known peer).
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    // A re-encrypted link from stored keys skips onAuthenticationComplete,
    // so we must detect that case here or the bracelet never sees the client as connected.
    if (connInfo.isEncrypted()) {
      Serial.println("[BLE] Link encrypted right away (peer already paired)");
      if (g_pBLEManager) {
        g_pBLEManager->handleAuthComplete(true, connInfo.getConnHandle());
      }
      return;
    }
    Serial.println("[BLE] Physical link up (waiting for authentication)");
  }

  // Called on disconnection, any reason.
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.print("[BLE] Disconnected, reason: ");
    Serial.println(reason);
    if (g_pBLEManager) {
      g_pBLEManager->handleDisconnect();
    }
  }

  // Called by NimBLE to get the passkey to show for pairing.
  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = g_pBLEManager ? g_pBLEManager->getPassKey() : BLE_STATIC_PASSKEY;
    Serial.println("\n==============================");
    Serial.print("   BLE PAIRING CODE: ");
    Serial.println(passkey);
    Serial.println("==============================\n");
    return passkey;
  }

  // Called once pairing/authentication finishes (success or failure).
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    bool success = connInfo.isEncrypted();
    if (g_pBLEManager) {
      g_pBLEManager->handleAuthComplete(success, connInfo.getConnHandle());
    }
  }
};

// ==================== CHARACTERISTIC CALLBACKS ====================
// Runs on the BLE host task: only memorize the command here, tick() does the
// real (slower) work from loop().
class MyControlCallbacks : public NimBLECharacteristicCallbacks {

  // Called when the app writes to SYNC_CTRL or TIME characteristics.
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    if (!g_pBLEManager) return;
    std::string value = pChar->getValue();

    // A successful encrypted write proves the link is ready, even if
    // NimBLE hasn't confirmed authentication yet.
    if (!g_pBLEManager->isConnected() && connInfo.isEncrypted()) {
      g_pBLEManager->handleAuthComplete(true, connInfo.getConnHandle());
    }

    // Trace every write: rare events (time + sync commands), useful to debug.
    Serial.print("[BLE] Write received on ...");
    Serial.print(pChar->getUUID().toString().substr(4, 4).c_str());
    Serial.print(" (");
    Serial.print(value.size());
    Serial.println(" bytes)");

    if (pChar->getUUID().equals(NimBLEUUID(SYNC_CTRL_UUID))) {
      // START/STOP = 1 byte. ACK = 3 bytes: [0x02][seqLo][seqHi].
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
      // 4 bytes = epoch only (legacy). 8 bytes = epoch + UTC offset (for local midnight).
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
// Create the BLE server, service and characteristics, configure security.
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

  // Backlog catch-up characteristics, all encrypted.
  pCharHistory = pService->createCharacteristic(
      HISTORY_UUID,
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

  // WRITE_ENC is a permission, not the WRITE property: both are needed or
  // Android silently refuses the write.
  pCharSyncCtrl = pService->createCharacteristic(
      SYNC_CTRL_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);

  pCharTime = pService->createCharacteristic(
      TIME_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);

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

// Start advertising the service UUID + device name.
bool BLEManager::startAdvertising() {
  if (advertisingActive) {
    return true;
  }

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

  // Main packet: flags + service UUID
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06); // LE General Discoverable + BR/EDR Not Supported
  advData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  pAdvertising->setAdvertisementData(advData);

  // Scan response: device name (separate packet)
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
// Notify a single live measurement in the legacy 4-byte format.
void BLEManager::sendSensorData(uint8_t hr, uint8_t spo2, uint32_t steps) {
  if (!deviceConnected || !pCharData) return;

  // Payload: [BPM(1)][SpO2(1)][Steps_LSB(1)][Steps_MSB(1)], steps clamped to uint16_t
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
// Mark the client as connected, guarding against a duplicate call.
void BLEManager::handleConnect() {
  // Can be called twice for the same connection (encrypted then authenticated).
  if (deviceConnected) return;
  deviceConnected = true;
  syncState = SYNC_IDLE;  // waiting for the app's START
  Serial.println("[BLE] Client connected and authenticated!");
  logState("connected");
}

// Reset connection state and restart advertising after a disconnect.
void BLEManager::handleDisconnect() {
  deviceConnected = false;
  advertisingActive = false;
  syncState = SYNC_IDLE;
  // Avoids querying a dead handle for MTU on reconnection.
  connHandle = BLE_HS_CONN_HANDLE_NONE;
  // Nothing is dropped: the un-ACKed packet will be resent on reconnection.
  if (lastBatchCount > 0) {
    Serial.print("[SYNC] Link lost with ");
    Serial.print(lastBatchCount);
    Serial.println(" un-ACKed measurements -> they will be sent again");
    lastBatchCount = 0;
  }
  // Flush deferred to tick() since we're on the BLE host task here.
  pendingFlush = true;
  Serial.println("[BLE] Client disconnected");
  logState("disconnected");
  startAdvertising();
}

// Confirm or reject the pending authentication.
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

// Return the fixed pairing passkey.
uint32_t BLEManager::getPassKey() const {
  return BLE_STATIC_PASSKEY;
}

// ==================== STOP ADVERTISING ====================
// Stop advertising (e.g. once a client is connected).
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
// Store the sync command for processing in tick().
void BLEManager::onSyncCommand(uint8_t cmd, uint16_t seq) {
  pendingCmd = cmd;
  pendingAckSeq = seq;
}

// Store the time write for processing in tick().
void BLEManager::onTimeWrite(uint32_t epoch, int32_t offsetSeconds) {
  pendingEpoch = epoch;
  pendingOffset = offsetSeconds;
  hasPendingTime = true;
}

// ==================== SYNC PROTOCOL (driven from loop) ====================
// Main state machine: apply pending time/commands, resend on timeout, drain backlog.
void BLEManager::tick() {
  uint32_t now = millis();

  // 1. Apply the time sent by the app (bracelet has no clock).
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

  // 2. RAM buffer flush requested by handleDisconnect.
  if (pendingFlush) {
    pendingFlush = false;
    storage_->flush();
  }

  // 3. Process the pending sync command, if any.
  uint8_t cmd = pendingCmd;
  if (cmd != 0) {
    // Keep the command if the link isn't ready yet, instead of dropping it silently.
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
          // Likely the ACK of a packet already resent after a timeout: harmless.
          Serial.println("[SYNC] ACK received while not waiting, ignored");
          break;
        }
        if (pendingAckSeq != batchSeq) {
          // Duplicate or late ACK: ignore, or we'd drop measurements the app never got.
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

  // 4. No ACK in time: resend the same packet (nothing was dropped, app dedups by ts).
  // millis() read again here since sendNextBatch() above may have updated lastBatchSentMs
  // after `now`, which would underflow the subtraction.
  if (syncState == SYNC_WAIT_ACK && (millis() - lastBatchSentMs) >= SYNC_ACK_TIMEOUT) {
    Serial.print("[SYNC] No ACK -> resending packet #");
    Serial.print(batchSeq);
    Serial.println(" (nothing was dropped)");
    batchSeq--;  // sendNextBatch re-increments it: the number stays stable
    sendNextBatch();
  }

  // 5. LIVE but backlog piled up again: resume draining.
  if (syncState == SYNC_LIVE && deviceConnected && storage_->pending() > 0) {
    Serial.print("[SYNC] ");
    Serial.print(storage_->pending());
    Serial.println(" measurements to drain while LIVE -> back to draining");
    sendNextBatch();
  }

  // 6. Periodic heartbeat log.
  if ((now - lastStateLogMs) >= STATE_LOG_INTERVAL) {
    logState("heartbeat");
  }
}

// Build and send the next backlog packet, or switch to LIVE if storage is empty.
void BLEManager::sendNextBatch() {
  if (!deviceConnected) {
    syncState = SYNC_IDLE;
    return;
  }

  // Size the packet to the negotiated MTU, or it would be silently truncated.
  uint8_t maxBatch = HISTORY_BATCH;
  uint16_t mtu = pServer->getPeerMTU(connHandle);
  // Unknown MTU (0): fall back to the BLE minimum (23 bytes).
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

  // Restore the real epoch on old measurements taken before time sync.
  // Only in the outgoing packet: flash keeps the uptime value.
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

  // Incremented before building: this is the number the app must ACK.
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
// Notify a single measurement in LIVE mode.
bool BLEManager::sendLive(const Measurement& m) {
  if (!deviceConnected || syncState != SYNC_LIVE || !pCharData) return false;

  uint8_t payload[MEASUREMENT_SIZE];
  encodeMeasurement(m, payload);
  pCharData->setValue(payload, MEASUREMENT_SIZE);
  return pCharData->notify();
}

// ==================== THE LINE THAT SAYS IT ALL ====================
// Log the full sync state: connection, sync mode, batch, storage counts, time.
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