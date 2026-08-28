#include "ble_manager.h"
#include "config.h"
#include <Arduino.h>

// ==================== INSTANCE GLOBALE POUR LES CALLBACKS ====================
static BLEManager* g_pBLEManager = nullptr;

// ==================== CALLBACKS SERVEUR ====================
class MyServerCallbacks : public NimBLEServerCallbacks {

  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.println("[BLE] Connexion physique établie (en attente d'authentification)");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.print("[BLE] Déconnexion, raison: ");
    Serial.println(reason);
    if (g_pBLEManager) {
      g_pBLEManager->handleDisconnect();
    }
  }

  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = g_pBLEManager ? g_pBLEManager->getPassKey() : BLE_STATIC_PASSKEY;
    Serial.println("\n==============================");
    Serial.print("   CODE D'APPAIRAGE BLE : ");
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

// ==================== CALLBACKS CARACTERISTIQUES ====================
// Attention : on tourne ici sur la tâche hôte BLE. Traiter la commande
// écrirait en flash (LittleFS + NVS), bien trop long pour cette tâche. On ne
// fait donc que mémoriser, et BLEManager::tick() fait le travail depuis loop().
class MyControlCallbacks : public NimBLECharacteristicCallbacks {

  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    if (!g_pBLEManager) return;
    std::string value = pChar->getValue();

    // Trace systématique : les écritures sont rares (heure + commandes de
    // synchro), et c'est le seul moyen de savoir si l'app arrive bien à parler
    // au bracelet quand la synchro ne démarre pas.
    Serial.print("[BLE] Write reçu sur ...");
    Serial.print(pChar->getUUID().toString().substr(4, 4).c_str());
    Serial.print(" (");
    Serial.print(value.size());
    Serial.println(" octets)");

    if (pChar->getUUID().equals(NimBLEUUID(SYNC_CTRL_UUID))) {
      if (value.size() != 1) {
        Serial.println("[BLE] Commande SYNC de taille invalide, ignorée");
        return;
      }
      g_pBLEManager->onSyncCommand((uint8_t)value[0]);
    } else if (pChar->getUUID().equals(NimBLEUUID(TIME_UUID))) {
      if (value.size() != 4) {
        Serial.println("[BLE] TIME de taille invalide, ignoré");
        return;
      }
      const uint8_t* b = (const uint8_t*)value.data();
      uint32_t epoch = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
      g_pBLEManager->onTimeWrite(epoch);
    }
  }
};

// ==================== CONSTRUCTEUR / DESTRUCTEUR ====================
BLEManager::BLEManager()
  : pServer(nullptr), pCharData(nullptr),
    pCharHistory(nullptr), pCharSyncCtrl(nullptr), pCharTime(nullptr),
    deviceConnected(false), advertisingActive(false),
    storage_(nullptr), time_(nullptr),
    syncState(SYNC_IDLE), lastBatchCount(0), batchSeq(0),
    lastBatchSentMs(0), lastStateLogMs(0),
    pendingCmd(0), pendingEpoch(0), hasPendingTime(false), pendingFlush(false),
    connHandle(BLE_HS_CONN_HANDLE_NONE) {
  g_pBLEManager = this;
}

BLEManager::~BLEManager() {
  NimBLEDevice::deinit(true);
}

