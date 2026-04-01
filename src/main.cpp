#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "relay.h"
#include "sensors.h"
#include "display.h"
#include "webserver.h"

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

    // I2C1 (Wire1): LCD — uses patched library with TwoWire& parameter
    Wire1.begin(PIN_LCD_SDA, PIN_LCD_SCL);
    Wire1.setClock(50000);
    Wire1.setTimeOut(50);

    sensors_init();
    display_init();

    // WiFi connect (may block up to 10s, shows IP on LCD when connected)
    webserver_init();

    // Show IP on LCD if connected
    if (WiFi.status() == WL_CONNECTED) {
        display_showIP(WiFi.localIP().toString().c_str());
    }

    // Watchdog: reset ESP if loop hangs for >15s (after WiFi is done)
    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = 15000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdtConfig);
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

        float chamberTemp  = sensors_getChamberTemp();
        float humidity     = sensors_getHumidity();
        float heatsinkTemp = sensors_getHeatsinkTemp();
        bool  chamberValid = sensors_isChamberValid();
        bool  heatsinkValid = sensors_isHeatsinkValid();

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
            relay_update(chamberTemp, chamberValid, heatsinkTemp, heatsinkValid);
        }

        // Serial logging
        Serial.printf("Chamber: %.1fC (%s) | Humidity: %.1f%% | Heatsink: %.1fC (%s) | "
                       "Relay: %s | Setpoint: %.1fC | Enabled: %s\n",
                       chamberTemp, chamberValid ? "ok" : "ERR",
                       humidity,
                       heatsinkTemp, heatsinkValid ? "ok" : "ERR",
                       relay_isOn() ? "ON" : "OFF",
                       relay_getSetpoint(),
                       relay_isEnabled() ? "yes" : "no");
    }

    // Display update (static 2-line layout, no cycling)
    display_update(
        sensors_getChamberTemp(),
        sensors_getHumidity(),
        sensors_getHeatsinkTemp(),
        sensors_isChamberValid(),
        sensors_isHeatsinkValid(),
        relay_isOn(),
        relay_isOvertemp()
    );
}
