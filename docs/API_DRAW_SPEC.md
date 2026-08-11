# คู่มือ API /api/draw สำหรับนาฬิกาอัจฉริยะ

## Endpoint

```
POST /api/draw
```

ส่ง JSON dashboard template เพื่อวาดกราฟและ widgets บนจอ ST7789 ขนาด 240×240 พิกเซล

**การยืนยันตัวตน**: ต้องผ่าน HTTP Basic Authentication

---

## รูปแบบ Request

```json
{
  "mode": "dashboard" | "clock",
  "widgets": [ ... ]
}
```

### พารามิเตอร์

- **mode** (string, ไม่บังคับ): โหมดแสดงผลหลังจาก push สำเร็จ
  - `"dashboard"` (ค่าเริ่มต้น): เข้าโหมด dashboard ทันที
  - `"clock"`: push ข้อมูลแต่ยังอยู่โหมดนาฬิกา (ให้ผู้ใช้สลับเอง)

- **widgets** (array, บังคับ): รายการ widget ที่จะวาด (สูงสุด 16 ชิ้น)

---

## ประเภท Widget

### 1. ข้อความ (Text / Title)

แสดงข้อความธรรมดา

```json
{
  "type": "text" | "title",
  "text": "ข้อความ",
  "x": 5,
  "y": 190,
  "size": 2,
  "color": "white"
}
```

**พารามิเตอร์:**
- `text` (string, บังคับ): ข้อความที่จะแสดง (สูงสุด 48 ตัวอักษร)
- `x` (int, ไม่บังคับ): พิกัด X (ค่าเริ่มต้น: `5`)
- `y` (int, ไม่บังคับ): พิกัด Y
  - `text`: ค่าเริ่มต้น `190`
  - `title`: ค่าเริ่มต้น `6` (หัวจอ)
- `size` (int, ไม่บังคับ): ขนาดตัวอักษร `1-3` (ค่าเริ่มต้น: `2`)
- `color` (string, ไม่บังคับ): สี (ค่าเริ่มต้น: `"white"`)

**ตัวอย่าง:**
```json
{ "type": "title", "text": "สรุปวันนี้", "color": "orange" }
{ "type": "text", "text": "รวม 1,430", "y": 200, "size": 2, "color": "white" }
```

---

### 2. การ์ด KPI

การ์ดแสดงค่าสำคัญตัวใหญ่พร้อมป้ายกำกับ

```json
{
  "type": "kpi",
  "label": "TOKENS",
  "text": "1.24M",
  "value": 18.42,
  "decimals": 2,
  "x": 5, "y": 40, "w": 110, "h": 62,
  "size": 3,
  "color": "cyan",
  "color2": "red",
  "threshold": 15,
  "axis": true
}
```

**พารามิเตอร์:**
- `label` (string, ไม่บังคับ): ป้ายกำกับด้านบน
- `text` (string, ไม่บังคับ): ข้อความที่แสดง (ใช้แทน value ถ้าต้องการจัดรูปแบบเอง)
- `value` (float, ไม่บังคับ): ตัวเลขที่แสดง (ใช้ร่วมกับ `decimals`)
- `data` (array, ไม่บังคับ): ใช้แทน `value` ถ้าต้องการส่งหลายค่า
- `decimals` (int, ไม่บังคับ): จำนวนทศนิยม `0-4` (ค่าเริ่มต้น: `0`)
- `size` (int, ไม่บังคับ): ขนาดตัวเลข `1-6` (ค่าเริ่มต้น: `3`)
- `threshold` (float, ไม่บังคับ): ค่าเกณฑ์ — ถ้า value ≥ threshold จะเปลี่ยนเป็น `color2`
- `color` (string, ไม่บังคับ): สีตัวเลข (ค่าเริ่มต้น: `"white"`)
- `color2` (string, ไม่บังคับ): สีเมื่อเกิน threshold (ค่าเริ่มต้น: `"red"`)
- `axis` (bool, ไม่บังคับ): วาดกรอบ (ค่าเริ่มต้น: `false`)

---

### 3. เกจ (Gauge)

แถบเกจแนวนอนแสดงความคืบหน้า

```json
{
  "type": "gauge",
  "value": 72,
  "min": 0,
  "max": 100,
  "x": 5, "y": 194, "w": 230, "h": 16,
  "color": "green",
  "color2": "red",
  "threshold": 80
}
```

**พารามิเตอร์:**
- `value` (float, บังคับ): ค่าปัจจุบัน
- `min`, `max` (float, บังคับ): ขอบเขตสเกล
- `threshold` (float, ไม่บังคับ): เกณฑ์ — ถ้า value ≥ threshold แถบเปลี่ยนเป็น `color2`
- `color` (string, ไม่บังคับ): สีแถบปกติ (ค่าเริ่มต้น: `"green"`)
- `color2` (string, ไม่บังคับ): สีแถบเกินเกณฑ์ (ค่าเริ่มต้น: `"red"`)

---

### 4. กราฟแท่งนอน (Bar Chart)

