#ifndef CONFIG_H
#define CONFIG_H

// WIRED HARDWARE
// Not set here but through the build_flags of platformio.ini:
// HAS_OXYGEN, HAS_IMU, HAS_POWER_BUTTON. A missing flag means the sensor is not
// wired: the matching code is compiled out and replaced by simulated values.

// General configuration
#define DEV_ID "BRASCO-00"
#define BLE_DEVICE_NAME "BRASCO-00"

// XIAO ESP32S3 pinout
#define SDA_PIN 5   // D4
#define SCL_PIN 6   // D5

// Deep sleep button (3 s long press). D9 = GPIO8, an RTC GPIO: mandatory for
// the ext0 wake-up (see WAKEUP_GPIO in power_management.h).
#define BUTTON_PIN D9

// Active-low wiring: GPIO8 -> button -> GND, with the internal pull-up (see the
// INPUT_PULLUP pinMode in main.cpp). No external resistor to solder, and the
// idle line sits firmly at 3V3 instead of floating.
// A floating pin read as plain INPUT was the bug: noise, never a real press.
// All button logic compares against this constant - change it only here, and
// remember the ext0 wake level in power_management.cpp (0 = active low).
#define BUTTON_ACTIVE_LEVEL LOW

// Debounce: a mechanical contact bounces for ~1 to 10 ms. Without this guard a
// single press shows up as several "touched" in the log.
#define BUTTON_DEBOUNCE_MS 30

// External status LED (normal mode / going to sleep), enabled by the
// HAS_STATUS_LED flag in platformio.ini.
// Active-high wiring: GPIO -> resistor -> LED anode, cathode to GND.
// HIGH turns it on (tested on board: active low left it dark).
// If it stays dark in both states it is the wiring: flash the esp32-s3-diag env
// (src/diag/pins_diag.cpp) to settle that before touching anything here.
#define STATUS_LED_PIN D10
#define STATUS_LED_ON  LOW
#define STATUS_LED_OFF HIGH

// Going-to-sleep signal: 4 blinks of 150 ms.
// Slow enough to be seen, short enough not to delay sleep (~1.2 s).
#define SLEEP_BLINK_COUNT  4
#define SLEEP_BLINK_PERIOD 150  // ms per half period (on then off)

// "Alive" blink in normal operation. 500 ms: slow enough for the eye to catch
// the alternation, fast enough that a frozen board stands out right away
// (LED stuck in one state).
#define STATUS_BLINK_PERIOD 500  // ms per half period

// Sensors
#define I2C_ADDRESS      0x57
#define MAX30102_ADDRESS 0x57
#define MPU6050_ADDRESS  0x68

// BLE pairing code
#define BLE_STATIC_PASSKEY 123456

// BLE UUIDs
#define SERVICE_UUID "146ef449-0083-438a-9af6-5be5bb541e2c"
#define DATA_UUID    "146ef450-0083-438a-9af6-5be5bb541e2c"
// The next three carry the backlog catch-up protocol
// (see README, "Protocole BLE - bracelet connecte")
#define HISTORY_UUID   "146ef451-0083-438a-9af6-5be5bb541e2c"  // notify: packet of stored measurements
#define SYNC_CTRL_UUID "146ef452-0083-438a-9af6-5be5bb541e2c"  // write : START / ACK / STOP
#define TIME_UUID      "146ef453-0083-438a-9af6-5be5bb541e2c"  // write : UTC epoch, uint32 LE

// JSON
#define JSON_BUFFER_SIZE 256

// Timing constants
#define READ_INTERVAL 4000UL  // ms

// Backlog storage (see include/storage.h)
#define STORAGE_FILE    "/data.bin"
#define RING_CAPACITY   25000UL  // 25000 * 8 B = 200 KB of flash, ~27 h of measurements
#define RAM_BATCH       32       // measurements kept in RAM before writing to flash
#define PREFS_NAMESPACE "brasco"

// Sync
#define HISTORY_BATCH    20      // measurements per packet; 20*8+4 = 164 B, fits the 185 MTU
#define SYNC_ACK_TIMEOUT 5000UL  // ms without an ACK -> resend the packet (nothing was dropped)

// Periodic state line on the serial port: without it a silent bracelet is
// impossible to debug (waiting for an ACK? storing? alone?).
#define STATE_LOG_INTERVAL 5000UL  // ms

// Error codes
#define ERR_BLE_INIT 0x2 // BLE init failed
#define ERR_BLE_ADV  0x3 // BLE advertising failed to start
#define ERR_I2C      0x4 // I2C communication error
#define ERR_MAX30102 0x5 // MAX30102 sensor init failed
#define ERR_MPU6050  0x6 // MPU6050 sensor init failed
#define ERR_STORAGE  0x7 // LittleFS mount / storage file error

#endif // CONFIG_H
