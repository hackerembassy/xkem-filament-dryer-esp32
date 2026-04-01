#ifndef SENSORS_H
#define SENSORS_H

void  sensors_init(void);
bool  sensors_read(void);
float sensors_getChamberTemp(void);
float sensors_getHumidity(void);
float sensors_getHeatsinkTemp(void);
bool  sensors_isChamberValid(void);
bool  sensors_isHeatsinkValid(void);

#endif
