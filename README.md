# xkem-filament-dryer-esp32

ESP32 firmware for a DIY filament dryer with bang-bang temperature control, web interface, and multi-layer safety protection.

## Features

- **Bang-bang chamber temperature control** with 2C hysteresis (30-60C range)
- **Heatsink overtemp protection** at 125C with 10C recovery hysteresis
- **Sensor failure detection** for both chamber and heatsink sensors
- **16x2 LCD display** showing temperature, humidity, heatsink status, and relay state
- **Web interface** with real-time status, setpoint adjustment, and enable/disable toggle
- **REST API** for integration with home automation systems
- **Hardware watchdog** (15s timeout) with automatic recovery
- **Dual I2C buses** to prevent LCD artefacting from bus contention

## Circuit

```text
ESP32 Dev Module
 |
 |-- GPIO 13 ---------> Relay module (heater control)
 |
 |-- I2C0 (Wire) -----> HTU21D temperature/humidity sensor (chamber)
 |     GPIO 19 (SDA)
 |     GPIO 18 (SCL)
 |
 |-- I2C1 (Wire1) ----> 16x2 LCD with I2C backpack (address 0x27)
 |     GPIO 22 (SDA)
 |     GPIO 21 (SCL)
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
| Relay module | Heater control (SSR or mechanical) |
| 10K resistor | NTC voltage divider fixed resistor |
| 100nF capacitor | ADC input filter |

## Safety Layers

The firmware implements multiple independent safety layers, evaluated in priority order:

| Priority | Layer | Trigger | Action |
| --- | --- | --- | --- |
| 0 | Boot-time relay off | ESP32 powers on or resets | Relay forced LOW before any init |
| 1 | Heatsink sensor invalid | NTC reading outside -10C to 200C | Relay OFF immediately |
| 2 | Heatsink overtemp | Heatsink >= 125C | Relay OFF, recovers at < 115C |
| 3 | Consecutive heatsink failure | 5 consecutive invalid readings (10s) | Relay forced OFF |
| 4 | Consecutive chamber failure | 5 consecutive invalid readings (10s) | Relay forced OFF |
| 5 | User disable | Toggle via web interface | Relay OFF |
| 6 | Chamber bang-bang | Chamber temp vs setpoint | Normal heating control |
| 7 | Hardware watchdog | Main loop hangs > 15s | ESP32 hard reset (relay starts OFF) |

**Not covered by firmware**: relay welding (SSR fails shorted). A thermal fuse on the heater element is recommended as hardware backstop.

## Web Interface

The ESP32 hosts a web interface on port 80 when connected to WiFi. The IP address is shown on the LCD at boot.

### API Endpoints

- `GET /` - Web dashboard (auto-refreshes every 60s)
- `GET /api/status` - JSON status of all sensors, relay state, setpoint, and flags
- `POST /api/setpoint` - Set target temperature (body: `{"setpoint": 45.0}`, range: 30-60C)
- `POST /api/toggle` - Toggle dryer enabled/disabled

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
  "enabled": true
}
```

## LCD Display

Static 2-line layout, updated every loop iteration with in-place overwrite (no flicker):

```text
T:42.5 RH:28.3
HS:65.2 R:ON
```

Heatsink status shows temperature when OK, `HOT` on overtemp, `ERR` on sensor failure.

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
| `CHAMBER_SETPOINT_DEFAULT` | 50.0C | Default target temperature |
| `CHAMBER_SETPOINT_MIN` | 30.0C | Minimum allowed setpoint |
| `CHAMBER_SETPOINT_MAX` | 60.0C | Maximum allowed setpoint |
| `CHAMBER_HYSTERESIS` | 2.0C | Bang-bang hysteresis |
| `HEATSINK_OVERTEMP` | 125.0C | Overtemp safety cutoff |
| `HEATSINK_HYSTERESIS` | 10.0C | Overtemp recovery hysteresis |
| `SENSOR_FAIL_MAX` | 5 | Consecutive failures before forced relay off |
| `SENSOR_READ_INTERVAL_MS` | 2000 | Sensor polling interval |

## Project Structure

```text
include/
  config.h          Pin definitions, NTC parameters, safety thresholds
  sensors.h         Sensor API (chamber temp, humidity, heatsink temp)
  relay.h           Relay control API (bang-bang, overtemp, enable/disable)
  display.h         LCD display API
  webserver.h       Web server API
  secrets.h.example WiFi credentials template
src/
  main.cpp          Setup, main loop, watchdog, sensor failure tracking
  sensors.cpp       HTU21D + NTC thermistor reading
  relay.cpp         Bang-bang control with safety priority chain
  display.cpp       Static 2-line LCD layout
  webserver.cpp     HTTP server, REST API, web dashboard
lib/
  LiquidCrystal_I2C/  Patched LCD library with TwoWire& support for dual I2C
```

## License

[MIT](LICENSE)
