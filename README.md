# xkem-filament-dryer-esp32

ESP32 firmware for a DIY filament dryer with bang-bang temperature control, web interface, and multi-layer safety protection.

## Features

- **Operating modes**: OFF, MAINTAIN (humidity-gated overnight mode at 40C), and timed drying presets (PLA 45C/4h, PETG 50C/6h, ABS 52C/8h, TPU 50C/5h, MIX 45C/4h)
- **Bang-bang chamber temperature control** with 2C hysteresis
- **Lid switch safety interlock** using reed switch on GPIO 27 — relay disabled when lid is open
- **Thermal fault detection** — detects lid-open-but-reads-closed by monitoring chamber temp rise (latches fault if <1C rise after 3 min of continuous heating)
- **Heatsink overtemp protection** at 125C with 10C recovery hysteresis
- **Sensor failure detection** for both chamber and heatsink sensors
- **Data logging** to LittleFS with CSV download via web dashboard
- **16x2 LCD display** showing temperature, humidity, mode, relay state, and lid status
- **Web dashboard** with mode selector, live time-series charts, and data log management
- **REST API** for integration with home automation systems
- **Hardware watchdog** (15s timeout) with automatic recovery
- **Thermal fuse**  10A 133C on heater to cut power in case of relay fusing on
- **Fuse** 1A fuse on mains. 
## Circuit

```text
ESP32 Dev Module
 |
 |-- GPIO 13 ---------> Relay module (heater control)
 |
 |-- GPIO 27 ---------> Reed switch (lid detection, pulled up, active LOW)
 |
 |-- I2C0 (Wire) -----> HTU21D sensor (0x40) + 16x2 LCD (0x27)
 |     GPIO 19 (SDA)      (shared bus, different addresses)
 |     GPIO 18 (SCL)
 |
 |-- GPIO 22 (SDA) ---> LCD I2C backpack (directly wired to I2C0 bus)
 |-- GPIO 21 (SCL)
 |
 |-- GPIO 33 (ADC) ---> NTC thermistor voltage divider (heatsink)
```

### NTC Thermistor Circuit

```text
3V3 ---[10K fixed resistor]---+--- NTC 10K (MF52AT 3950) --- GND
                               |
                          GPIO 33
                               |
                          [100nF cap]
                               |
                              GND
```

- **Thermistor**: MF52AT 10K NTC, Beta 3950
- **Voltage divider**: 10K fixed resistor from 3V3 to ADC pin, NTC from ADC pin to GND
- **Filter cap**: 100nF ceramic between ADC pin and GND (noise filtering)
- **ADC**: Uses `analogReadMilliVolts()` for factory-calibrated readings via eFuse

### Components

| Component | Description |
| --- | --- |
| ESP32 Dev Module | Main controller (ESP32-D0WD-V3) |
| HTU21D | Chamber temperature and humidity sensor (I2C, 0x40) |
| NTC 10K MF52AT 3950 | Heatsink temperature sensor |
| 16x2 LCD + I2C backpack | Display (I2C, 0x27) |
| Reed switch | Lid open/closed detection (normally open, magnet closes) |
| Relay module | Heater control (SSR or mechanical) |
| 10K resistor | NTC voltage divider fixed resistor |
| 100nF capacitor | ADC input filter |
| 10A 133C Thermal fuse | on thermal elements |
| 1A Fuse | on mains power |

## Safety Layers

The firmware implements multiple independent safety layers, evaluated in priority order:

| Priority | Layer | Trigger | Action |
| --- | --- | --- | --- |
| 0 | Boot-time relay off | ESP32 powers on or resets | Relay forced LOW before any init |
| 1 | Lid open | Reed switch detects open lid | Relay OFF immediately |
| 2 | Thermal fault (latched) | Chamber temp not rising despite heating | Relay OFF, clears on mode change |
| 3 | Mode OFF | User selects OFF mode | Relay OFF |
| 4 | Heatsink sensor invalid | NTC reading outside -10C to 200C | Relay OFF immediately |
| 5 | Heatsink overtemp | Heatsink >= 125C | Relay OFF, recovers at < 115C |
| 6 | Consecutive sensor failure | 5 consecutive invalid readings (10s) | Relay forced OFF |
| 7 | Drying timer expiry | Drying duration elapsed | Auto-revert to MAINTAIN mode |
| 8 | Humidity gate (MAINTAIN mode) | Humidity <= 20% | Relay OFF until humidity > 22% (hysteresis) |
| 9 | Chamber bang-bang | Chamber temp vs setpoint | Normal heating control |
| 10 | Thermal fault check | Relay ON >3 min, chamber rise <1C | Latch thermal fault |
| 11 | Hardware watchdog | Main loop hangs > 15s | ESP32 hard reset (relay starts OFF) |

### Thermal Fault Detection

When the relay has been continuously ON for 3 minutes, the firmware checks whether the chamber temperature rose by at least 1.0C. If not, it latches a thermal fault and disables the heater. This catches the scenario where the lid is physically open but the reed switch still reads closed (e.g., magnet misalignment). The check is skipped when the chamber is within 10C of setpoint (relay cycling zone). After passing the check, tracking restarts from the current temp to catch mid-session lid opening. The fault clears only when the user changes mode.

