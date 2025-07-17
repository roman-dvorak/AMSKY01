# AFSKY01 Hygrometer Firmware

## Overview

The AFSKY01 is a precision hygrometer device based on the Raspberry Pi Pico (RP2040) microcontroller. It measures temperature and humidity using the SHT45 sensor and provides real-time data output via USB-CDC serial communication.

## Features

- **Temperature and Humidity Measurement**: High-precision readings using SHT45 sensor
- **I2C Communication**: Custom I2C1 pins (GPIO 18/19) for sensor communication
- **USB-CDC Serial Output**: Real-time data streaming in CSV format
- **Dual LED Indicators**:
  - CPU Status LED (GPIO 22): Smooth PWM breathing effect
  - Trigger Output LED (GPIO 27): 1Hz blinking
- **CSV Data Format**: `hygro;<temperature>;<humidity>`

## Hardware Specifications

- **Microcontroller**: Raspberry Pi Pico (RP2040)
- **Sensor**: SHT45 temperature and humidity sensor
- **I2C Interface**: Custom I2C1 on GPIO 18 (SDA) and GPIO 19 (SCL)
- **Power**: USB-powered with optional power detection on GPIO 28 (ADC2)
- **Communication**: USB-CDC serial at 115200 baud

## Pin Configuration

| Pin | Function | Description |
|-----|----------|-------------|
| GPIO 18 | I2C1 SDA | Sensor data line |
| GPIO 19 | I2C1 SCL | Sensor clock line |
| GPIO 22 | CPU Status LED | PWM breathing effect |
| GPIO 27 | Trigger Output LED | 1Hz blinking |
| GPIO 28 | Power Detection | ADC2 for USB power detection |

## Software Dependencies

- PlatformIO framework
- Arduino-pico (earlephilhower core)
- Adafruit SHT4x Library (^1.0.4)
- Adafruit Unified Sensor (^1.1.14)

## Building and Flashing

1. Install PlatformIO
2. Clone the repository
3. Build and upload:
   ```bash
   platformio run --target upload
   ```

## Serial Output Format

The device outputs data in CSV format via USB-CDC serial (115200 baud):

### Startup Information
```
AFSKY01 Hygrometer
HW Version: 1.0
FW Version: 1.0.0
```

### Sensor Data (every 2 seconds)
```
hygro;29.64;35.21
hygro;29.63;35.09
hygro;29.66;34.99
```

Format: `hygro;<temperature_celsius>;<relative_humidity_percent>`

### Error Handling
```
hygro;ERROR;ERROR
```

## Configuration

- **Measurement Interval**: 2 seconds
- **CPU LED Breathing Period**: 2 seconds
- **Trigger LED Blink Rate**: 1 Hz
- **Sensor Precision**: High precision mode
- **Sensor Heater**: Disabled

## Version Information

- **Hardware Version**: 1.0
- **Firmware Version**: 1.0.0
- **Framework**: Arduino-pico (earlephilhower)
- **Platform**: Raspberry Pi RP2040

## License

This project is part of the ASTROMETERS project series.
