#ifndef DISPLAY_H
#define DISPLAY_H

void display_init(void);
void display_update(float chamberTemp, float humidity, float heatsinkTemp,
                    bool chamberValid, bool heatsinkValid,
                    bool relayOn, bool overtemp);
void display_showIP(const char *ip);

#endif
