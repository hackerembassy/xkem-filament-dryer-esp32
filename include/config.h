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
#define CHAMBER_SETPOINT_MAX      52.0f   // Maximum allowed setpoint (C) — practical ceiling for uninsulated enclosure
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
#define THERMAL_NEAR_SETPOINT     10.0f      // Skip check when within this range of setpoint (C)

// --- Operating mode presets ---
#define MAINTAIN_HUMIDITY_TARGET  20.0f      // Humidity threshold for maintain mode (%)
#define MAINTAIN_HUMIDITY_HYST    2.0f       // Hysteresis: relay stays off until humidity > target + hyst (%)
#define MAINTAIN_TEMP_TARGET      40.0f      // Heating target for maintain mode (C)
#define PRESET_PLA_TEMP           45.0f      // PLA drying temperature (C)
#define PRESET_PETG_TEMP          50.0f      // PETG drying temperature (C) — compensated with longer time
#define PRESET_ABS_TEMP           52.0f      // ABS drying temperature (C) — compensated with longer time
#define PRESET_TPU_TEMP           50.0f      // TPU drying temperature (C)
#define PRESET_MIX_TEMP           45.0f      // Mixed plastics safe drying temperature (C)

// --- Drying durations (hours) — lower temps = longer times ---
#define DRYING_HOURS_PLA    4
#define DRYING_HOURS_PETG   6       // Longer to compensate for 50C (recommended 55-65C)
#define DRYING_HOURS_ABS    8       // Much longer to compensate for 52C (recommended 60-80C)
#define DRYING_HOURS_TPU    5
#define DRYING_HOURS_MIX    4

// --- NTC short detection ---
#define NTC_VOUT_SHORT_FLOOR  0.15f // Below this voltage, NTC is shorted or ADC unreliable (V)

#endif
