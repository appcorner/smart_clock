# GeekMagic SmallTV ESP8266 Custom Firmware (v3.5.0)

โปรเจกต์เฟิร์มแวร์ C++ Native สำหรับอุปกรณ์ **GeekMagic SmallTV** (ใช้ชิป ESP8266 ESP-12F และหน้าจอ ST7789 IPS 240x240 LCD) ปรับปรุงให้รองรับการแสดงผลภาษาไทยแบบจัดระเบียบสระ พร้อมระบบควบคุม Wi-Fi Manager, ดึงข้อมูลสภาพอากาศและราคาทองจาก API จริง, dashboard widget หลายชนิด (รวม QR/PromptPay), และอัปเดตแบบไร้สาย (OTA) ผ่านหน้าเว็บ

---

## 📌 คุณสมบัติเด่น (Features)
- 🇹🇭 **ระบบฟอนต์ภาษาไทยแบบสมบูรณ์ (178 Glyphs):** รองรับวรรณยุกต์และสระบน/ล่าง (Combining Marks) ซ้อนตำแหน่งพยัญชนะได้ถูกต้อง และแสดงผลได้หลายขนาด (Scale 1x-3x ตามชนิด widget)
- 📶 **Smart Wi-Fi Manager (EEPROM):** สแกนหา Wi-Fi และตั้งค่าผ่านเว็บ ตัวบอร์ดจำค่าถาวรลง EEPROM (ปิด AP Mode อัตโนมัติเมื่อเชื่อมต่อสำเร็จ)
- 🕒 **NTP Time Sync:** เวลาไทย UTC+7 นิ่งสนิท ปรับเวลาตัวเลขใหญ่ชัดเจน
- 🥇 **XAU/USD Gold Widget:** ดึงราคาทองจาก API จริงทุก 5 นาที สีเขียวเมื่อราคาสูงกว่ารอบก่อน สีแดงเมื่อต่ำกว่า
- 🌤️ **สภาพอากาศจาก API จริง:** อุณหภูมิและสภาพอากาศภาษาไทยจาก open-meteo ทุก 10 นาที (แปลงชื่อเมืองเป็นพิกัดอัตโนมัติ พร้อม cache lat/lon ลง EEPROM)
- 📶 **Wi-Fi Auto-Reconnect:** `loop()` เฝ้าสถานะทุก 2 วินาที ลองต่อใหม่ทุก 15 วินาที และเปิด AP สำรองให้เข้าไปแก้ค่าได้ถ้าหลุดเกิน 2 นาที
- 📊 **Mini Dashboard จาก JSON:** push `POST /api/draw` เข้ามาแล้วเครื่องวาดทั้งจอเอง รองรับกราฟแท่งเทียน/แท่งตั้ง/แท่งนอน/เส้น/โดนัท-พาย, KPI, เกจ, ข้อความไทย, และ **QR code (v3.5.0)** — ทั้งหมด autoscale ให้อัตโนมัติ ไม่เขียนแฟลชแม้แต่ไบต์เดียว สลับกลับหน้านาฬิกาได้ด้วย `/api/mode`
- 📱 **QR Code + PromptPay (v3.5.0):** เข้ารหัส QR (VERSION 7 / ECC-L) จาก text ตรงๆ หรือประกอบสาย PromptPay ให้อัตโนมัติจากเบอร์โทร/เลขบัตรประชาชน + ยอดเงิน โดยเครื่องหรี่จอลงเองชั่วคราวตอนโชว์ QR ให้กล้องมือถือโฟกัสง่ายขึ้น
- 💡 **ปรับความสว่างจอได้จากหน้าเว็บ:** สไลเดอร์ 5-100% บันทึกลง EEPROM ทันที
- ⚡ **OTA Firmware Update:** อัปเดตเฟิร์มแวร์ใหม่เป็นไฟล์ `.bin` ผ่านหน้าเว็บเบราว์เซอร์ได้ทันทีโดยไม่ต้องเสียบสาย
- 🔒 **ป้องกันหน้าเว็บด้วยรหัสผ่าน:** ทุก endpoint รวม OTA และรีสตาร์ท ต้องผ่าน HTTP Basic Auth พร้อมเตือนบนจอเมื่อยังใช้รหัสเริ่มต้น

---

## 🔌 การตั้งค่าพินหน้าจอ LCD (Hardware Pinout)
บอร์ด GeekMagic SmallTV ไม่มีขา CS (Tied to GND) จึงจำเป็นต้องใช้ค่าการเชื่อมต่อดังนี้:

