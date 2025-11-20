# Freenove ESP32-S3 WROOM (Camera)

## Overview
This board is based on the ESP32-S3-WROOM module and includes a camera interface.

## Specifications
- **Module**: ESP32-S3-WROOM
- **Flash**: Typically 8MB or 16MB (Quad SPI)
- **PSRAM**: Typically 8MB (Octal SPI)
- **USB**: Native USB-C (used for JTAG/Serial)

## Pinout
### Onboard LED
- **GPIO 48**: RGB LED (WS2812 or similar, but often just a single pin control for simple blinky if driven directly, or requires RMT/Bitbanging if it's a smart LED).
  - *Note: Research indicates GPIO 48. If it is a WS2812, simple high/low won't work for color, but might show activity. For a simple "Blinky" task, we will attempt to toggle it as a GPIO first. If it's a WS2812, we need a library.*
  - *Correction*: Many Freenove boards use a simple LED or a WS2812. If it's WS2812, we need the `led_strip` component. Let's assume it might be a WS2812 given "RGB" description.

### Camera
- **D0-D7**: Data lines
- **XCLK, PCLK, VSYNC, HREF**: Control lines
- **SDA/SCL**: I2C control for sensor

## Configuration
To use the full capabilities (PSRAM), ensure `CONFIG_ESP32S3_SPIRAM_SUPPORT=y` is set in `sdkconfig`.
