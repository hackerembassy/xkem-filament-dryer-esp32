#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "relay.h"
#include "sensors.h"
#include "display.h"
#include "webserver.h"
#include "lid.h"
#include "datalog.h"

static unsigned long lastSensorRead = 0;
static int heatsinkFailCount = 0;
static int chamberFailCount  = 0;

void setup() {
    Serial.begin(115200);

    // Safety first: relay OFF before anything else
    relay_init();

    // Let power supply stabilize before I2C init
    delay(500);

    // I2C0 (Wire): HTU21D sensor — library hardcodes Wire internally
    Wire.begin(PIN_HTU_SDA, PIN_HTU_SCL);
    Wire.setClock(50000);
    Wire.setTimeOut(50);

    // LCD shares I2C0 (Wire) with HTU21D — different addresses (0x27 vs 0x40)

    sensors_init();
    lid_init();
    display_init();

    // WiFi connect (may block up to 10s, shows IP on LCD when connected)
    webserver_init();

    // Data logging (LittleFS + NTP, must be after WiFi for NTP sync)
    datalog_init();

    // Show IP on LCD if connected
    if (WiFi.status() == WL_CONNECTED) {
        display_showIP(WiFi.localIP().toString().c_str());
    }

    // Watchdog: reset ESP if loop hangs for >15s (after WiFi is done)
    // Arduino ESP32 core 3.x already initializes TWDT at boot,
    // so use reconfigure() to change the timeout instead of init()
    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdtConfig);
    esp_task_wdt_add(NULL);

    Serial.println("Filament dryer ready");
}

void loop() {
    esp_task_wdt_reset();

    // Handle HTTP requests (non-blocking)
    webserver_loop();

    unsigned long now = millis();

    // Sensor read + relay control every SENSOR_READ_INTERVAL_MS
    if (now - lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
        lastSensorRead = now;

        sensors_read();
        lid_read();

        float chamberTemp  = sensors_getChamberTemp();
        float humidity     = sensors_getHumidity();
        float heatsinkTemp = sensors_getHeatsinkTemp();
        bool  chamberValid = sensors_isChamberValid();
        bool  heatsinkValid = sensors_isHeatsinkValid();
        bool  lidOpen       = lid_isOpen();

        // Track consecutive sensor failures
        if (!heatsinkValid) {
            heatsinkFailCount++;
        } else {
            heatsinkFailCount = 0;
        }

        if (!chamberValid) {
            chamberFailCount++;
        } else {
            chamberFailCount = 0;
        }

        // Forced relay off on sustained sensor failure
        if (heatsinkFailCount >= SENSOR_FAIL_MAX) {
            relay_forceOff();
            Serial.println("SAFETY: Heatsink sensor failure limit reached, relay forced OFF");
        } else if (chamberFailCount >= SENSOR_FAIL_MAX) {
            relay_forceOff();
            Serial.println("SAFETY: Chamber sensor failure limit reached, relay forced OFF");
        } else {
            relay_update(chamberTemp, chamberValid, heatsinkTemp, heatsinkValid, lidOpen, humidity);
        }

        // Data logging (buffered, flushed to flash periodically)
        datalog_record(chamberTemp, humidity, heatsinkTemp,
                       chamberValid, heatsinkValid,
                       relay_isOn(), lidOpen,
                       relay_getSetpoint(),
                       relay_getMode() != MODE_OFF,
                       relay_isOvertemp());

        // Serial logging
        Serial.printf("Chamber: %.1fC (%s) | Humidity: %.1f%% | Heatsink: %.1fC (%s) | "
                       "Relay: %s | Mode: %s | Setpoint: %.1fC | Lid: %s | ThermalFault: %s\n",
                       chamberTemp, chamberValid ? "ok" : "ERR",
                       humidity,
                       heatsinkTemp, heatsinkValid ? "ok" : "ERR",
                       relay_isOn() ? "ON" : "OFF",
                       relay_getModeName(),
                       relay_getSetpoint(),
                       lidOpen ? "OPEN" : "closed",
                       relay_isThermalFault() ? "YES" : "no");

        // Display update (same interval as sensor reads to avoid hammering I2C)
        display_update(chamberTemp, humidity, heatsinkTemp,
                       chamberValid, heatsinkValid,
                       relay_isOn(), relay_isOvertemp(), lidOpen,
                       relay_isThermalFault(), relay_getModeLabel());
    }

    // Flush data log buffer to flash periodically
    datalog_flush();
}
