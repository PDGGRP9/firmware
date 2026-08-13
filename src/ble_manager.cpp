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
      g_pBLEManager->handleAuthComplete(success);
    }
  }
};

// ==================== CONSTRUCTEUR / DESTRUCTEUR ====================
BLEManager::BLEManager()
  : pServer(nullptr), pCharData(nullptr),
    deviceConnected(false), advertisingActive(false) {
  g_pBLEManager = this;
}

BLEManager::~BLEManager() {
  NimBLEDevice::deinit(true);
}

// ==================== INITIALISATION ====================
bool BLEManager::initialize() {
  Serial.println("\n--- Initialisation BLE Manager ---");

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
  Serial.println("[BLE] Client connecté et authentifié !");
}

void BLEManager::handleDisconnect() {
  deviceConnected = false;
  advertisingActive = false;
  Serial.println("[BLE] Client déconnecté");
  startAdvertising();
}

void BLEManager::handleAuthComplete(bool success) {
  if (success) {
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