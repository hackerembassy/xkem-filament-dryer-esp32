#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"
#include "display.h"

static LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS, Wire);

void display_init(void) {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Filament Dryer");
    lcd.setCursor(0, 1);
    lcd.print("Starting...");
}

void display_showIP(const char *ip) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi connected");
    lcd.setCursor(0, 1);
    lcd.print(ip);
    delay(2000);
}

// Format a float to one decimal place into a buffer (e.g. "150.0")
static void fmtTemp(char *buf, size_t len, float val) {
    int whole = (int)val;
    int frac  = abs((int)(val * 10) % 10);
    snprintf(buf, len, "%d.%d", whole, frac);
}

void display_update(float chamberTemp, float humidity, float heatsinkTemp,
                    bool chamberValid, bool heatsinkValid,
                    bool relayOn, bool overtemp, bool lidOpen,
                    bool thermalFault, const char* modeLabel) {
    char line[21]; // extra room to avoid truncation warnings; LCD only shows 16

    // Line 1: "T:23.4 RH:61.2 " or "T:ERR  RH:ERR  "
    lcd.setCursor(0, 0);
    if (chamberValid) {
        char tbuf[7];
        char hbuf[7];
        fmtTemp(tbuf, sizeof(tbuf), chamberTemp);
        fmtTemp(hbuf, sizeof(hbuf), humidity);
        snprintf(line, sizeof(line), "T:%-5s RH:%-4s", tbuf, hbuf);
    } else {
        snprintf(line, sizeof(line), "T:ERR  RH:ERR   ");
    }
    line[LCD_COLS] = '\0';
    lcd.print(line);

    // Line 2: "HS:45.2 PLA ON L" or "*THERMAL FAULT *"
    lcd.setCursor(0, 1);
    if (thermalFault) {
        lcd.print("*THERMAL FAULT *");
        return;
    }

    if (!heatsinkValid) {
        snprintf(line, sizeof(line), "HS:%-4s %s %-2s", "ERR", modeLabel, relayOn ? "ON" : "--");
    } else if (overtemp) {
        snprintf(line, sizeof(line), "HS:%-4s %s %-2s", "HOT", modeLabel, relayOn ? "ON" : "--");
    } else {
        char hbuf[7];
        fmtTemp(hbuf, sizeof(hbuf), heatsinkTemp);
        snprintf(line, sizeof(line), "HS:%-4s %s %-2s", hbuf, modeLabel, relayOn ? "ON" : "--");
    }
    // Pad to 15 chars and append lid indicator at position 15
    int len = strlen(line);
    while (len < 15) line[len++] = ' ';
    line[15] = lidOpen ? 'O' : 'L';
    line[LCD_COLS] = '\0';
    lcd.print(line);
}
