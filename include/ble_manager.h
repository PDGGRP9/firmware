#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>

class BLEManager {
private:
    NimBLEServer*         pServer;
    NimBLECharacteristic* pCharData;
    bool deviceConnected;
    bool advertisingActive;

public:
    BLEManager();
    ~BLEManager();

    bool initialize();
    bool startAdvertising();
    bool stopAdvertising();

    bool isConnected() const { return deviceConnected; }

    void sendSensorData(uint8_t hr, uint8_t spo2, uint32_t steps);

    void handleConnect();
    void handleDisconnect();
    void handleAuthComplete(bool success);
    uint32_t getPassKey() const;
};

#endif