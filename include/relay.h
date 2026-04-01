#ifndef RELAY_H
#define RELAY_H

void  relay_init(void);
void  relay_update(float chamberTemp, bool chamberValid,
                   float heatsinkTemp, bool heatsinkValid);
void  relay_forceOff(void);
void  relay_setSetpoint(float tempC);
float relay_getSetpoint(void);
bool  relay_isOn(void);
bool  relay_isOvertemp(void);
bool  relay_isEnabled(void);
void  relay_setEnabled(bool enabled);

#endif
