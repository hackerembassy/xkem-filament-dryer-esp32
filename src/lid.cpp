#include <Arduino.h>
#include "config.h"
#include "lid.h"

// Default to open (safe state): heater cannot run until switch confirms lid closed
static bool lidOpen = true;

void lid_init(void) {
    pinMode(PIN_LID_SWITCH, INPUT_PULLUP);
}

void lid_read(void) {
    // Dual-read software debounce: two reads separated by 1ms must agree
    // Reed switch with internal pullup: LOW = closed (magnet present), HIGH = open
    int first = digitalRead(PIN_LID_SWITCH);
    delay(1);
    int second = digitalRead(PIN_LID_SWITCH);

    if (first == second) {
        lidOpen = (first == HIGH);
    }
    // If reads disagree, keep previous state (noise rejection)
}

bool lid_isOpen(void) {
    return lidOpen;
}
