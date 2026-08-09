#include "ble_manager.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

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
      CHAR_DATA_UUID,
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

  // Valeur initiale : JSON vide
  JsonDocument doc;
  doc["device_id"] = DEV_ID;
  doc["hr"] = 0;
  doc["spo2"] = 0;
  doc["steps"] = 0;
  doc["ts"] = 0;

  char buffer[JSON_BUFFER_SIZE];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));
  pCharData->setValue((uint8_t*)buffer, len);

  Serial.println("[OK] BLE Manager initialisé");
  return true;
}

// ==================== ADVERTISING ====================
bool BLEManager::startAdvertising() {
  if (advertisingActive) {
    return true;
  }

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setName(BLE_DEVICE_NAME);
  pAdvertising->enableScanResponse(true);

  bool ok = pAdvertising->start();
  if (ok) {
    advertisingActive = true;
    Serial.println("[OK] BLE Advertising démarré");
  } else {
    Serial.println("[FAIL] Advertising");
  }
  return ok;
}

bool BLEManager::stopAdvertising() {
  NimBLEDevice::stopAdvertising();
  advertisingActive = false;
  return true;
}

// ==================== ENVOI JSON ====================
void BLEManager::sendSensorData(uint8_t hr, uint8_t spo2, uint32_t steps) {
  if (!deviceConnected || !pCharData) return;

  JsonDocument doc;
  doc["device_id"] = DEV_ID;
  doc["hr"]        = hr;
  doc["spo2"]      = spo2;
  doc["steps"]     = steps;
  doc["ts"]        = millis();

  char buffer[JSON_BUFFER_SIZE];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));

  pCharData->setValue((uint8_t*)buffer, len);
  pCharData->notify();

  #ifdef DEBUG_BLE
  Serial.print("[BLE JSON] ");
  Serial.println(buffer);
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