```json
{
  "type": "bar",
  "data": [520, 410, 260, 150, 90],
  "x": 5, "y": 40, "w": 230, "h": 150,
  "color": "cyan",
  "color2": "orange",
  "threshold": 400,
  "values": true,
  "decimals": 0,
  "axis": true
}
```

**พารามิเตอร์:**
- `data` (array, บังคับ): ชุดข้อมูล (สูงสุด 255 จุด)
- `threshold` (float, ไม่บังคับ): แท่งที่มีค่า ≥ threshold จะใช้ `color2`
- `values` (bool, ไม่บังคับ): แสดงค่าท้ายแท่ง (ค่าเริ่มต้น: `false`)
- `decimals` (int, ไม่บังคับ): ทศนิยมที่แสดง (ค่าเริ่มต้น: `0`)

---

### 5. กราฟแท่งตั้ง (Column Chart)

```json
{
  "type": "column",
  "data": [420, 615, 580, 940, 1020, 760, 350],
  "x": 5, "y": 40, "w": 230, "h": 150,
  "color": "cyan",
  "color2": "orange",
  "threshold": 900,
  "decimals": 0,
  "axis": true
}
```

**พารามิเตอร์:** เหมือนกราฟแท่งนอน

---

### 6. กราฟเส้น (Line Chart / Sparkline)

```json
{
  "type": "line",
  "data": [26.4, 26.1, 25.8, ..., 26.5],
  "x": 5, "y": 40, "w": 230, "h": 150,
  "color": "green",
  "color2": "#062808",
  "fill": true,
  "decimals": 1,
  "axis": true
}
```

```json
{
  "type": "sparkline",
  "data": [132, 140, 138, 151, ..., 148],
  "x": 5, "y": 104, "w": 230, "h": 40,
  "color": "cyan",
  "axis": false
}
```

**พารามิเตอร์:**
- `fill` (bool, ไม่บังคับ): เติมพื้นที่ใต้เส้น (ค่าเริ่มต้น: `false`)
- **sparkline**: `axis` ค่าเริ่มต้น = `false` (เส้นเปล่าไม่มีกรอบ)
- **line**: `axis` ค่าเริ่มต้น = `true`

---

### 7. กราฟแท่งเทียน (Candlestick)

```json
{
  "type": "candles",
  "data": [
    [open, high, low, close],
    [2615, 2622, 2611, 2612],
    ...
  ],
  "x": 5, "y": 40, "w": 230, "h": 140,
  "color": "green",
  "color2": "red",
  "axis": true
}
```

**พารามิเตอร์:**
- `data` (array of [O,H,L,C], บังคับ): แต่ละจุดเป็น array 4 ค่า
- `color` (string, ไม่บังคับ): สีแท่งขาขึ้น (close ≥ open) (ค่าเริ่มต้น: `"green"`)
- `color2` (string, ไม่บังคับ): สีแท่งขาลง (close < open) (ค่าเริ่มต้น: `"red"`)

---

### 8. กราฟวงกลม (Donut / Pie)

```json
{
  "type": "donut",
  "data": [62, 24, 14],
  "hole": 50,
  "x": 155, "y": 92, "w": 80, "h": 80
}
```

```json
{
  "type": "pie",
  "data": [62, 24, 14],
  "x": 155, "y": 92, "w": 80, "h": 80
}
```

**พารามิเตอร์:**
- `data` (array, บังคับ): สัดส่วนแต่ละชิ้น (3 สีหมุนเวียน: เขียว, เหลือง, ส้ม)
- `hole` (int, ไม่บังคับ): เปอร์เซ็นต์รูตรงกลาง `0-95`
  - `donut`: ค่าเริ่มต้น `55`
  - `pie`: ค่าเริ่มต้น `0`

---

### 9. QR Code

วาด QR code สำหรับ URL หรือ PromptPay

```json
{
  "type": "qr",
  "text": "https://example.com",
  "x": 20, "y": 30, "w": 200, "h": 200,
  "color": "black",
  "color2": "white"
}
```

```json
{
  "type": "qr",
  "promptpay_id": "0812345678",
  "promptpay_amount": 150.00,
  "x": 20, "y": 30, "w": 200, "h": 200
}
```

**พารามิเตอร์:**
- **ทางที่ 1**: ข้อความทั่วไป
  - `text` (string, บังคับ): ข้อความที่เข้ารหัส (สูงสุด 160 ตัวอักษร)
- **ทางที่ 2**: PromptPay QR
  - `promptpay_id` (string, บังคับ): เบอร์โทร 10 หลัก หรือ เลขบัตรประชาชน 13 หลัก
  - `promptpay_amount` (float, ไม่บังคับ): จำนวนเงิน (ถ้าไม่ส่ง = ให้ผู้จ่ายกรอกเอง)

---

### 10. วาดรูปพื้นฐาน (Primitives)

#### สี่เหลี่ยม (Rectangle)

```json
{
  "type": "rect",
  "x": 0, "y": 0, "w": 240, "h": 30,
  "color": "grey",
  "fill": true
}
```

#### เส้นแนวนอน (Horizontal Line)

```json
{
  "type": "hline",
  "x": 5, "y": 178, "w": 230,
  "color": "grey"
}
```