| ขาจอภาพ ST7789 | พินบนชิป ESP8266 (NodeMCU) | รายละเอียด |
|---|---|---|
| **MOSI / SDA** | GPIO 13 (D7) | SPI Data |
| **CLK / SCLK** | GPIO 14 (D5) | SPI Clock |
| **DC** | GPIO 0 (D3) | Command / Data |
| **RST** | GPIO 2 (D4) | Reset |
| **BL (Backlight)** | GPIO 5 (D1) | Active LOW ผ่าน PWM (`analogWriteRange(1023)`) — 0 = สว่างสุด / 1023 = ดับสุด |
| **CS (Chip Select)** | *ต่อลง GND* | ต้องกําหนด **SPI Mode 3** ในโค้ด |

---

## 🔌 การต่อพินสำหรับ Flash เฟิร์มแวร์ (Flashing Connection Pinout)
เนื่องจากตัวบอร์ดนาฬิกา GeekMagic SmallTV ไม่มีชิป USB-to-Serial บนตัวบอร์ด (พอร์ต USB-C บนบอร์ดรับเฉพาะไฟเลี้ยง 5V เท่านั้น) การเขียนโปรแกรมครั้งแรกจึงต้องต่อสายพิน Tx/Rx ผ่านชิปแปลงสัญญาณภายนอก เช่น การทำ **NodeMCU Passthrough** (โดยบัดกรีหรือจิ้มพินด้านหลังเคสบอร์ดนาฬิกา) ดังนี้:

### 1. ตารางการเชื่อมต่อพิน (NodeMCU Passthrough)
| ขาบนตัวบอร์ดนาฬิกา SmallTV | ขาบนบอร์ด NodeMCU | รายละเอียด |
|---|---|---|
| **TXD0** | **RX** (GPIO3) | สัญญาณส่งข้อมูล |
| **RXD0** | **TX** (GPIO1) | สัญญาณรับข้อมูล |
| **GND** | **GND** | กราวด์ร่วม |
| **5V / VCC** | **VU / 5V** | ไฟเลี้ยงระบบ |
| **GPIO0** (FLASH) | **GND** *(ต่อค้างเฉพาะตอนบูต)* | **สำคัญ:** ต้องดึงลงดินขณะจ่ายไฟเพื่อเข้า **Bootloader Mode** |
| *ไม่มี* | **RST ต่อลง GND** | **สำคัญมาก:** ต้องต่อ RST ของ NodeMCU ลง GND เพื่อหยุดการทำงานของชิป ESP8266 บน NodeMCU (ใช้เป็นตัวผ่าน USB-to-Serial เท่านั้น) |

### 2. แผนผังและจุดเชื่อมต่อจริง (Wiring Diagrams)
เราได้ดึงพินเอาต์จากจุดเชื่อมต่อด้านหลังของบอร์ดและจัดทำรูปแผนผังการเชื่อมสายจริงดังแสดงด้านล่าง:

