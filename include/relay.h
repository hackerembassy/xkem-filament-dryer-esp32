#ifndef RELAY_H
#define RELAY_H

enum DryerMode {
    MODE_OFF = 0,
    MODE_MAINTAIN,
    MODE_DRY_PLA,
    MODE_DRY_PETG,
    MODE_DRY_ABS,
    MODE_DRY_TPU,
    MODE_DRY_MIX
};

void        relay_init(void);
void        relay_update(float chamberTemp, bool chamberValid,
                         float heatsinkTemp, bool heatsinkValid,
                         bool lidOpen, float humidity);
void        relay_forceOff(void);
float       relay_getSetpoint(void);
bool        relay_isOn(void);
bool        relay_isOvertemp(void);
bool        relay_isThermalFault(void);
void        relay_setMode(DryerMode mode);
DryerMode   relay_getMode(void);
const char* relay_getModeName(void);
const char* relay_getModeLabel(void);

#endif
