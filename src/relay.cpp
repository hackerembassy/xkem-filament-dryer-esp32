#include <Arduino.h>
#include "config.h"
#include "relay.h"

static DryerMode currentMode   = MODE_MAINTAIN;
static float     setpoint      = MAINTAIN_TEMP_TARGET;
static bool      relayOn       = false;
static bool      heatsinkOT    = false;
static bool      chamberReached = false;

// Thermal fault detection
static bool          thermalFault    = false;
static bool          thermalTracking = false;
static unsigned long thermalOnStart  = 0;
static float         thermalStartTemp = 0.0f;

static const char* MODE_NAMES[]  = {"off", "maintain", "pla", "petg", "abs", "tpu", "mix"};
static const char* MODE_LABELS[] = {"OFF", "MNT", "PLA", "PET", "ABS", "TPU", "MIX"};

static void applyModeSetpoint(void) {
    switch (currentMode) {
        case MODE_OFF:       setpoint = MAINTAIN_TEMP_TARGET; break;
        case MODE_MAINTAIN:  setpoint = MAINTAIN_TEMP_TARGET; break;
        case MODE_DRY_PLA:   setpoint = PRESET_PLA_TEMP; break;
        case MODE_DRY_PETG:  setpoint = PRESET_PETG_TEMP; break;
        case MODE_DRY_ABS:   setpoint = PRESET_ABS_TEMP; break;
        case MODE_DRY_TPU:   setpoint = PRESET_TPU_TEMP; break;
        case MODE_DRY_MIX:   setpoint = PRESET_MIX_TEMP; break;
    }
}

static void relayOff(void) {
    digitalWrite(PIN_RELAY, LOW);
    relayOn = false;
    thermalTracking = false;
}

static void relayOnAction(float chamberTemp, bool chamberValid) {
    if (!relayOn && chamberValid) {
        thermalTracking  = true;
        thermalOnStart   = millis();
        thermalStartTemp = chamberTemp;
    }
    digitalWrite(PIN_RELAY, HIGH);
    relayOn = true;
}

void relay_init(void) {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    relayOn = false;
}

void relay_update(float chamberTemp, bool chamberValid,
                  float heatsinkTemp, bool heatsinkValid,
                  bool lidOpen, float humidity) {

    // Priority 0: lid open
    if (lidOpen) {
        relayOff();
        return;
    }

    // Priority 1: thermal fault (latched until mode change)
    if (thermalFault) {
        relayOff();
        return;
    }

    // Priority 2: dryer off
    if (currentMode == MODE_OFF) {
        relayOff();
        return;
    }

    // Priority 3: heatsink sensor invalid
    if (!heatsinkValid) {
        relayOff();
        return;
    }

    // Priority 4: heatsink overtemp with hysteresis
    if (heatsinkTemp >= HEATSINK_OVERTEMP) {
        heatsinkOT = true;
    } else if (heatsinkTemp < (HEATSINK_OVERTEMP - HEATSINK_HYSTERESIS)) {
        heatsinkOT = false;
    }
    if (heatsinkOT) {
        relayOff();
        return;
    }

    // Maintain mode: humidity gate
    if (currentMode == MODE_MAINTAIN) {
        if (chamberValid && humidity <= MAINTAIN_HUMIDITY_TARGET) {
            relayOff();
            chamberReached = false;
            return;
        }
    }

    // Bang-bang temperature control with hysteresis
    if (chamberValid) {
        if (chamberTemp >= setpoint) {
            chamberReached = true;
        } else if (chamberTemp < (setpoint - CHAMBER_HYSTERESIS)) {
            chamberReached = false;
        }
    }

    if (chamberReached) {
        relayOff();
    } else {
        relayOnAction(chamberTemp, chamberValid);
    }

    // Thermal fault check (while relay is ON)
    if (relayOn && thermalTracking && chamberValid) {
        bool nearSetpoint = (chamberTemp >= (setpoint - THERMAL_NEAR_SETPOINT));
        if (!nearSetpoint) {
            unsigned long elapsed = millis() - thermalOnStart;
            if (elapsed >= THERMAL_CHECK_PERIOD_MS) {
                float rise = chamberTemp - thermalStartTemp;
                if (rise < THERMAL_MIN_RISE) {
                    thermalFault = true;
                    relayOff();
                    Serial.println("THERMAL FAULT: Chamber temp not rising, dryer disabled");
                    return;
                }
                // Passed; restart tracking from current temp
                thermalOnStart   = millis();
                thermalStartTemp = chamberTemp;
            }
        }
    }
}

void relay_forceOff(void) {
    relayOff();
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

bool relay_isThermalFault(void) {
    return thermalFault;
}

void relay_setMode(DryerMode mode) {
    currentMode = mode;
    applyModeSetpoint();
    chamberReached  = false;
    thermalFault    = false;
    thermalTracking = false;
    if (currentMode == MODE_OFF) {
        relayOff();
    }
}

DryerMode relay_getMode(void) {
    return currentMode;
}

const char* relay_getModeName(void) {
    return MODE_NAMES[(int)currentMode];
}

const char* relay_getModeLabel(void) {
    return MODE_LABELS[(int)currentMode];
}