---

## พารามิเตอร์ทั่วไป (ใช้ได้กับหลาย widget)

| พารามิเตอร์ | ชนิด | คำอธิบาย | ค่าเริ่มต้น |
|------------|------|----------|------------|
| `x`, `y` | int | ตำแหน่งมุมซ้ายบน (พิกเซล) | `5, 40` |
| `w`, `h` | int | ความกว้าง × สูง (พิกเซล) | `230, 140` |
| `label` | string | ป้ายกำกับด้านบน (สูงสุด 16 ตัวอักษร) | `""` |
| `color` | string | สีหลัก | ขึ้นกับ widget |
| `color2` | string | สีรอง (threshold, เงา) | ขึ้นกับ widget |
| `axis` | bool | วาดกรอบและแกน | `true` (กราฟ), `false` (kpi) |
| `fill` | bool | เติมพื้นที่ (line/rect) | `false` |
| `values` | bool | แสดงค่าบนกราฟ | `false` |
| `decimals` | int | จำนวนทศนิยม `0-4` | `0` |
| `min`, `max` | float | ขอบเขตสเกล (ส่งทั้งคู่ = ตรึงสเกล) | autoscale |
| `threshold` | float | ค่าเกณฑ์สลับสี | ไม่มี |

---

## ค่าสี

รองรับ 3 รูปแบบ:

1. **ชื่อสี** (ไม่สนใจตัวพิมพ์เล็ก-ใหญ่):
   - `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`
   - `orange`, `purple`, `pink`, `lime`, `teal`, `brown`
   - `white`, `black`, `grey`/`gray`, `darkgrey`, `lightgrey`

2. **RGB565 Hex**: `#F800` (แดง), `#07E0` (เขียว)

3. **RGB888 Hex**: `#FF5733`, `#062808` (แปลงเป็น RGB565 อัตโนมัติ)

---

## Response

### สำเร็จ (200 OK)
```
OK
```

### ข้อผิดพลาด

| รหัส | ข้อความ | ความหมาย |
|------|---------|---------|
| 400 | `expected JSON body` | ไม่มี body หรือไม่ใช่ JSON |
| 400 | `JSON error: ...` | JSON parse ล้มเหลว |
| 400 | `missing widgets array` | ไม่มี field `widgets` |
| 400 | `no drawable widget` | ทุก widget ผ่านเงื่อนไขไม่ได้ |
| 413 | `body too large` | body > 8192 bytes |
| 503 | `heap too low, try again` | RAM เหลือน้อยเกิน ลองใหม่อีกครั้ง |

---

## ข้อจำกัด

| ค่าคงที่ | ค่า | คำอธิบาย |
|---------|-----|----------|
| `DASH_MAX_WIDGETS` | 16 | จำนวน widget สูงสุดต่อ frame |
| `DASH_MAX_POINTS` | 255 | จำนวนจุดข้อมูลสูงสุดต่อกราฟ |
| `DASH_POOL_SIZE` | 1024 | พื้นที่รวมสำหรับเก็บจุดข้อมูลทั้งหมด |
| `DASH_MAX_BODY` | 8192 | ขนาด JSON body สูงสุด (bytes) |

---

## ตัวอย่าง: Dashboard รวม

```json
{
  "mode": "dashboard",
  "widgets": [
    { "type": "title", "text": "ภาพรวมระบบ", "color": "orange" },
    { "type": "kpi", "label": "UPTIME %", "value": 99.8, "decimals": 1,
      "x": 5, "y": 34, "w": 112, "h": 52, "size": 3, "color": "green" },
    { "type": "kpi", "label": "ERRORS", "value": 12,
      "x": 123, "y": 34, "w": 112, "h": 52, "size": 3, "color": "yellow",
      "threshold": 10, "color2": "red" },
    { "type": "column", "x": 5, "y": 94, "w": 145, "h": 76, "axis": false,
      "color": "cyan",
      "data": [12, 18, 15, 22, 27, 24, 31, 28, 35, 33] },
    { "type": "donut", "x": 155, "y": 92, "w": 80, "h": 80, "hole": 50,
      "data": [62, 24, 14] },
    { "type": "hline", "x": 5, "y": 178, "w": 230, "color": "grey" },
    { "type": "gauge", "x": 5, "y": 188, "w": 230, "h": 14,
      "value": 61, "min": 0, "max": 100, "color": "green",
      "threshold": 85, "color2": "red" },
    { "type": "text", "text": "CPU 61%", "y": 208, "size": 2, "color": "white" }
  ]
}
```

---

## ตัวอย่างการใช้ Curl

```bash
curl -u admin:password -X POST http://192.168.1.100/api/draw \
  -H "Content-Type: application/json" \
  -d @examples/09_mixed.json
```

---

## API อื่นๆ ที่เกี่ยวข้อง

- `GET /api/mode?to=clock|dashboard|toggle` — สลับโหมดแสดงผล
- `GET /config` — ตั้งค่าระบบ (Wi-Fi, ความสว่าง, API keys)
- `GET /` — Web UI สำหรับควบคุมผ่านเบราว์เซอร์
