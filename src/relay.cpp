#include <Arduino.h>
#include "config.h"
#include "relay.h"

static float setpoint       = CHAMBER_SETPOINT_DEFAULT;
static bool  relayOn        = false;
static bool  heatsinkOT     = false;
static bool  chamberReached = false;
static bool  enabled        = true;

void relay_init(void) {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    relayOn = false;
}

void relay_update(float chamberTemp, bool chamberValid,
                  float heatsinkTemp, bool heatsinkValid,
                  bool lidOpen) {
    // Priority 0: lid open — immediate safety shutoff
    if (lidOpen) {
        digitalWrite(PIN_RELAY, LOW);
        relayOn = false;
        return;
    }

    // Dryer disabled by user — relay stays off
    if (!enabled) {
        digitalWrite(PIN_RELAY, LOW);
        relayOn = false;
        return;
    }

    // Priority 1: invalid heatsink reading — fail-safe OFF
    if (!heatsinkValid) {
        digitalWrite(PIN_RELAY, LOW);
        relayOn = false;
        return;
    }

    // Priority 2: heatsink overtemp protection with hysteresis
    if (heatsinkTemp >= HEATSINK_OVERTEMP) {
        heatsinkOT = true;
    } else if (heatsinkTemp < (HEATSINK_OVERTEMP - HEATSINK_HYSTERESIS)) {
        heatsinkOT = false;
    }

    if (heatsinkOT) {
        digitalWrite(PIN_RELAY, LOW);
        relayOn = false;
        return;
    }

    // Priority 3: chamber bang-bang control with hysteresis
    // If chamber sensor is invalid, hold current relay state
    if (chamberValid) {
        if (chamberTemp >= setpoint) {
            chamberReached = true;
        } else if (chamberTemp < (setpoint - CHAMBER_HYSTERESIS)) {
            chamberReached = false;
        }
    }

    if (chamberReached) {
        digitalWrite(PIN_RELAY, LOW);
        relayOn = false;
    } else {
        digitalWrite(PIN_RELAY, HIGH);
        relayOn = true;
    }
}

void relay_forceOff(void) {
    digitalWrite(PIN_RELAY, LOW);
    relayOn = false;
}

void relay_setSetpoint(float tempC) {
    if (tempC < CHAMBER_SETPOINT_MIN) tempC = CHAMBER_SETPOINT_MIN;
    if (tempC > CHAMBER_SETPOINT_MAX) tempC = CHAMBER_SETPOINT_MAX;
    setpoint = tempC;
    // Reset bang-bang state so relay re-evaluates against new setpoint
    chamberReached = false;
}

float relay_getSetpoint(void) {
    return setpoint;
}

bool relay_isOn(void) {
    return relayOn;
}

bool relay_isOvertemp(void) {
    return heatsinkOT;
}

bool relay_isEnabled(void) {
    return enabled;
}

void relay_setEnabled(bool state) {
    enabled = state;
    if (!enabled) {
        relay_forceOff();
    }
}
