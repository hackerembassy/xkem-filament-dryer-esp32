#ifndef DATALOG_H
#define DATALOG_H

#include <WebServer.h>

void datalog_init(void);
void datalog_record(float chamberTemp, float humidity, float heatsinkTemp,
                    bool chamberValid, bool heatsinkValid,
                    bool relayOn, bool lidOpen,
                    float setpoint, bool enabled, bool overtemp);
void datalog_flush(void);
void datalog_registerEndpoints(WebServer &server);

#endif
