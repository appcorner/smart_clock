# Build Guide (GeekMagic SmallTV ESP8266 Firmware)

This guide contains complete instructions for compiling and flashing the firmware for the GeekMagic SmallTV board (ESP8266 ESP-12F).

---

## 📋 Prerequisites

### Required Hardware
- **GeekMagic SmallTV board** (ESP8266 ESP-12F + ST7789 240x240 display)
- **NodeMCU board** or other USB-to-Serial converter (for initial flashing)
- **Jumper wires** for connecting Tx/Rx/GND/5V/GPIO0 pins
- **USB-C cable** to power the board

### Required Software
Choose **one method** based on your preference:

#### Method 1: PlatformIO (Recommended for developers)
- **Python 3.7+** ([Download](https://www.python.org/downloads/))
- **PlatformIO Core** installed via pip:
  ```bash
  pip install platformio
  ```
- **esptool** (for serial flashing):
  ```bash
  pip install esptool
  ```

#### Method 2: Arduino IDE
- **Arduino IDE 2.x** ([Download](https://www.arduino.cc/en/software))
- **ESP8266 Board Package** via Board Manager (URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`)
- **Required libraries** (via Library Manager):
  - `ArduinoJson` (v6.x, not v7)
  - `Adafruit GFX Library`
  - `Adafruit ST7735 and ST7789 Library`
  - `Adafruit BusIO`

---

## 🔨 Method 1: Build with PlatformIO (Recommended)

### 1. Clone or download the project
```bash
git clone <repo-url>
cd smart_clock
```

### 2. Check platformio.ini
This file is already configured; no changes needed:
- Board: `esp12e` (ESP8266 ESP-12F)
- Flash: 4MB (1MB filesystem, 3MB sketch/OTA)
- CPU: 160 MHz
- All libraries download automatically

### 3. Compile the firmware
```bash
pio run
```

**Output files:**
- `.pio/build/geekmagic/firmware.bin` (raw file)
- `build_esp8266/SDP_v3.5.0.bin` (copied automatically by post-build script)

**Example successful output:**
```
RAM:   [=====     ]  49.2% (used 40284 bytes from 81920 bytes)
Flash: [=====     ]  48.4% (used 505551 bytes from 1044464 bytes)
Building .pio/build/geekmagic/firmware.bin
========================= [SUCCESS] Took 12.34 seconds =========================
```

### 4. Verify output file
```bash
ls -lh build_esp8266/SDP_v3.5.0.bin
```

---

## 🔨 Method 2: Build with Arduino IDE

### 1. Open the project
Open `smart_clock_esp8266/smart_clock_esp8266.ino` in Arduino IDE

### 2. Configure the board
**Tools → Board → ESP8266 Boards → Generic ESP8266 Module**

Set these options:
- **Flash Size:** `4MB (FS:1MB OTA:~1019KB)`
- **CPU Frequency:** `160 MHz`
- **Upload Speed:** `115200`
- **Port:** Select your NodeMCU's COM port (e.g., `COM3`)

### 3. Install libraries
**Tools → Manage Libraries...** then search and install:
- `ArduinoJson` by Benoit Blanchon (version 6.x)
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`
- `Adafruit BusIO`

### 4. Compile
**Sketch → Export compiled Binary** (Ctrl+Alt+S)

**Output file:**
- `smart_clock_esp8266.ino.bin` in the `smart_clock_esp8266/` folder

---

## 📥 Method 3: Download Pre-compiled Binary

If you don't want to compile yourself, download the ready-made binary from:

**GitHub Releases:** [https://github.com/<your-repo>/releases](change URL)

File: `SDP_v3.5.0.bin` (approximately 509 KB)

---

## 🔌 Hardware Connection for Flashing

### Pin Connection Table (NodeMCU Passthrough)

| Clock Board Pin | NodeMCU Pin | Notes |
|---|---|---|
| **TXD0** | **RX** (GPIO3) | |
| **RXD0** | **TX** (GPIO1) | |
| **GND** | **GND** | |
| **5V / VCC** | **VU / 5V** | |
| **GPIO0** | **GND** | ⚠️ **Hold only while powering on** to enter bootloader mode |
| *(NodeMCU only)* | **RST → GND** | ⚠️ **Important:** NodeMCU's RST must be tied to GND to halt that chip |

**Diagrams:** See `images/board_pins.jpg` and README.md

### Wiring Steps
1. Power off both boards first
2. Connect Tx/Rx/GND/5V wires per the table
3. Connect **clock board GPIO0 → GND** and hold
4. Connect **NodeMCU RST → GND** (if using NodeMCU)
5. Plug USB cable into computer (board enters bootloader mode)
6. Disconnect GPIO0 from GND after power-on (but keep NodeMCU RST tied to GND)

---

## ⚡ Flashing the Firmware

### Method 1: Initial Serial Flash

#### Using esptool (PlatformIO / Python)

**1. Check COM port**
```bash
# Windows
mode

# Linux/Mac
ls /dev/tty*
```

**2. Erase existing EEPROM** (recommended):
```bash
python -m esptool --chip esp8266 --port COM3 --baud 115200 erase_flash
```

**3. Flash new firmware:**
```bash
python -m esptool --chip esp8266 --port COM3 --baud 115200 write_flash 0x0 build_esp8266/SDP_v3.5.0.bin
```

**Example successful output:**
```
esptool.py v4.7.0
Serial port COM3
Connecting....
Chip is ESP8266EX
Features: WiFi
Crystal is 26MHz
...
Wrote 505552 bytes at 0x00000000 in 44.5 seconds (90.9 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

#### Using PlatformIO
```bash
pio run -t upload --upload-port COM3
```

#### Using Arduino IDE
1. Select the correct port in **Tools → Port**
2. Press **Upload** (Ctrl+U)

---

### Method 2: OTA (Over-The-Air) Update

**Requirements:**
- Board must already be connected to Wi-Fi
- Know the board's IP address (check the display or router)
- Know web page username/password (default: `admin` / `smartclock`)

**Steps:**
1. Open a web browser to `http://<device-ip>/` (e.g., `http://192.168.1.139/`)
2. Log in with username/password
3. Go to the **🚀 Update Firmware (OTA)** section
4. Click **Choose File** and select `SDP_v3.5.0.bin`
5. Click **⚡ Upload .bin**
6. Wait about 10-15 seconds; the board will restart with the new version

**Note:** If upload fails (timeout / connection lost), it may be because the file is too large or Wi-Fi signal is weak. Try again or use serial flashing.

---

## 🧪 Testing After Build/Flash

### 1. Check Serial Monitor
```bash
# PlatformIO
pio device monitor -p COM3 -b 115200

# Arduino IDE
Tools → Serial Monitor (set baud rate to 115200)
```

**Example correct output:**
```
[BOOT] ESP8266 started
[WIFI] Scanning...
[WIFI] Found 5 networks
[WIFI] Starting AP mode: SmartClock_XXXXXX
[WIFI] AP IP: 192.168.4.1
[LCD] ST7789 240x240 initialized
[NTP] Syncing time...
[NTP] Time synced: 2025-01-15 14:30:00
```

### 2. Check LCD Display
- Should see the clock face with digits
- If using default password, there will be a red `[!PW]` badge in the top-right corner
- Thai text must display correctly (vowels/tone marks stack properly)

### 3. Connect to Wi-Fi
- Connect to Wi-Fi network named `SmartClock_XXXXXX` (password: `smartclock`)
- Open browser to `http://192.168.4.1/`
- Configure your home/office Wi-Fi
- Board will restart and connect to main Wi-Fi automatically

### 4. Test Dashboard API (optional)
```bash
cd scripts
./test_dashboard.sh <device-ip> admin <password>
```

---

## 🔧 Common Troubleshooting

### 1. `pio run` fails with `CERTIFICATE_VERIFY_FAILED`

**Cause:** Antivirus/firewall software (Norton, Zscaler) intercepts TLS, causing certifi verification to fail

**Fix:**
```bash
python scripts/make_ca_bundle.py
export REQUESTS_CA_BUNDLE="$HOME/.platformio/win-ca-bundle.pem"  # Linux/Mac
# or
$env:REQUESTS_CA_BUNDLE = "$env:USERPROFILE\.platformio\win-ca-bundle.pem"  # PowerShell
```

Then run `pio run` again

---

### 2. esptool can't find chip / timeout

**Symptoms:**
```
Serial port COM3
Connecting........_____....._____
A fatal error occurred: Failed to connect to ESP8266
```

**Causes and fixes:**
- ❌ **Forgot to pull GPIO0 to GND while powering on** → Connect GPIO0→GND before plugging in USB, then disconnect after entering bootloader
- ❌ **NodeMCU still running** → NodeMCU RST must be tied to GND throughout
- ❌ **Wrong COM port selected** → Use `mode` (Windows) or `ls /dev/tty*` (Linux) to check again
- ❌ **Bad USB cable** → Try another cable (must be a data cable, not charge-only)

---

### 3. Compiles successfully but display doesn't turn on / shows only white

**Causes and fixes:**
- ❌ **LCD wired to wrong pins** → Check against [LCD pin table](#-hardware-connection-for-flashing) in README
- ❌ **SPI Mode 3 not set** → Code must have `tft.init(240, 240, SPI_MODE3)`
- ❌ **Backlight not on** → Backlight is Active LOW, must use `digitalWrite(TFT_BL, LOW)` to turn on
- ❌ **Backlight too dim** → Firmware should have `analogWriteRange(1023)` in `setup()` (fixed in v3.4.0+)

---

### 4. Compiles successfully but RAM / Flash exceeds 100%

**Symptoms:**
```
RAM:   [==========] 102.3% (used 83844 bytes from 81920 bytes)
Flash: [==========] 101.2% (used 1057234 bytes from 1044464 bytes)
```

**Fixes:**
- 🔹 Reduce program size: disable unused features (e.g., debug logs)
- 🔹 Change flash layout in `platformio.ini`:
  ```ini
  board_build.ldscript = eagle.flash.4m2m.ld  # FS 2MB, Sketch 2MB
  ```
- 🔹 Check for large files embedded (e.g., image hex arrays in `.h` files)

---

### 5. OTA upload fails / timeout

**Causes and fixes:**
- ❌ **Weak Wi-Fi signal** → Move closer to router or use USB serial flashing instead
- ❌ **File too large** → Check that .bin file is under ~800 KB (currently ~509 KB is normal)
- ❌ **Wrong password** → Verify username/password are correct
- ❌ **Not enough heap** → Wait for board to rest (multiple refresh data calls can exhaust heap), then try again

---

## 📚 References

- **README.md / README.en.md** — Main project documentation
- **docs/API_DRAW_SPEC.md** — API documentation for `/api/draw`
- **platformio.ini** — Build configuration
- **scripts/** — All helper scripts

---

## 📞 Getting Help

If you encounter problems not covered in this documentation:
1. Check **Serial Monitor** output for error messages
2. Open an issue on the GitHub repository with logs attached
3. Include:
   - Firmware version you're trying to flash
   - Build method (PlatformIO / Arduino IDE / Pre-compiled)
   - Operating system (Windows / Linux / Mac)
   - Full error message

---

*This document covers building firmware v3.5.0 — Last updated: 2026-08-11*
