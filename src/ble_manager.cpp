#include "ble_manager.h"
#include "config.h"
#include <Arduino.h>

// ==================== CALLBACK HANDLER ====================
static BLEManager* g_pBLEManager = nullptr;

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    if (g_pBLEManager) {
      g_pBLEManager->onConnect();
    }
  }
};

// ==================== CONSTRUCTEUR / DESTRUCTEUR ====================
BLEManager::BLEManager() 
  : pServer(nullptr), pCharHR(nullptr), pCharSpO2(nullptr), 
    pCharSteps(nullptr), deviceConnected(false) {
  g_pBLEManager = this;
}

BLEManager::~BLEManager() {
  NimBLEDevice::deinit(false);
}

// ==================== INITIALISATION ====================
bool BLEManager::initialize() {
  Serial.println("\n--- Initialisation BLE Manager ---");
  
  try {
    // Initialiser le device BLE
    NimBLEDevice::init(BLE_DEVICE_NAME);
    
    // Créer le serveur
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    // Créer le service
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    
    // Créer les caractéristiques
    pCharHR = pService->createCharacteristic(
      CHAR_HR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    
    pCharSpO2 = pService->createCharacteristic(
      CHAR_SPO2_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    
    pCharSteps = pService->createCharacteristic(
      CHAR_STEPS_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    
    Serial.println("[OK] BLE Manager initialisé");
    return true;
    
  } catch (const std::exception& e) {
    Serial.print("[FAIL] BLE Manager - ");
    Serial.println(e.what());
    return false;
  }
}

// ==================== ADVERTISING ====================
bool BLEManager::startAdvertising() {
  try {
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();
    
    Serial.println("[OK] BLE Advertising démarré");
    return true;
  } catch (const std::exception& e) {
    Serial.println("[FAIL] Advertising");
    return false;
  }
}

bool BLEManager::stopAdvertising() {
  NimBLEDevice::stopAdvertising();
  return true;
}

// ==================== ENVOI DES DONNÉES ====================
void BLEManager::updateHeartRate(uint8_t value) {
  if (deviceConnected && pCharHR) {
    pCharHR->setValue(&value, 1);
    pCharHR->notify();
  }
}

void BLEManager::updateSpO2(uint8_t value) {
  if (deviceConnected && pCharSpO2) {
    pCharSpO2->setValue(&value, 1);
    pCharSpO2->notify();
  }
}

void BLEManager::updateSteps(uint32_t value) {
  if (deviceConnected && pCharSteps) {
    pCharSteps->setValue((uint8_t*)&value, sizeof(value));
    pCharSteps->notify();
  }
}

// ==================== CALLBACKS ====================
void BLEManager::onConnect() {
  deviceConnected = true;
  Serial.println("[BLE] Client connecté !");
  startAdvertising();
}

void BLEManager::onDisconnect() {
  deviceConnected = false;
  Serial.println("[BLE] Client déconnecté");
}