#ifndef CONFIG_H
#define CONFIG_H

// --- Pin assignments ---
#define PIN_RELAY       13
#define PIN_LCD_SDA     22
#define PIN_LCD_SCL     21
#define PIN_HTU_SDA     19
#define PIN_HTU_SCL     18
#define PIN_NTC         33
#define PIN_LID_SWITCH  27

// --- LCD ---
#define LCD_ADDRESS     0x27
#define LCD_COLS        16
#define LCD_ROWS        2

// --- NTC thermistor (Beta-equation conversion) ---
#define NTC_RESISTANCE      10000.0f    // Nominal resistance at 25C (ohms)
#define NTC_BETA            3950.0f     // Beta coefficient (K)
#define NTC_REF_TEMP        298.15f     // 25C in Kelvin
#define NTC_FIXED_RESISTOR  10000.0f    // Series resistor (ohms)
#define NTC_VCC             3.3f        // ADC reference voltage (V)
#define NTC_SAMPLES         64          // ADC oversampling count
#define NTC_TEMP_MIN        -10.0f      // Sanity check lower bound (C)
#define NTC_TEMP_MAX        200.0f      // Sanity check upper bound (C)

// --- Timing (milliseconds) ---
#define SENSOR_READ_INTERVAL_MS   2000

// --- Chamber temperature control (bang-bang) ---
#define CHAMBER_SETPOINT_DEFAULT  50.0f   // Default target (C)
#define CHAMBER_SETPOINT_MAX      60.0f   // Maximum allowed setpoint (C)
#define CHAMBER_SETPOINT_MIN      30.0f   // Minimum allowed setpoint (C)
#define CHAMBER_HYSTERESIS        2.0f    // Bang-bang hysteresis (C)

// --- Heatsink safety cutoff ---
#define HEATSINK_OVERTEMP         125.0f  // Overtemp threshold (C)
#define HEATSINK_HYSTERESIS       10.0f   // Recovery hysteresis (C)

// --- Sensor failure limits ---
#define SENSOR_FAIL_MAX           5       // Consecutive failures before forced relay off

// --- Thermal fault detection ---
#define THERMAL_CHECK_PERIOD_MS   180000UL   // 3 min continuous relay ON before checking (ms)
#define THERMAL_MIN_RISE          1.0f       // Minimum expected chamber rise in check period (C)
#define THERMAL_NEAR_SETPOINT     3.0f       // Skip check when within this range of setpoint (C)

// --- Operating mode presets ---
#define MAINTAIN_HUMIDITY_TARGET  40.0f      // Humidity threshold for maintain mode (%)
#define MAINTAIN_TEMP_TARGET      40.0f      // Heating target for maintain mode (C)
#define PRESET_PLA_TEMP           45.0f      // PLA drying temperature (C)
#define PRESET_PETG_TEMP          55.0f      // PETG drying temperature (C)
#define PRESET_ABS_TEMP           60.0f      // ABS drying temperature (C)
#define PRESET_TPU_TEMP           50.0f      // TPU drying temperature (C)
#define PRESET_MIX_TEMP           45.0f      // Mixed plastics safe drying temperature (C)

#endif