- **ตำแหน่งพินด้านหลังเคสบอร์ดนาฬิกา (GeekMagic Board Pins):**

  ![Board Photo](images/board_pins.jpg)
  ![Board Pins Diagram](https://i0.wp.com/randomnerdtutorials.com/wp-content/uploads/2019/05/ESP8266-ESP-12E-chip-pinout-gpio-pin.png?quality=100&strip=all&ssl=1)

- **การต่อสายจริงผ่าน NodeMCU Passthrough Mode:**

  ![NodeMCU Passthrough Wiring](https://i0.wp.com/randomnerdtutorials.com/wp-content/uploads/2019/05/ESP8266-NodeMCU-kit-12-E-pinout-gpio-pin.png?quality=100&strip=all&ssl=1)

---

## 🔗 แหล่งข้อมูลอ้างอิง (Reference Sources)
โครงการนี้รวบรวมข้อมูลและต่อยอดมาจากแหล่งอ้างอิงภายนอกที่สำคัญดังนี้:
1. **Home Assistant Forum - GeekMagic Thread:**
   [Installing ESPhome on GEEKMAGIC Smart Weather Clock](https://community.home-assistant.io/t/installing-esphome-on-geekmagic-smart-weather-clock-smalltv-pro/618029/5) *(ช่วยยืนยัน Pinout และการใช้ SPI Mode 3)*
2. **GitHub Repository - ViToni:**
   [esphome-geekmagic-smalltv](https://github.com/ViToni/esphome-geekmagic-smalltv)
3. **GitHub Repository - Adrien Brault:**
   [geekmagic-hacs](https://github.com/adrienbrault/geekmagic-hacs)
4. **GitHub Repository - bvweerd:**
   [geekmagic-tv-esp8266](https://github.com/bvweerd/geekmagic-tv-esp8266) *(เฉลยการกำหนดค่า LCD และ build_flags บนเฟิร์มแวร์ ESP8266 C++ Native)*

---

## 🌐 แหล่งข้อมูลสภาพอากาศและราคาทอง (Live Data APIs)

ตั้งแต่ v3.1.0 ค่าบนจอไม่ใช่ค่าตายตัวอีกแล้ว ทั้งสองอย่างดึงจาก API จริง ไม่ต้องสมัคร API key

| ข้อมูล | ปลายทาง | โปรโตคอล | รอบการดึง |
|---|---|---|---|
| พิกัดจากชื่อเมือง | `geocoding-api.open-meteo.com` | HTTP | ครั้งเดียว แล้วจำลง EEPROM |
| อุณหภูมิ + สภาพอากาศ | `api.open-meteo.com` | HTTP | ทุก 10 นาที |
| ราคาทอง XAU/USD | `api.gold-api.com` | HTTPS | ทุก 5 นาที |

**การแปลงชื่อเมืองเป็นพิกัด:** กรอกชื่อเมืองในการ์ด **🏙️ เมือง** บนหน้าเว็บ ตัวเครื่อง geocode ครั้งเดียวแล้วเก็บ lat/lon ลง EEPROM รอบถัดไปจึงยิงตรงไปที่ API อากาศเลย ไม่เสียเวลา geocode ซ้ำ

**สีของราคาทอง:** เทียบกับราคาที่ดึงได้รอบก่อนหน้า สูงกว่าเป็นสีเขียว ต่ำกว่าเป็นสีแดง รอบแรกหลังบูตยังไม่มีค่าเทียบจึงเป็นสีขาว

**เมื่อดึงข้อมูลไม่สำเร็จ:** ค่าเดิมยังอยู่บนจอ ไม่ขึ้นขีดกลางหรือค่าว่าง แต่จะมี **จุดสีเหลือง** กำกับไว้ว่าข้อมูลนี้เป็นของเก่า และตัวเครื่องจะเปลี่ยนไปลองใหม่ทุก 1 นาทีจนกว่าจะสำเร็จ

**บังคับดึงเดี๋ยวนี้:** กดปุ่ม **🔄 รีเฟรชข้อมูล** บนหน้าเว็บ (เรียก `GET /refresh` ต้องผ่าน auth) ไม่ต้องรอครบรอบ

> **หมายเหตุ:** การดึงราคาทองเป็น HTTPS ซึ่งกิน heap ตอน TLS handshake ราว 16–22 KB ตัวเฟิร์มแวร์จึงข้ามรอบนั้นไปถ้า heap เหลือน้อยกว่า 24 KB และไม่ดึงอากาศกับทองในรอบ `loop()` เดียวกัน เพื่อไม่ให้พีคการใช้ RAM ทับกัน

---

## 📊 Mini Dashboard จาก JSON Template (v3.2.0)

ตั้งแต่ v3.2.0 สั่งวาด dashboard ทั้งจอได้จาก JSON ที่ push เข้ามา ใช้แทนการส่งรูป JPEG ทั้งภาพ จึง **ไม่เขียนแฟลชเลยแม้แต่ครั้งเดียว** ไม่ต้องห่วงเรื่อง flash wear และเพราะ frame อยู่ใน RAM เท่านั้น รีบูตแล้วกลับมาเป็นหน้านาฬิกาเองอัตโนมัติ

### Endpoint

| Endpoint | Method | ทำอะไร |
|---|---|---|
| `/api/draw` | POST | รับ JSON template แล้ววาดทั้งจอ (body ไม่เกิน 6144 bytes) |
| `/api/mode?to=clock` | GET | กลับหน้านาฬิกา |
| `/api/mode?to=dashboard` | GET | วาด frame เดิมจาก RAM ซ้ำ (ตอบ 409 ถ้ายังไม่เคย push) |
| `/api/mode?to=toggle` | GET | สลับไปมา |

ทุกตัวต้องผ่าน HTTP Basic Auth เหมือน endpoint อื่น

### Widget ที่รองรับ

| `type` | ทำอะไร | field สำคัญ |
|---|---|---|
| `candles` | แท่งเทียน OHLC autoscale เอง หาช่วง high/low ของชุดข้อมูลแล้ว map ลงความสูงที่มีให้ พร้อมป้ายราคาสูงสุด/ต่ำสุดริมขวา แท่งขึ้นเขียว แท่งลงแดง | `data: [[o,h,l,c], ...]` สูงสุด 40 แท่ง (เกินเก็บท้ายสุดเพราะใหม่กว่า) |
| `column` | กราฟแท่งตั้ง ตามลำดับเวลา | `data: [v, ...]` |
| `bar` | กราฟแท่งนอน เทียบอันดับระหว่างรายการ | `data: [v, ...]`, `values` (โชว์ตัวเลขกำกับแท่ง) |
| `line` / `sparkline` | กราฟเส้น/แนวโน้มต่อเนื่อง (`sparkline` ไม่มีกรอบ/ป้ายเว้นแต่สั่งมาเอง) | `data: [v, ...]` |
| `donut` / `pie` | สัดส่วนต่อยอดรวม (`pie` คือ `donut` ที่ `hole=0` เป็นค่าเริ่มต้น) | `data: [v, ...]`, `hole` (% รูกลาง 0–95) |
| `kpi` | ตัวเลขสรุปค่าเดียวพร้อมป้ายกำกับ | `value` หรือ `data`, `label`, `text` |
| `gauge` | แถบเกจแนวนอนค่าเดียว | `value`, `min`/`max` (ต้องมาคู่กันถึงตรึงสเกล) |
| `title` / `text` | ข้อความไทยผ่าน renderer 178 glyphs `size` เลือกได้ 1–3 (`title` ปักหัวจอตัว 2x ไม่ต้องส่งพิกัด) | `text` |
| `rect` / `hline` | สี่เหลี่ยม/เส้นแนวนอน จัดเลย์เอาต์ | `x,y,w,h`, `fill` |
| `qr` | QR code เข้ารหัสตรึง VERSION 7 / ECC-L (45x45 module) — ดูหัวข้อ [QR Code + PromptPay](#-qr-code--promptpay-v350) | `text` หรือ `promptpay_id`/`promptpay_amount` |

ทุกกราฟ (`candles`/`column`/`bar`/`line`/`donut`) รับ `min`/`max` เพื่อตรึงสเกลเอง (ไม่ส่ง = autoscale), `threshold`+`color2` เพื่อสลับสีจุดที่เกินเกณฑ์

`color` รับทั้งชื่อสี (`red`, `green`, `orange`, `cyan`, `grey` ฯลฯ) และ `#RRGGBB`

### ตัวอย่าง

```bash
curl --user admin:yourpass -X POST http://<device-ip>/api/draw \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {"type": "title", "text": "ทองคำ XAU/USD", "color": "orange"},
      {"type": "candles", "x": 5, "y": 40, "w": 230, "h": 140,
       "data": [[2610,2618,2604,2615], [2615,2622,2611,2612], [2612,2620,2606,2618]]},
      {"type": "text", "text": "$2,618.00", "y": 195, "size": 2, "color": "green"}
    ]
  }'
```

ทดสอบเร็วๆ ด้วยสคริปต์ที่เตรียมไว้ (ยิงแท่งเทียน 24 แท่ง แล้วสลับโหมดให้ดู):

```bash
./scripts/test_dashboard.sh 192.168.1.50 admin yourpass
```

### Dashboard รายงานการใช้ AI token

`scripts/ai_tokens_dashboard.py` อ่าน transcript ของ Claude Code ที่ `~/.claude/projects/*/*.jsonl`
รวมยอด token ต่อวัน (กันนับซ้ำด้วย `message.id`) แล้วยิงเป็นกราฟแท่งขึ้นจอ

```bash
python scripts/ai_tokens_dashboard.py --dry-run          # ดู payload ก่อนยิง
python scripts/ai_tokens_dashboard.py 192.168.1.50 admin yourpass
python scripts/ai_tokens_dashboard.py --metric total --days 30 192.168.1.50 admin yourpass
```

`--metric` เลือกได้สามแบบ:

| metric | นับอะไร | เหมาะกับ |
|---|---|---|
| `billable` (ค่าตั้งต้น) | input + output + cache write | ใกล้ค่าใช้จ่ายจริงที่สุด |
| `total` | รวม cache read ด้วย | ดูปริมาณที่ไหลผ่าน model ทั้งหมด |
| `output` | เฉพาะที่ model เขียนออก | ดูปริมาณงานที่สั่งจริง |

ไม่ต้องแก้ firmware — สคริปต์ยืม widget `candles` มาทำกราฟแท่ง โดยส่ง OHLC เป็น `[0, v, 0, v]`
ทำให้ `open == close == v` ได้แท่งทึบจากฐาน 0 และเพราะ `close >= open` เครื่องวาดเป็นสีเขียวทุกแท่ง

### Dashboard โควต้าคงเหลือ

`scripts/ai_quota_dashboard.py` อ่านโควต้าจากที่แต่ละเครื่องมือ cache ไว้บนเครื่องนี้เท่านั้น
ไม่ยิง API ไม่แตะ credential

```bash
python scripts/ai_quota_dashboard.py --dry-run
python scripts/ai_quota_dashboard.py 192.168.1.50 admin yourpass
```

| เครื่องมือ | แหล่งข้อมูล | สถานะ |
|---|---|---|
| Codex | `~/.codex/sessions/**/*.jsonl` → `token_count.rate_limits` | ✅ มี `used_percent` + `window_minutes` + `resets_at` |
| Claude Code | `~/.claude/projects/*/*.jsonl` | ❌ ไม่ cache โควต้า (`rateLimits` เป็น `null` ทุกรายการ) |
| Antigravity | `~/.antigravity-agent/cloud_accounts.db` คอลัมน์ `quota_json` | ❌ เข้ารหัส `iv:salt:ciphertext` ต้องใช้คีย์จาก keystore |

ตัวที่ไม่มีข้อมูลจะขึ้น `n/a` ไม่เดาค่าให้ และไม่วาดแท่ง 0 เพราะแท่ง 0 อ่านว่า "ใช้ไป 0%" ซึ่งผิด

สองอย่างที่หลอก `drawCandles()` ให้ทำหน้าที่เป็นเกจ:

- **ตรึงสเกล** ใส่ `lo=0, hi=100` ทุกแท่ง ไม่งั้น autoscale จะทำให้แท่ง 94% กับ 100% สูงเท่ากัน
  ป้ายแกนขวาเลยอ่านเป็น `100`/`0` ตรงๆ และไส้เทียนที่ลากถึงเพดานกลายเป็นเส้นบอกที่ว่างที่ยังเหลือ
- **คุมสี** สีมาจากเงื่อนไข `cl >= op` ใน firmware ไม่ใช่ field สี เขียวใช้ `[0,100,0,v]`
  เกิน 90% สลับเป็น `[v,100,0,0]` ได้แท่งแดงก้อนเดิม

**snapshot ไม่ใช่ realtime** — Codex เขียนค่าตอนคุยเท่านั้น ถ้าไม่ได้เปิดใช้ ตัวเลขจะค้าง
สคริปต์จึงแสดงอายุ snapshot กำกับไว้ และตัด window ที่ snapshot เก่ากว่าความยาว window ตัวเองทิ้ง
(เช่นหน้าต่าง 5 ชม. ที่บันทึกไว้เดือนก่อน reset ไปหลายรอบแล้ว ไร้ความหมาย)

สคริปต์ตรวจข้อจำกัดของเครื่องให้ก่อนยิงทุกครั้ง (ความยาว 32 bytes ต่อข้อความ, ความกว้างจอ 240px,
เพดาน 40 แท่ง / 4 ข้อความ / body 6144 bytes) ถ้าเกินจะ error พร้อมบอกสาเหตุ ไม่ปล่อยให้ขึ้นจอเป็นขยะ

### ข้อจำกัดที่ควรรู้

- **TTL 10 นาที** — ถ้าไม่มี frame ใหม่ push เข้ามาภายใน 10 นาที เครื่องกลับหน้านาฬิกาเอง เพื่อไม่ให้ข้อมูลเก่าค้างบนจอตอนต้นทางล่ม
- **frame ต้องสมบูรณ์ในตัวเอง** — ทุกครั้งที่ push จะล้าง widget เดิมทั้งหมดก่อน ไม่ได้วาดทับสะสม
- **ตอนอยู่โหมด dashboard เครื่องยังดึงอากาศกับทองตามรอบเดิม** แต่ไม่วาดลงจอ พอสลับกลับหน้านาฬิกาจะเห็นค่าล่าสุดทันที
- **heap guard** — ถ้า heap เหลือน้อยกว่า ~16 KB (`DASH_DOC_SIZE` 8192 + กันชน 8000 ไบต์) ตอน push จะตอบ `503` ให้ลองใหม่ ไม่ปล่อยให้จอง document พังกลางทาง
- ข้อความจำกัด 4 ชิ้นต่อ frame ยาวไม่เกิน 31 ตัวอักษร (นับเป็นไบต์ ภาษาไทย 1 ตัวกิน 3 ไบต์)

---

## 📱 QR Code + PromptPay (v3.5.0)

widget `qr` เข้ารหัส QR ตรึงที่ **VERSION 7 / ECC-L** (grid 45x45 module) รับข้อมูลได้สองทาง:

**ทางที่ 1 — ส่ง `text` ตรงมาเลย** สำหรับ URL หรือข้อความทั่วไป เครื่องเข้ารหัสตามที่ส่งมาโดยไม่ปรุงแต่ง

**ทางที่ 2 — ส่ง `promptpay_id` (+ `promptpay_amount` ถ้าต้องการตรึงยอด)** เครื่องประกอบสาย EMVCo/Thai QR Payment (tag-length-value + CRC16) ให้เองทั้งหมด ไม่ต้องคำนวณ CRC หรือ TLV ฝั่งผู้ส่ง:
- `promptpay_id` รับได้ทั้งเบอร์โทร 10 หลักขึ้นต้นด้วย 0 (เช่น `0812345678`) และเลขบัตรประชาชน 13 หลัก
- `promptpay_amount` ใส่ถ้าต้องการตรึงยอดเงินไว้ในตัว QR เลย ไม่ใส่ (หรือ `0`) หมายถึงเปิดให้ผู้จ่ายกรอกยอดเองปลายทาง
- ถ้า `promptpay_id` ไม่ตรงรูปแบบทั้งสองแบบ widget นั้นจะถูกข้ามทิ้งเงียบๆ (widget อื่นในเฟรมยังวาดได้ตามปกติ)

```json
{
  "type": "qr",
  "promptpay_id": "0812345678",
  "promptpay_amount": 150.00,
  "x": 20, "y": 30, "w": 200, "h": 200,
  "color": "black", "color2": "white"
}
```

**เรื่องความสว่าง:** ความสว่างเต็มจ้าเกินไปสำหรับกล้องมือถือ (โฟกัส/สแกนไม่ติด) ตัวเครื่องจึงหรี่จอลงเองชั่วคราวเป็น 30% ทุกครั้งที่เฟรมมี widget `qr` ที่มี payload อยู่ แล้วคืนค่าความสว่างที่ตั้งไว้ตอนสลับกลับหน้านาฬิกา (ตั้งค่าความสว่างปกติได้ที่การ์ด **💡 ความสว่างหน้าจอ** บนหน้าเว็บ)

> ก่อนใช้จ่ายจริง ควรสแกนทดสอบเทียบชื่อผู้รับ/ยอดเงินในแอปธนาคารให้ตรงก่อนเสมอ

---

## 📥 การรับไฟล์เฟิร์มแวร์ (.bin)

ไฟล์ไบนารีไม่ได้ถูกเก็บไว้ใน repo นี้ (ถูกกันไว้ด้วย `.gitignore` เพื่อไม่ให้ repo บวม) เลือกรับไฟล์ได้ 2 ทาง:

**ทางที่ 1 — ดาวน์โหลดจากหน้า Releases (แนะนำ)**
เข้าไปที่หน้า **Releases** ของ repo นี้ แล้วดาวน์โหลดไฟล์แนบของเวอร์ชันล่าสุด:
- `SDP_v3.5.0.bin` — ใช้ได้ทั้งแฟลชครั้งแรกผ่านสาย Serial และอัปเดตแบบ OTA

**ทางที่ 2 — คอมไพล์เองด้วย PlatformIO (แนะนำสำหรับนักพัฒนา)**
โปรเจกต์ตั้งค่า PlatformIO ไว้ให้พร้อมแล้ว ไลบรารีทั้งหมดถูกระบุใน `platformio.ini` จึงถูกดาวน์โหลดอัตโนมัติ:

```bash
pio run
```

ไฟล์ `.bin` จะถูกสร้างที่ `.pio/build/geekmagic/firmware.bin` และมี post-build script คัดลอกไปไว้ที่ `build_esp8266/SDP_v<เวอร์ชัน>.bin` ให้อัตโนมัติ (อ่านเลขเวอร์ชันจาก `FW_VERSION` ในซอร์ส)

คำสั่งอื่นที่ใช้บ่อย:

```bash
pio run -t upload --upload-port COM3
```

```bash
pio device monitor -p COM3
```

**ทางที่ 3 — คอมไพล์ด้วย Arduino IDE**
เปิด `smart_clock_esp8266/smart_clock_esp8266.ino` (เลือกบอร์ดตระกูล ESP8266 ขนาดแฟลช 4MB) แล้วสั่ง **Export Compiled Binary** ต้องติดตั้งไลบรารี `Adafruit GFX Library`, `Adafruit ST7735 and ST7789 Library`, `Adafruit BusIO` และ `ArduinoJson` (เวอร์ชัน 6.x) ผ่าน Library Manager ก่อนคอมไพล์

---

## 🔒 รหัสผ่านหน้าเว็บ (Web Authentication)

ตั้งแต่ v3.0.0 ทุก endpoint ถูกป้องกันด้วย **HTTP Basic Auth** รวมถึง `/update_ota` และ `/restart` ที่เป็นจุดเสี่ยงที่สุด

**รหัสเริ่มต้นจากโรงงาน:**

| ช่อง | ค่า |
|---|---|
| ชื่อผู้ใช้ | `admin` |
| รหัสผ่าน | `smartclock` |

⚠️ **ต้องเปลี่ยนทันทีหลังติดตั้ง** ระหว่างที่ยังใช้รหัสเริ่มต้น ตัวเครื่องจะเตือนสองที่คือแถบ `[!PW]` สีแดงมุมขวาบนของจอ LCD และแบนเนอร์สีส้มบนหน้าเว็บ เปลี่ยนได้ที่การ์ด **🔒 รหัสผ่านหน้าเว็บ** (รหัสใหม่ต้องยาวอย่างน้อย 8 ตัวอักษร)

**ถ้าลืมรหัสผ่าน:** ตัวเครื่องไม่มีปุ่มรีเซ็ต ต้องล้าง EEPROM ด้วยการต่อสายแฟลชแล้วรัน `erase-flash` ตามขั้นตอนด้านล่าง ค่าที่ตั้งไว้ทั้งหมดรวม Wi-Fi จะหายไปด้วย

> **หมายเหตุเรื่องความปลอดภัย:** Basic Auth ส่งรหัสผ่านแบบ base64 บน HTTP ธรรมดา ซึ่งดักอ่านได้ถ้ามีคนดมแพ็กเก็ตอยู่ในวงเดียวกัน ระดับนี้พอสำหรับเครือข่ายในบ้าน แต่ไม่ควรเปิด port ของอุปกรณ์นี้ออกอินเทอร์เน็ต

---

## 🛠️ ขั้นตอนการรันคำสั่ง Flash เฟิร์มแวร์

### 1. วิธีแฟลชด้วยสาย NodeMCU ครั้งแรกสุด (Serial Flash)
1. เชื่อมต่อสายตามตารางด้านบนให้เรียบร้อย (รวมถึงต่อ **GPIO0 -> GND** และ **NodeMCU RST -> GND**)
2. เสียบสาย USB เข้ากับคอมพิวเตอร์ (ตัวคอมพิวเตอร์จะมองเห็นเป็น COM Port ของ NodeMCU เช่น `COM3`)
3. เปิดหน้าต่าง PowerShell และรันคำสั่งต่อไปนี้:

```powershell
# 1. ล้างข้อมูลความจำเดิมทั้งหมด
C:\Python312\python.exe -m esptool --chip esp8266 --port COM3 --baud 115200 erase-flash

# 2. แฟลชไฟล์เฟิร์มแวร์ใหม่ลงชิป (แก้ path ให้ตรงกับที่วางไฟล์ .bin ไว้)
C:\Python312\python.exe -m esptool --chip esp8266 --port COM3 --baud 115200 write-flash 0x0 "build_esp8266/SDP_v3.5.0.bin"
```

### 2. วิธีอัปเดตแบบไร้สาย (OTA Update)
เมื่อบอร์ดเชื่อมต่อ Wi-Fi แล้ว:
1. เปิดเว็บเบราว์เซอร์ไปที่ IP ของตัวนาฬิกา (เช่น `http://192.168.1.139/` หรือ `http://192.168.4.1/` ใน AP Mode) แล้วใส่ชื่อผู้ใช้และรหัสผ่านตามหัวข้อ [รหัสผ่านหน้าเว็บ](#-รหัสผ่านหน้าเว็บ-web-authentication)
2. ไปที่หัวข้อ **🚀 อัปเดต Firmware (OTA)**
3. เลือกไฟล์อัปเดตเวอร์ชันล่าสุด: `SDP_v3.5.0.bin`
4. กด **⚡ อัปโหลด .bin** ตัวเครื่องจะรีสตาร์ทเป็นเวอร์ชันใหม่ทันที

---

## 📂 โครงสร้างโฟลเดอร์หลักในโปรเจกต์
```text
smart_clock/
├── smart_clock_esp8266/
│   ├── smart_clock_esp8266.ino   # ซอร์สโค้ดหลัก C++ บนบอร์ด ESP8266
│   └── ThaiFont.h                # ข้อมูลพิกเซลตัวอักษรภาษาไทย 178 Glyphs
├── scripts/
│   ├── copy_firmware.py          # post-build hook คัดลอก .bin ไป build_esp8266/
│   ├── make_ca_bundle.py         # เครื่องมือแก้ปัญหา TLS inspection (ดูหัวข้อแก้ปัญหา)
│   ├── test_dashboard.sh         # ยิง /api/draw ทดสอบ mini dashboard บนเครื่องจริง
│   ├── ai_tokens_dashboard.py    # รวมยอด AI token รายวันจาก transcript แล้วยิงขึ้นจอ
│   └── ai_quota_dashboard.py     # อ่านโควต้าคงเหลือที่ cache ไว้บนเครื่อง แล้วยิงขึ้นจอ
├── images/                       # รูปแผนผังการต่อสายและพินเอาต์
├── platformio.ini                # ตั้งค่า build สำหรับ ESP8266 (board = esp12e)
├── build_esp8266/                # ปลายทางไฟล์ .bin ที่คอมไพล์เอง (ไม่ถูกเก็บใน repo)
├── .gitignore                    # กันไฟล์ build และ secrets ไม่ให้ขึ้น repo
└── README.md                     # ไฟล์คู่มือการใช้งานไฟล์นี้
```

---

## 🩺 แก้ปัญหาที่เจอบ่อย (Troubleshooting)

### `pio run` ล้มเหลวด้วย `HTTPClientError` ตอนติดตั้ง platform
เกิดบนเครื่องที่มีโปรแกรมสแกน TLS คั่นกลาง (Norton Web/Mail Shield, Zscaler, พร็อกซีองค์กร) โปรแกรมพวกนี้ออกใบรับรองปลอมแทนของจริง ไลบรารี `requests` ที่ PlatformIO ใช้เชื่อแค่ชุด CA ของ `certifi` ซึ่งไม่มี root CA ตัวนั้นอยู่ จึงตรวจไม่ผ่านด้วย `CERTIFICATE_VERIFY_FAILED` (สังเกตว่า `curl` กับเบราว์เซอร์ยังใช้ได้ปกติ เพราะอ่าน Windows certificate store)

วิธีแก้คือสร้าง CA bundle ที่รวม root CA จาก Windows store เข้ากับ certifi:

```bash
python scripts/make_ca_bundle.py
```

แล้วตั้ง environment variable ก่อนรัน `pio` (สคริปต์จะพิมพ์ path ที่ถูกต้องให้):

```bash
export REQUESTS_CA_BUNDLE="$HOME/.platformio/win-ca-bundle.pem"
```

บน PowerShell:

```powershell
$env:REQUESTS_CA_BUNDLE = "$env:USERPROFILE\.platformio\win-ca-bundle.pem"
```

### จอไม่ติด หรือขึ้นแต่สีขาว
ตรวจว่ากำหนด **SPI Mode 3** ในโค้ด (`tft.init(240, 240, SPI_MODE3)`) เพราะขา CS ถูกต่อลง GND ถาวร และไฟ Backlight เป็น **Active LOW** คือ `digitalWrite(TFT_BL, LOW)` จะสว่าง

### แฟลชไม่ได้ / esptool หาชิปไม่เจอ
ต้องดึง **GPIO0 ลง GND ขณะจ่ายไฟ** เพื่อเข้า bootloader mode และถ้าใช้ NodeMCU เป็นตัวแปลง USB-to-Serial ต้องต่อ **RST ของ NodeMCU ลง GND** เพื่อหยุดชิปตัวนั้นไม่ให้ทำงานแข่ง

---
*จัดทำขึ้นและทดสอบรันเสร็จสมบูรณ์ร่วมกับ USER เพื่อการพัฒนาโครงการอย่างยั่งยืน 🇹🇭*
