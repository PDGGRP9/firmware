#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>

class BLEManager {
private:
    NimBLEServer* pServer;
    NimBLECharacteristic* pCharHR;
    NimBLECharacteristic* pCharSpO2;
    NimBLECharacteristic* pCharSteps;
    bool deviceConnected;

public:
    BLEManager();
    ~BLEManager();
    
    // Initialisation
    bool initialize();
    bool startAdvertising();
    bool stopAdvertising();
    
    // Getters
    bool isConnected() const { return deviceConnected; }
    
    // Envoi des données
    void updateHeartRate(uint8_t value);
    void updateSpO2(uint8_t value);
    void updateSteps(uint32_t value);
    
    // Callbacks internes
    void onConnect();
    void onDisconnect();
};

#endif // BLE_MANAGER_H