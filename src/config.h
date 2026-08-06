#ifndef CONFIG_H
#define CONFIG_H

// Configuration générale
#define DEV_ID "BRASCO-00"
#define BLE_DEVICE_NAME "BraceletTest"

// Pinout XIAO ESP32S3
#define SDA_PIN 5   // D4
#define SCL_PIN 6   // D5

// Capteurs
#define I2C_ADDRESS 0x57
#define MAX30102_ADDRESS 0x57
#define MPU6050_ADDRESS 0x68

// BLE UUIDs
#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHAR_HR_UUID "12345678-1234-1234-1234-123456789ab1"
#define CHAR_SPO2_UUID "12345678-1234-1234-1234-123456789ab2"
#define CHAR_STEPS_UUID "12345678-1234-1234-1234-123456789ab3"

// Constantes de temps
#define READ_INTERVAL 4000UL  // ms

// Codes d'erreur
#define ERR_BLE 0x2
#define ERR_I2C 0x3
#define ERR_MAX30102 0x4
#define ERR_MPU6050 0x5
#define ERR_SENSORS 0x6

#endif // CONFIG_H