// ==================== INITIALISATION ====================
bool BLEManager::initialize(Storage* storage, TimeSource* time) {
  Serial.println("\n--- Initialisation BLE Manager ---");

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

  // Valeur initiale : payload binaire à zéro (4 bytes)
  uint8_t initialPayload[4] = {0, 0, 0, 0};
  pCharData->setValue(initialPayload, sizeof(initialPayload));

  // Les trois caractéristiques du rattrapage de backlog. Tout est chiffré :
  // sans appairage l'app ne lit rien et n'écrit rien.
  pCharHistory = pService->createCharacteristic(
      HISTORY_UUID,
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

  // WRITE_ENC est une PERMISSION (0x1000), pas la propriété WRITE (0x0008) :
  // sans les deux, la caractéristique n'est pas déclarée inscriptible et
  // Android refuse l'écriture sans même remonter d'erreur.
  pCharSyncCtrl = pService->createCharacteristic(
      SYNC_CTRL_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);

  pCharTime = pService->createCharacteristic(
      TIME_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);

  // pCharData est déjà déréférencée juste au-dessus : on ne teste ici que les
  // trois nouvelles.
  if (!pCharHistory || !pCharSyncCtrl || !pCharTime) {
    Serial.println("[FAIL] BLE - création des caractéristiques");
    return false;
  }

  MyControlCallbacks* pCtrl = new MyControlCallbacks();
  pCharSyncCtrl->setCallbacks(pCtrl);
  pCharTime->setCallbacks(pCtrl);

  pService->start();

  Serial.println("[OK] BLE Manager initialisé");
  return true;
}

// ==================== ADVERTISING ====================

bool BLEManager::startAdvertising() {
  if (advertisingActive) {
    return true;
  }

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

  // Paquet principal : juste les flags + UUID de service
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06); // LE General Discoverable + BR/EDR Not Supported
  advData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  pAdvertising->setAdvertisementData(advData);

  // Scan response : le nom du device (paquet séparé, 31 octets aussi dispo)
  NimBLEAdvertisementData scanData;
  scanData.setName(BLE_DEVICE_NAME);
  pAdvertising->setScanResponseData(scanData);

  bool ok = pAdvertising->start();
  if (ok) {
    advertisingActive = true;
    Serial.println("[OK] BLE Advertising démarré");
  } else {
    Serial.println("[FAIL] Advertising");
  }
  return ok;
}

