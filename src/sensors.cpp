#include <Arduino.h>
#include <Wire.h>
#include <HTU21D.h>
#include "config.h"
#include "sensors.h"

static HTU21D myHTU21D(HTU21D_RES_RH12_TEMP14);

static float chamberTemp   = 0.0f;
static float humidity       = 0.0f;
static float heatsinkTemp  = 0.0f;
static bool  chamberValid  = false;
static bool  heatsinkValid = false;

// Read NTC thermistor via ADC with oversampling and Beta-equation conversion
static float readNTC(void) {
    long sum = 0;
    for (int i = 0; i < NTC_SAMPLES; i++) {
        sum += analogReadMilliVolts(PIN_NTC);
        delayMicroseconds(100);
    }
    float mv = sum / (float)NTC_SAMPLES;

    float Vout = mv / 1000.0f;
    if (Vout >= (NTC_VCC - 0.01f)) return NTC_TEMP_MAX + 1.0f;  // short circuit protection
    if (Vout <= 0.01f) return NTC_TEMP_MIN - 1.0f;              // open circuit protection

    // Divider: VCC → R_fixed → ADC → R_ntc → GND
    float R_ntc = NTC_FIXED_RESISTOR * Vout / (NTC_VCC - Vout);
    float tempK = 1.0f / (1.0f / NTC_REF_TEMP + log(R_ntc / NTC_RESISTANCE) / NTC_BETA);
    return tempK - 273.15f;
}

void sensors_init(void) {
    // Wire must be initialized with HTU21D pins before this call.
    // HTU21D library internally calls Wire.begin() which is a no-op
    // if Wire is already started (ESP32 Arduino core 3.x behavior).
    if (!myHTU21D.begin()) {
        Serial.println("ERROR: HTU21D not found on I2C bus");
    }

    pinMode(PIN_NTC, INPUT);
}

bool sensors_read(void) {
    bool allOk = true;

    // Read heatsink NTC
    float ntcTemp = readNTC();
    if (ntcTemp >= NTC_TEMP_MIN && ntcTemp <= NTC_TEMP_MAX) {
        heatsinkTemp = ntcTemp;
        heatsinkValid = true;
    } else {
        heatsinkValid = false;
        allOk = false;
    }

    // Read HTU21D (I2C1)
    float t = myHTU21D.readTemperature();
    float h = myHTU21D.readHumidity();
    if (t != HTU21D_ERROR && h != HTU21D_ERROR) {
        chamberTemp = t;
        humidity = h;
        chamberValid = true;
    } else {
        chamberValid = false;
        allOk = false;
    }

    return allOk;
}

float sensors_getChamberTemp(void) {
    return chamberTemp;
}

float sensors_getHumidity(void) {
    return humidity;
}

float sensors_getHeatsinkTemp(void) {
    return heatsinkTemp;
}

bool sensors_isChamberValid(void) {
    return chamberValid;
}

bool sensors_isHeatsinkValid(void) {
    return heatsinkValid;
}
