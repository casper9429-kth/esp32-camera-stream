# ESP32-S3 Memory Architecture

## Memory Types

### 1. SRAM (Internal RAM) - ~512KB Total
**Location**: Inside the ESP32-S3 chip  
**Speed**: Fastest memory

#### DRAM (Data RAM) - ~320KB
- **Purpose**: Variables, heap allocation, stack
- **Access**: Direct CPU access
- **Usage**: General program data, small buffers

#### IRAM (Instruction RAM) - ~128KB  
- **Purpose**: Frequently-executed code
- **Access**: Direct CPU access
- **Usage**: Interrupt handlers, critical functions

### 2. PSRAM (External RAM) - 8MB (on Freenove board)
**Location**: External chip connected via Octal SPI  
**Speed**: Slower than SRAM but much larger  
**Purpose**: Large data structures, camera frame buffers  
**Access**: Via `MALLOC_CAP_SPIRAM` capability  
**Note**: Essential for camera operation at higher resolutions

### 3. Flash (Program Storage) - 8-16MB
**Location**: External SPI flash chip  
**Speed**: Slowest, read-only at runtime  
**Purpose**: Program code, constants, file system  
**Note**: Code is cached in IRAM for faster execution

### 4. RTC Memory - 8KB
**Location**: RTC power domain  
**Speed**: Moderate  
**Purpose**: Data persistence during deep sleep  
**Special**: Retains data when main CPU is powered off

## System Stats Page

Access the stats page at: `http://<ESP32_IP>/stats`

### Displayed Information

**📊 Memory Usage**
- PSRAM Total/Used (with progress bar)
- DRAM Total/Used (with progress bar)
- Color-coded warnings (green < 60%, yellow < 80%, red >= 80%)

**💻 CPU Usage**
- Real-time CPU utilization percentage
- Active task count
- Based on FreeRTOS runtime statistics

**📡 Wi-Fi Status**
- Connected SSID
- Signal strength (RSSI in dBm)
- Channel number

**⚙️ System Info**
- Uptime (formatted as days/hours/minutes/seconds)
- Current free heap
- Minimum free heap (lowest point since boot)

### API Endpoint

**GET `/stats_json`** - Returns JSON with all system stats

Example response:
```json
{
  "memory": {
    "psram_total": 8388608,
    "psram_free": 7340032,
    "psram_used": 1048576,
    "dram_total": 327680,
    "dram_free": 245760,
    "dram_used": 81920
  },
  "cpu": {
    "usage_percent": 15,
    "task_count": 12
  },
  "wifi": {
    "ssid": "YourNetwork",
    "rssi": -45,
    "channel": 11
  },
  "system": {
    "uptime_ms": 123456,
    "free_heap": 245760,
    "min_free_heap": 230400
  }
}
```
