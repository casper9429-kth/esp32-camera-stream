# ESP32-S3 Camera Streaming with Wi-Fi Provisioning

A complete camera streaming solution for the **Freenove ESP32-S3 WROOM** board with web-based Wi-Fi provisioning.

## Features

- 📷 **MJPEG Camera Streaming** - Real-time video streaming over HTTP
- 🌐 **Web-based Wi-Fi Provisioning** - Easy setup via captive portal
- 💾 **Persistent Credentials** - Saves Wi-Fi settings to NVS
- 🎨 **LED Status Indicators** - Visual feedback for device state
- 🔄 **Auto-reconnect** - Automatically connects to saved Wi-Fi on boot

## Hardware Requirements

- Freenove ESP32-S3 WROOM board with camera
- USB-C cable for programming

## Quick Start

### 1. Install ESP-IDF

```bash
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.1.2 esp-idf
cd esp-idf
./install.sh
```

### 2. Build and Flash

```bash
. ./esp-idf/export.sh
idf.py build
idf.py flash monitor
```

### 3. First-Time Setup (Provisioning Mode)

On first boot, the device creates a Wi-Fi access point:

1. **Connect** to Wi-Fi network: `ESP32-Setup` (no password)
2. **Open browser** to: `http://192.168.4.1`
3. **Click "Scan Networks"** to see available Wi-Fi networks
4. **Select your network** and enter password
5. **Click "Connect"** - device will save credentials and reboot

**LED Indicator**: Blue = Provisioning Mode

### 4. Streaming Mode

After provisioning, the device automatically connects to your Wi-Fi:

1. Check the serial monitor for the assigned IP address (e.g., `192.168.0.111`)
2. Open browser to: `http://<IP_ADDRESS>/stream`
3. View live camera feed!

**LED Indicator**: Green = Connected to Wi-Fi

### 5. Reset Wi-Fi Settings

To change Wi-Fi networks:
- Visit: `http://<IP_ADDRESS>/reset`
- Device will clear credentials and reboot into Provisioning Mode

## LED Status Indicators

- 🔵 **Blue** - Provisioning Mode (waiting for Wi-Fi setup)
- 🟢 **Green** - Connected to Wi-Fi (streaming mode)
- 🔴 **Red** - Camera initialization failed

## Project Structure

```
.
├── CMakeLists.txt          # Root build configuration
├── main/
│   ├── CMakeLists.txt      # Component build configuration
│   ├── main.c              # Main application code
│   └── idf_component.yml   # Component dependencies
├── docs/
│   └── freenove_esp32s3.md # Board documentation
├── sdkconfig.defaults      # Default SDK configuration
└── esp-idf/                # ESP-IDF framework (local)
```

## Technical Details

### Camera Configuration
- **Resolution**: VGA (640x480)
- **Format**: JPEG
- **Frame Buffer**: PSRAM
- **Streaming Protocol**: MJPEG over HTTP

### Wi-Fi Modes
- **Provisioning**: SoftAP mode (ESP32-Setup)
- **Streaming**: Station mode (connects to your Wi-Fi)

### Dependencies
- `esp32-camera` - Camera driver
- `esp_http_server` - Web server
- `esp_wifi` - Wi-Fi functionality
- `nvs_flash` - Non-volatile storage
- `led_strip` - RGB LED control
- `json` - JSON parsing for network scan

## Troubleshooting

**Camera Init Failed (Red LED)**
- Check camera connections
- Verify board is Freenove ESP32-S3 WROOM

**Can't connect to ESP32-Setup**
- Ensure device is in Provisioning Mode (Blue LED)
- Try forgetting and reconnecting to the network

**No video stream**
- Check IP address in serial monitor
- Ensure you're on the same Wi-Fi network
- Try accessing `http://<IP>/reset` to reconfigure

## License

MIT License - Feel free to use and modify!