// ==================== ENVOI BINAIRE ====================
void BLEManager::sendSensorData(uint8_t hr, uint8_t spo2, uint32_t steps) {
  if (!deviceConnected || !pCharData) return;

  // Payload binaire : [BPM(1)][SpO2(1)][Steps_LSB(1)][Steps_MSB(1)]
  // Steps limité à uint16_t (0-65535) pour tenir sur 2 bytes, little-endian
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

// ==================== CALLBACKS INTERNES ====================
void BLEManager::handleConnect() {
  deviceConnected = true;
  syncState = SYNC_IDLE;  // on attend le START de l'app
  Serial.println("[BLE] Client connecté et authentifié !");
  logState("connecte");
}

void BLEManager::handleDisconnect() {
  deviceConnected = false;
  advertisingActive = false;
  syncState = SYNC_IDLE;
  // Rien n'est purgé : le paquet non acquitté repartira à la reconnexion.
  if (lastBatchCount > 0) {
    Serial.print("[SYNC] Lien perdu avec ");
    Serial.print(lastBatchCount);
    Serial.println(" mesures non acquittées -> elles repartiront");
    lastBatchCount = 0;
  }
  // Le tampon RAM doit partir en flash, mais pas ici : on est sur la tâche
  // hôte BLE. tick() s'en charge au prochain tour de loop().
  pendingFlush = true;
  Serial.println("[BLE] Client déconnecté");
  logState("deconnecte");
  startAdvertising();
}

void BLEManager::handleAuthComplete(bool success, uint16_t handle) {
  if (success) {
    connHandle = handle;
    handleConnect();
  } else {
    Serial.println("[BLE] Échec de l'authentification -> déconnexion");
    deviceConnected = false;
    if (pServer) {
      pServer->disconnect(0);
    }
  }
}

uint32_t BLEManager::getPassKey() const {
  return BLE_STATIC_PASSKEY;
}

// ==================== ARRET DE L'ADVERTISING ====================
bool BLEManager::stopAdvertising() {
  if (!advertisingActive) return true;
  bool ok = NimBLEDevice::getAdvertising()->stop();
  if (ok) {
    advertisingActive = false;
    Serial.println("[BLE] Advertising arrêté");
  }
  return ok;
}

// ==================== COMMANDES RECUES (juste memorisees) ====================
void BLEManager::onSyncCommand(uint8_t cmd) {
  pendingCmd = cmd;
}

void BLEManager::onTimeWrite(uint32_t epoch) {
  pendingEpoch = epoch;
  hasPendingTime = true;
}

// ==================== PROTOCOLE DE SYNCHRO (depuis loop) ====================
void BLEManager::tick() {
  uint32_t now = millis();

  // 1. L'app nous a donné l'heure : le bracelet n'a pas d'horloge, sans ça
  //    toutes les mesures partent avec ts = 0.
  if (hasPendingTime) {
    uint32_t epoch = pendingEpoch;
    hasPendingTime = false;
    time_->sync(epoch, now);
    Serial.print("[BLE] Heure reçue de l'app : epoch=");
    Serial.println(epoch);
    logState("time-sync");
  }

  // 2. Vidage du tampon RAM demandé par la déconnexion (voir handleDisconnect).
  if (pendingFlush) {
    pendingFlush = false;
    storage_->flush();
  }

  // 3. Commande de synchro en attente.
  uint8_t cmd = pendingCmd;
  if (cmd != 0) {
    pendingCmd = 0;
    switch (cmd) {
      case SYNC_CMD_START:
        Serial.print("[SYNC] START reçu -> vidage du stock (");
        Serial.print(storage_->pending());
        Serial.println(" mesures en attente)");
        batchSeq = 0;
        sendNextBatch();
        break;

      case SYNC_CMD_ACK:
        if (syncState != SYNC_WAIT_ACK) {
          // Typiquement l'ACK d'un paquet déjà renvoyé après timeout :
          // sans conséquence, on l'ignore.
          Serial.println("[SYNC] ACK reçu hors attente, ignoré");
          break;
        }
        Serial.print("[SYNC] ACK paquet #");
        Serial.print(batchSeq);
        Serial.print(" -> purge de ");
        Serial.print(lastBatchCount);
        Serial.println(" mesures");
        storage_->confirm(lastBatchCount);
        lastBatchCount = 0;
        sendNextBatch();
        break;

      case SYNC_CMD_STOP:
        Serial.println("[SYNC] STOP reçu -> retour en idle (rien n'est purgé)");
        syncState = SYNC_IDLE;
        lastBatchCount = 0;
        logState("stop");
        break;

      default:
        Serial.println("[SYNC] Commande inconnue, ignorée");
        break;
    }
  }

  // 4. Pas d'ACK à temps : on renvoie le même paquet. Sans risque, rien n'a été
  //    purgé, et l'app déduplique par ts.
  if (syncState == SYNC_WAIT_ACK && (now - lastBatchSentMs) >= SYNC_ACK_TIMEOUT) {
    Serial.print("[SYNC] Pas d'ACK -> renvoi du paquet #");
    Serial.print(batchSeq);
    Serial.println(" (rien n'a été purgé)");
    batchSeq--;  // sendNextBatch le réincrémente : le numéro reste stable
    sendNextBatch();
  }

  // 5. On est en LIVE mais du backlog s'est accumulé (une notify ratée par
  //    exemple) : retour en vidage, comme le prévoit le diagramme du README.
  if (syncState == SYNC_LIVE && deviceConnected && storage_->pending() > 0) {
    Serial.print("[SYNC] ");
    Serial.print(storage_->pending());
    Serial.println(" mesures à écouler en LIVE -> retour en vidage");
    sendNextBatch();
  }

  // 6. Battement de coeur : même quand rien ne bouge, on veut savoir où on en est.
  if ((now - lastStateLogMs) >= STATE_LOG_INTERVAL) {
    logState("heartbeat");
  }
}

void BLEManager::sendNextBatch() {
  if (!deviceConnected) {
    syncState = SYNC_IDLE;
    return;
  }

  // Le MTU est négocié par le téléphone : s'il n'a pas demandé mieux que les
  // 23 octets par défaut, un paquet plein serait tronqué en silence. On adapte
  // plutôt la taille du paquet à ce qui passe vraiment.
  uint8_t maxBatch = HISTORY_BATCH;
  uint16_t mtu = pServer->getPeerMTU(connHandle);
  if (mtu > 3 + HISTORY_HEADER_SIZE) {
    uint16_t fits = (mtu - 3 - HISTORY_HEADER_SIZE) / MEASUREMENT_SIZE;
    if (fits < 1) fits = 1;
    if (fits < maxBatch) {
      maxBatch = (uint8_t)fits;
      Serial.print("[SYNC] MTU=");
      Serial.print(mtu);
      Serial.print(" -> paquets réduits à ");
      Serial.print(maxBatch);
      Serial.println(" mesures");
    }
  }

  Measurement batch[HISTORY_BATCH];
  uint8_t n = storage_->readBatch(batch, maxBatch);

  if (n == 0) {
    // Stock vide : l'app peut passer en live.
    uint8_t packet[HISTORY_HEADER_SIZE];
    size_t len = buildHistoryEndPacket(packet);
    pCharHistory->setValue(packet, len);
    pCharHistory->notify();

    syncState = SYNC_LIVE;
    lastBatchCount = 0;
    Serial.println("[SYNC] Stock vide -> passage en LIVE");
    logState("stock-vide");
    return;
  }

  uint8_t packet[HISTORY_HEADER_SIZE + HISTORY_BATCH * MEASUREMENT_SIZE];
  size_t len = buildHistoryPacket(batch, n, packet);
  pCharHistory->setValue(packet, len);
  pCharHistory->notify();

  batchSeq++;
  lastBatchCount = n;
  lastBatchSentMs = millis();
  syncState = SYNC_WAIT_ACK;

  Serial.print("[SYNC] Paquet #");
  Serial.print(batchSeq);
  Serial.print(" envoyé (");
  Serial.print(n);
  Serial.print(" mesures, ");
  Serial.print((unsigned)len);
  Serial.println(" octets) -> attente ACK");
  logState("paquet-envoye");
}

// ==================== ENVOI LIVE (meme record 8 octets) ====================
bool BLEManager::sendLive(const Measurement& m) {
  if (!deviceConnected || syncState != SYNC_LIVE || !pCharData) return false;

  uint8_t payload[MEASUREMENT_SIZE];
  encodeMeasurement(m, payload);
  pCharData->setValue(payload, MEASUREMENT_SIZE);
  return pCharData->notify();
}

// ==================== LA LIGNE QUI DIT TOUT ====================
// Émise à chaque transition d'état et toutes les STATE_LOG_INTERVAL ms. C'est
// elle qu'on colle dans un ticket : elle dit qui est connecté, qui attend quoi
// et combien de mesures sont encore en stock.
void BLEManager::logState(const char* reason) {
  lastStateLogMs = millis();

  const char* stateName = "IDLE";
  if (syncState == SYNC_WAIT_ACK)  stateName = "WAIT_ACK";
  else if (syncState == SYNC_LIVE) stateName = "LIVE";

  Serial.printf("[STATE] conn=%d sync=%s batch=%lu attente=%u ram=%lu flash=%lu "
                "pend=%lu drop=%lu ts=%lu (%s)\n",
                deviceConnected ? 1 : 0, stateName, (unsigned long)batchSeq,
                lastBatchCount,
                (unsigned long)(storage_ ? storage_->ramPending() : 0),
                (unsigned long)(storage_ ? storage_->pendingInFlash() : 0),
                (unsigned long)(storage_ ? storage_->pending() : 0),
                (unsigned long)(storage_ ? storage_->dropped() : 0),
                (unsigned long)(time_ ? time_->now(millis()) : 0), reason);
}