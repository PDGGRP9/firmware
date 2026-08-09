#ifndef CONFIG_H
#define CONFIG_H

// Configuration générale
#define DEV_ID "BRASCO-00"
#define BLE_DEVICE_NAME "BRASCO-00"

// Pinout XIAO ESP32S3
#define SDA_PIN 5   // D4
#define SCL_PIN 6   // D5

// Capteurs
#define I2C_ADDRESS      0x57
#define MAX30102_ADDRESS 0x57
#define MPU6050_ADDRESS  0x68

// Code d'appairage BLE
#define BLE_STATIC_PASSKEY 123456

// BLE UUIDs
#define SERVICE_UUID     "146ef449-0083-438a-9af6-5be5bb541e2c"
#define CHAR_DATA_UUID   "146ef450-0083-438a-9af6-5be5bb541e2c"

// JSON
#define JSON_BUFFER_SIZE 256

// Constantes de temps
#define READ_INTERVAL 4000UL  // ms

// Codes d'erreur
#define ERR_BLE_INIT 0x2
#define ERR_BLE_ADV  0x3
#define ERR_I2C      0x4
#define ERR_MAX30102 0x5
#define ERR_MPU6050  0x6
#define ERR_SENSORS  0x7

#endif // CONFIG_H