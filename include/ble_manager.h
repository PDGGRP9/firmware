#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>

#include "logic/time_source.h"
#include "storage.h"

// Où en est le bracelet vis-à-vis de l'app (voir le diagramme d'état du README).
//   SYNC_IDLE     : pas d'app appairée, ou l'app n'a pas encore dit START.
//   SYNC_WAIT_ACK : un paquet d'historique est parti, on attend l'ACK.
//                   Rien n'est purgé tant qu'il n'arrive pas.
//   SYNC_LIVE     : stock vide, les mesures partent au fil de l'eau, sans ACK.
enum SyncState { SYNC_IDLE, SYNC_WAIT_ACK, SYNC_LIVE };

class BLEManager {
private:
    NimBLEServer*         pServer;
    NimBLECharacteristic* pCharData;
    NimBLECharacteristic* pCharHistory;
    NimBLECharacteristic* pCharSyncCtrl;
    NimBLECharacteristic* pCharTime;
    bool deviceConnected;
    bool advertisingActive;

    // Le stockage et l'heure vivent dans main.cpp : on ne fait que les utiliser.
    Storage*    storage_;
    TimeSource* time_;

    SyncState syncState;
    uint8_t  lastBatchCount;    // taille du paquet en attente d'ACK
    // Numéro du paquet en cours. Il part dans l'en-tête HISTORY et l'app le
    // renvoie dans son ACK : c'est ce qui permet de refuser l'ACK d'un paquet
    // déjà acquitté. Seule l'égalité est testée, le wrap à 65535 est sans effet.
    uint16_t batchSeq;
    uint32_t lastBatchSentMs;
    uint32_t lastStateLogMs;

    // Écrits depuis la tâche BLE, lus depuis loop() : voir tick().
    volatile uint8_t  pendingCmd;      // 0 = rien en attente
    volatile uint16_t pendingAckSeq;   // numéro porté par le dernier ACK reçu
    volatile uint32_t pendingEpoch;
    volatile bool     hasPendingTime;
    volatile bool     pendingFlush;
    uint16_t connHandle;   // pour interroger le MTU négocié avec ce téléphone

    void sendNextBatch();
    // La ligne qui dit tout : qui est connecté, qui attend quoi, ce qui reste
    // en stock. `reason` = ce qui l'a déclenchée.
    void logState(const char* reason);

public:
    BLEManager();
    ~BLEManager();

    // storage et time doivent vivre aussi longtemps que le BLEManager.
    bool initialize(Storage* storage, TimeSource* time);
    bool startAdvertising();
    bool stopAdvertising();

    bool isConnected() const { return deviceConnected; }
    bool isLive() const { return syncState == SYNC_LIVE; }

    void sendSensorData(uint8_t hr, uint8_t spo2, uint32_t steps);

    // Envoie une mesure en direct. false = elle n'est pas partie, l'appelant
    // doit la mettre en flash (elle deviendra du backlog).
    bool sendLive(const Measurement& m);

    // À appeler à chaque tour de loop() : c'est là que le protocole avance.
    // Les callbacks NimBLE tournent sur la tâche hôte BLE et ne peuvent pas
    // écrire en flash (bien trop long) : ils posent la commande reçue, tick()
    // la traite.
    void tick();

    // Appelés depuis les callbacks NimBLE — ne font que mémoriser.
    // `seq` n'a de sens que pour SYNC_CMD_ACK ; 0 pour START et STOP.
    void onSyncCommand(uint8_t cmd, uint16_t seq);
    void onTimeWrite(uint32_t epoch);

    void handleConnect();
    void handleDisconnect();
    void handleAuthComplete(bool success, uint16_t handle);
    uint32_t getPassKey() const;
};

#endif