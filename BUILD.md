# คู่มือการ Build Firmware (GeekMagic SmallTV ESP8266)

คู่มือนี้รวบรวมขั้นตอนการคอมไพล์และแฟลช firmware สำหรับบอร์ด GeekMagic SmallTV (ESP8266 ESP-12F) ไว้อย่างครบถ้วน

---

## 📋 ข้อกำหนดเบื้องต้น (Prerequisites)

### ฮาร์ดแวร์ที่จำเป็น
- **บอร์ด GeekMagic SmallTV** (ESP8266 ESP-12F + จอ ST7789 240x240)
- **บอร์ด NodeMCU** หรือ USB-to-Serial converter อื่นๆ (สำหรับแฟลชครั้งแรก)
- **สายจั๊มเปอร์** สำหรับต่อพิน Tx/Rx/GND/5V/GPIO0
- **สาย USB-C** สำหรับจ่ายไฟให้บอร์ด

### ซอฟต์แวร์ที่จำเป็น
เลือก **วิธีใดวิธีหนึ่ง** ตามความถนัด:

#### วิธีที่ 1: PlatformIO (แนะนำสำหรับนักพัฒนา)
- **Python 3.7+** ([ดาวน์โหลด](https://www.python.org/downloads/))
- **PlatformIO Core** ติดตั้งผ่าน pip:
  ```bash
  pip install platformio
  ```
- **esptool** (สำหรับแฟลชผ่านสาย):
  ```bash
  pip install esptool
  ```

#### วิธีที่ 2: Arduino IDE
- **Arduino IDE 2.x** ([ดาวน์โหลด](https://www.arduino.cc/en/software))
- **ESP8266 Board Package** ติดตั้งผ่าน Board Manager (URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`)
- **ไลบรารีที่จำเป็น** (ติดตั้งผ่าน Library Manager):
  - `ArduinoJson` (v6.x, ไม่ใช่ v7)
  - `Adafruit GFX Library`
  - `Adafruit ST7735 and ST7789 Library`
  - `Adafruit BusIO`

---

## 🔨 วิธีที่ 1: Build ด้วย PlatformIO (แนะนำ)

### 1. โคลนหรือดาวน์โหลดโปรเจกต์
```bash
git clone <repo-url>
cd smart_clock
```

### 2. ตรวจสอบไฟล์ platformio.ini
ไฟล์นี้มีการตั้งค่าไว้ครบแล้ว ไม่ต้องแก้อะไร:
- Board: `esp12e` (ESP8266 ESP-12F)
- Flash: 4MB (1MB filesystem, 3MB sketch/OTA)
- CPU: 160 MHz
- ไลบรารีทั้งหมดดาวน์โหลดอัตโนมัติ

### 3. คอมไพล์ firmware

**คำสั่งพื้นฐาน:**
```bash
pio run
```

**คำสั่งเสริม:**
```bash
# Clean ก่อน build (แนะนำถ้าเคยมีปัญหา cache)
pio run -t clean
pio run

# Build พร้อม verbose output (ดู log ละเอียด)
pio run -v
```

**ผลลัพธ์ที่ได้:**
- ไฟล์ `.pio/build/geekmagic/firmware.bin` (ไฟล์ดิบ)
- ไฟล์ `build_esp8266/SDP_v<version>.bin` (คัดลอกโดย post-build script อัตโนมัติ)
  - เลขเวอร์ชันอ่านจาก `#define FW_VERSION` ในไฟล์ `.ino` บรรทัดที่ 17

**ตัวอย่าง output ที่สำเร็จ:**
```
RAM:   [=====     ]  49.2% (used 40284 bytes from 81920 bytes)
Flash: [=====     ]  48.5% (used 506191 bytes from 1044464 bytes)
Building .pio/build/geekmagic/firmware.bin
after_build([".pio\build\geekmagic\firmware.bin"], [".pio\build\geekmagic\firmware.elf"])
[copy_firmware] -> D:\sandboxs\smart_clock\build_esp8266\SDP_v3.5.3.bin (498.4 KB)
========================= [SUCCESS] Took 9.59 seconds =========================
```

### 4. ตรวจสอบไฟล์ output
```bash
ls -lh build_esp8266/SDP_v*.bin
```

**หมายเหตุสำคัญ:**
- ⚠️ ตรวจสอบ `git status` ก่อน build ทุกครั้ง เพื่อให้แน่ใจว่า binary ตรงกับ source code ที่ต้องการ
- ⚠️ ถ้าแก้โค้ดด้วยตัวเอง ควร commit ก่อน build เพื่อให้มี snapshot ที่ตรวจสอบได้
- ⚠️ แนะนำให้ clean build ถ้าเปลี่ยน session หรือเคยมี binary ที่มีปัญหา

---

## 🔨 วิธีที่ 2: Build ด้วย Arduino IDE

### 1. เปิดโปรเจกต์
เปิดไฟล์ `smart_clock_esp8266/smart_clock_esp8266.ino` ใน Arduino IDE

### 2. ตั้งค่าบอร์ด
**Tools → Board → ESP8266 Boards → Generic ESP8266 Module**

ตั้งค่าดังนี้:
- **Flash Size:** `4MB (FS:1MB OTA:~1019KB)`
- **CPU Frequency:** `160 MHz`
- **Upload Speed:** `115200`
- **Port:** เลือก COM port ของ NodeMCU (เช่น `COM3`)

### 3. ติดตั้งไลบรารี
**Tools → Manage Libraries...** แล้วค้นหาและติดตั้ง:
- `ArduinoJson` by Benoit Blanchon (เวอร์ชัน 6.x)
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`
- `Adafruit BusIO`

### 4. คอมไพล์
**Sketch → Export compiled Binary** (Ctrl+Alt+S)

**ผลลัพธ์ที่ได้:**
- `smart_clock_esp8266.ino.bin` ในโฟลเดอร์ `smart_clock_esp8266/`

---

## 📥 วิธีที่ 3: ดาวน์โหลด Pre-compiled Binary

ถ้าไม่ต้องการคอมไพล์เอง ดาวน์โหลดไฟล์สำเร็จรูปจาก:

**GitHub Releases:** [https://github.com/<your-repo>/releases](เปลี่ยน URL)

ไฟล์: `SDP_v3.5.0.bin` (ประมาณ 509 KB)

---

## 🔌 การเชื่อมต่อฮาร์ดแวร์สำหรับแฟลช

### ตารางการต่อพิน (NodeMCU Passthrough)

| ขาบนบอร์ดนาฬิกา | ขาบน NodeMCU | หมายเหตุ |
|---|---|---|
| **TXD0** | **RX** (GPIO3) | |
| **RXD0** | **TX** (GPIO1) | |
| **GND** | **GND** | |
| **5V / VCC** | **VU / 5V** | |
| **GPIO0** | **GND** | ⚠️ **ต่อค้างขณะจ่ายไฟเท่านั้น** เพื่อเข้า bootloader mode |
| *(NodeMCU เท่านั้น)* | **RST → GND** | ⚠️ **สำคัญ:** ต้องต่อ RST ของ NodeMCU ลง GND เพื่อหยุดชิปตัวนั้น |

**รูปประกอบ:** ดูที่ `images/board_pins.jpg` และ README.md

### ขั้นตอนการต่อสาย
1. ปิดไฟทั้งสองบอร์ดก่อน
2. ต่อสาย Tx/Rx/GND/5V ตามตาราง
3. ต่อ **GPIO0 ของบอร์ดนาฬิกา → GND** ค้างไว้
4. ต่อ **RST ของ NodeMCU → GND** (ถ้าใช้ NodeMCU)
5. เสียบสาย USB เข้าคอมพิวเตอร์ (บอร์ดจะเข้า bootloader mode)
6. ถอด GPIO0 ออกจาก GND หลังจ่ายไฟแล้ว (แต่ปล่อย NodeMCU RST ต่อ GND ไว้)

---

## ⚡ การแฟลช Firmware

### วิธีที่ 1: แฟลชครั้งแรกผ่านสาย (Serial Flash)

#### ด้วย esptool (PlatformIO / Python)

**1. ตรวจสอบ COM port**
```bash
# Windows
mode

# Linux/Mac
ls /dev/tty*
```

**2. ล้าง EEPROM เดิม** (แนะนำ):
```bash
python -m esptool --chip esp8266 --port COM3 --baud 115200 erase_flash
```

**3. แฟลช firmware ใหม่:**
```bash
python -m esptool --chip esp8266 --port COM3 --baud 115200 write_flash 0x0 build_esp8266/SDP_v3.5.0.bin
```

**ตัวอย่าง output ที่สำเร็จ:**
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

#### ด้วย PlatformIO
```bash
pio run -t upload --upload-port COM3
```

#### ด้วย Arduino IDE
1. เลือก port ที่ถูกต้องใน **Tools → Port**
2. กด **Upload** (Ctrl+U)

---

### วิธีที่ 2: อัปเดตผ่าน OTA (Over-The-Air)

**เงื่อนไข:**
- บอร์ดต้องเชื่อมต่อ Wi-Fi อยู่แล้ว
- รู้ IP address ของบอร์ด (ดูจากจอหรือ router)
- รู้ username/password หน้าเว็บ (ค่าเริ่มต้น: `admin` / `smartclock`)

**ขั้นตอน:**
1. เปิดเว็บเบราว์เซอร์ไปที่ `http://<device-ip>/` (เช่น `http://192.168.1.139/`)
2. ล็อกอินด้วย username/password
3. ไปที่หัวข้อ **🚀 อัปเดต Firmware (OTA)**
4. กด **Choose File** เลือก `SDP_v3.5.0.bin`
5. กด **⚡ อัปโหลด .bin**
6. รอประมาณ 10-15 วินาที บอร์ดจะรีสตาร์ทเป็นเวอร์ชันใหม่

**หมายเหตุ:** ถ้าอัปโหลดไม่สำเร็จ (timeout / connection lost) อาจเป็นเพราะไฟล์ใหญ่เกินไป หรือ Wi-Fi สัญญาณอ่อน ให้ลองใหม่หรือใช้วิธีแฟลชผ่านสาย

---

## 🧪 ทดสอบหลัง Build/Flash

### 1. ตรวจสอบ Serial Monitor
```bash
# PlatformIO
pio device monitor -p COM3 -b 115200

# Arduino IDE
Tools → Serial Monitor (ตั้ง baud rate 115200)
```

**ตัวอย่าง output ที่ถูกต้อง:**
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

### 2. ตรวจสอบหน้าจอ LCD
- ควรเห็นหน้านาฬิกาพร้อมตัวเลข
- ถ้าใช้รหัสผ่านเริ่มต้นจะมีแถบ `[!PW]` สีแดงมุมขวาบน
- ข้อความไทยต้องแสดงผลถูกต้อง (สระ/วรรณยุกต์ซ้อนกันได้)

### 3. เชื่อมต่อ Wi-Fi
- เชื่อมต่อกับ Wi-Fi ชื่อ `SmartClock_XXXXXX` (password: `smartclock`)
- เปิดเบราว์เซอร์ไปที่ `http://192.168.4.1/`
- ตั้งค่า Wi-Fi ของบ้าน/ออฟฟิศ
- บอร์ดจะรีสตาร์ทและเชื่อมต่อเข้า Wi-Fi หลักโดยอัตโนมัติ

### 4. ทดสอบ Dashboard API (ถ้าต้องการ)
```bash
cd scripts
./test_dashboard.sh <device-ip> admin <password>
```

---

## 🔧 แก้ปัญหาที่เจอบ่อย

### 1. `pio run` ล้มเหลวด้วย `CERTIFICATE_VERIFY_FAILED`

**สาเหตุ:** โปรแกรม antivirus/firewall (Norton, Zscaler) ดักจับ TLS ทำให้ certifi ตรวจสอบใบรับรองไม่ผ่าน

**วิธีแก้:**
```bash
python scripts/make_ca_bundle.py
export REQUESTS_CA_BUNDLE="$HOME/.platformio/win-ca-bundle.pem"  # Linux/Mac
# หรือ
$env:REQUESTS_CA_BUNDLE = "$env:USERPROFILE\.platformio\win-ca-bundle.pem"  # PowerShell
```

แล้วรัน `pio run` ใหม่

---

### 2. esptool หาชิปไม่เจอ / timeout

**อาการ:**
```
Serial port COM3
Connecting........_____....._____
A fatal error occurred: Failed to connect to ESP8266
```

**สาเหตุและวิธีแก้:**
- ❌ **ลืมดึง GPIO0 ลง GND ขณะจ่ายไฟ** → ต้องต่อ GPIO0→GND ก่อนเสียบ USB แล้วค่อยถอดหลังเข้า bootloader
- ❌ **NodeMCU ยังทำงานอยู่** → ต้องต่อ NodeMCU RST→GND ค้างไว้ตลอด
- ❌ **เลือก COM port ผิด** → ใช้ `mode` (Windows) หรือ `ls /dev/tty*` (Linux) เช็คใหม่
- ❌ **สาย USB เสีย** → ลองสายเส้นอื่น (ต้องเป็นสาย Data ไม่ใช่สายชาร์จอย่างเดียว)

---

### 3. คอมไพล์ผ่าน แต่จอไม่ติด / ขึ้นแต่สีขาว

**สาเหตุและวิธีแก้:**
- ❌ **ต่อสาย LCD ผิดพิน** → ตรวจตาม [ตารางพิน LCD](#-การเชื่อมต่อฮาร์ดแวร์สำหรับแฟลช) ใน README
- ❌ **ไม่ได้ตั้ง SPI Mode 3** → ตรวจโค้ดต้องมี `tft.init(240, 240, SPI_MODE3)`
- ❌ **Backlight ไม่ติด** → Backlight เป็น Active LOW ต้อง `digitalWrite(TFT_BL, LOW)` ถึงจะสว่าง
- ❌ **Backlight มืดเกินไป** → firmware ควรมี `analogWriteRange(1023)` ใน `setup()` (แก้ไขแล้วใน v3.4.0+)

---

### 4. คอมไพล์ผ่าน แต่ RAM / Flash เกิน 100%

**อาการ:**
```
RAM:   [==========] 102.3% (used 83844 bytes from 81920 bytes)
Flash: [==========] 101.2% (used 1057234 bytes from 1044464 bytes)
```

**วิธีแก้:**
- 🔹 ลดขนาดโปรแกรม: ปิด feature ที่ไม่ใช้ (เช่น debug log)
- 🔹 เปลี่ยน Flash layout ใน `platformio.ini`:
  ```ini
  board_build.ldscript = eagle.flash.4m2m.ld  # FS 2MB, Sketch 2MB
  ```
- 🔹 ตรวจสอบว่าไม่มีไฟล์ใหญ่ติดมา (เช่น รูปภาพ hex array ใน `.h`)

---

### 5. OTA อัปโหลดไม่ได้ / timeout

**สาเหตุและวิธีแก้:**
- ❌ **Wi-Fi สัญญาณอ่อน** → เข้าใกล้ router หรือใช้สาย USB แฟลชแทน
- ❌ **ไฟล์ .bin ใหญ่เกินไป** → ตรวจว่าไฟล์ไม่เกิน ~800 KB (ปัจจุบัน ~509 KB ปกติ)
- ❌ **รหัสผ่านผิด** → ตรวจ username/password ให้ถูก
- ❌ **Heap ไม่พอ** → รอให้บอร์ดพักสักครู่ (กด refresh data หลายครั้งทำให้ heap หมด) แล้วลองใหม่

---

## 📚 อ้างอิง

- **README.md / README.en.md** — คู่มือหลักของโปรเจกต์
- **docs/API_DRAW_SPEC.md** — เอกสาร API สำหรับ `/api/draw`
- **platformio.ini** — ตั้งค่า build
- **scripts/** — สคริปต์เสริมทั้งหมด

---

## 📞 ขอความช่วยเหลือ

หากประสบปัญหาที่ไม่มีในเอกสารนี้:
1. ตรวจสอบ **Serial Monitor** output ว่ามี error message อะไร
2. เปิด issue ที่ GitHub repository พร้อมแนบ log
3. ระบุ:
   - เวอร์ชัน firmware ที่พยายามแฟลช
   - วิธีการ build (PlatformIO / Arduino IDE / Pre-compiled)
   - ระบบปฏิบัติการ (Windows / Linux / Mac)
   - ข้อความ error ทั้งหมด

---

*เอกสารนี้ครอบคลุมการ build firmware v3.5.0 — อัปเดตล่าสุด: 2026-08-11*