**Not covered by firmware**: relay welding (SSR fails shorted). A thermal fuse on the heater element is recommended as hardware backstop.

## Operating Modes

| Mode | Setpoint | Duration | Behavior |
| --- | --- | --- | --- |
| OFF | -- | -- | Heater disabled |
| MAINTAIN | 40C | Indefinite | Humidity-gated: heats only when humidity > 22%, off when <= 20%. Safe for overnight use. |
| PLA | 45C | 4h | Timed drying, auto-reverts to MAINTAIN on expiry |
| PETG | 50C | 6h | Timed drying, longer to compensate for reduced temp |
| ABS | 52C | 8h | Timed drying, much longer to compensate for reduced temp |
| TPU | 50C | 5h | Timed drying, auto-reverts to MAINTAIN on expiry |
| MIX | 45C | 4h | Safe temperature for mixed spool types |

Default mode on boot is MAINTAIN.

## Web Interface

The ESP32 hosts a web interface on port 80 when connected to WiFi. The IP address is shown on the LCD at boot. The dashboard includes a mode selector grid, live time-series charts (temperature and humidity/relay), and data log management (download CSV, clear log).

### API Endpoints

- `GET /` - Web dashboard with live charts (auto-refreshes every 10s)
- `GET /api/status` - JSON status of all sensors, relay state, mode, and flags
- `POST /api/mode` - Set operating mode (body: `{"mode": "pla"}`)
- `POST /api/toggle` - Toggle between OFF and MAINTAIN (backward compat)
- `GET /api/log` - Download CSV data log
- `DELETE /api/log` - Clear data log
- `GET /api/log/stats` - Log file stats (record count, file size, disk free)

### Status Response

```json
{
  "chamber_temp": 42.5,
  "humidity": 28.3,
  "heatsink_temp": 65.2,
  "chamber_valid": true,
  "heatsink_valid": true,
  "relay_on": true,
  "overtemp": false,
  "setpoint": 45.0,
  "enabled": true,
  "lid_open": false,
  "thermal_fault": false,
  "mode": "pla",
  "drying_remaining": 14400
}
```

## LCD Display

Static 2-line layout, updated every 2s with in-place overwrite (no flicker):

```text
T:42.5 RH:28.3
HS:65.2 PLA ON L
```

- **Line 1**: Chamber temperature and humidity
- **Line 2**: Heatsink temp (`HOT`/`ERR` on fault), mode label (3 chars), relay state (`ON`/`--`), lid (`L`=closed, `O`=open)
- **Thermal fault override**: Line 2 shows `*THERMAL FAULT *` when fault is latched

## Building

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE plugin)

### Setup

1. Clone the repository
2. Copy `include/secrets.h.example` to `include/secrets.h`
3. Edit `include/secrets.h` with your WiFi credentials

### Compile and Upload

```bash
pio run              # compile
pio run -t upload    # flash to ESP32
pio device monitor   # serial output (115200 baud)
```

## Configuration

Key parameters in `include/config.h`:

| Parameter | Default | Description |
| --- | --- | --- |
| `CHAMBER_HYSTERESIS` | 2.0C | Bang-bang hysteresis |
| `HEATSINK_OVERTEMP` | 125.0C | Overtemp safety cutoff |
| `HEATSINK_HYSTERESIS` | 10.0C | Overtemp recovery hysteresis |
| `SENSOR_FAIL_MAX` | 5 | Consecutive failures before forced relay off |
| `SENSOR_READ_INTERVAL_MS` | 2000 | Sensor polling interval |
| `THERMAL_CHECK_PERIOD_MS` | 180000 | Time before thermal fault check (3 min) |
| `THERMAL_MIN_RISE` | 1.0C | Minimum expected chamber rise in check period |
| `THERMAL_NEAR_SETPOINT` | 10.0C | Skip thermal check within this range of setpoint |
| `MAINTAIN_HUMIDITY_TARGET` | 20.0% | Humidity threshold for maintain mode |
| `MAINTAIN_HUMIDITY_HYST` | 2.0% | Maintain mode humidity hysteresis |
| `MAINTAIN_TEMP_TARGET` | 40.0C | Heating target for maintain mode |
| `NTC_VOUT_SHORT_FLOOR` | 0.15V | ADC voltage floor for NTC short detection |

## Project Structure

```text
include/
  config.h          Pin definitions, NTC parameters, safety thresholds, mode presets
  sensors.h         Sensor API (chamber temp, humidity, heatsink temp)
  relay.h           Relay control API (modes, bang-bang, thermal fault, overtemp)
  display.h         LCD display API
  lid.h             Lid switch API
  datalog.h         Data logging API
  webserver.h       Web server API
  secrets.h.example WiFi credentials template
src/
  main.cpp          Setup, main loop, watchdog, sensor failure tracking
  sensors.cpp       HTU21D + NTC thermistor reading
  relay.cpp         Mode-based control with safety priority chain and thermal fault detection
  display.cpp       Static 2-line LCD layout with mode and fault display
  lid.cpp           Reed switch debounced reading
  datalog.cpp       LittleFS CSV logging with NTP timestamps
  webserver.cpp     HTTP server, REST API, web dashboard with charts
lib/
  LiquidCrystal_I2C/  LCD library with TwoWire& support
```

## License

[MIT](LICENSE)
