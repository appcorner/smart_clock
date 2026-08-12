# QR Code Refactoring (v3.6.0)

## สรุปการเปลี่ยนแปลง

ย้าย QR code payload (`qrText[160]`) จาก `Widget` struct → `Dashboard` struct เพื่อประหยัด RAM

---

## เหตุผล

### ปัญหาเดิม
```cpp
struct Widget {
    char qrText[160] = "";  // ทุก widget มี 160 bytes
};

Widget widgets[10];  // = 10 × 160 = 1,600 bytes
```

**แต่จริงๆ:** หน้าจอ 240×240 พิกเซล รองรับ QR ได้แค่ **1 อัน** ต่อ dashboard

- QR code 200×200 px = เกือบเต็มจอ
- QR เล็กกว่า 150px → module หนาแน่น → สแกนยาก
- หลาย QR พร้อมกัน → ผู้ใช้สับสน ไม่รู้จะสแกนอันไหน

**RAM สิ้นเปลือง:** 1,440 bytes (9 widgets ไม่ได้ใช้ qrText เลย)

---

## โครงสร้างใหม่

### Dashboard Struct
```cpp
struct Dashboard {
    Widget widgets[DASH_MAX_WIDGETS];
    uint8_t widgetCount = 0;
    float pool[DASH_POOL_SIZE];
    uint16_t poolUsed = 0;
    bool valid = false;
    bool dirty = false;
    unsigned long lastPush = 0;
    
    // QR code — ใช้ได้ 1 อันต่อ dashboard
    char qrText[160] = "";        // payload (PromptPay/URL ~150 bytes)
    int16_t qrX = 5, qrY = 5;
    int16_t qrW = 200, qrH = 200;
    uint16_t qrBg = ST77XX_BLACK; // สีพื้นหลัง
    uint16_t qrFg = ST77XX_WHITE; // สี module
};
```

### Widget Struct
```cpp
struct Widget {
    // ... ฟิลด์อื่นเหมือนเดิม
    // ลบ: char qrText[160] = "";
    char text[DASH_TEXT_LEN] = "";
    char label[DASH_TEXT_LEN] = "";
    bool hasRef = false;
    float refValue = 0.0f;
    char refType[12] = "";
};
```

---

## การเปลี่ยนแปลงโค้ด

### 1. ลบ W_QR จาก WidgetType
```cpp
enum WidgetType : uint8_t {
    // ...
    W_HLINE,
    W_VLINE
    // ลบ: W_QR
};
```

### 2. แก้ drawQr() รับพารามิเตอร์ตรง
```cpp
// เดิม
void drawQr(const Widget &g);

// ใหม่
void drawQr(const char* text, int16_t x, int16_t y, int16_t w, int16_t h,
            uint16_t bg, uint16_t fg);
```

### 3. แก้ renderDashboard()
```cpp
void renderDashboard() {
    tft.fillScreen(ST77XX_BLACK);
    
    // เช็ค QR ง่ายขึ้น — ไม่ต้อง loop
    bool hasQr = (dash.qrText[0] != '\0');
    setBacklightPct(hasQr ? QR_DIM_BRIGHTNESS_PCT : sysConfig.brightness);
    
    // Pass 1: วาดกราฟ
    for (uint8_t i = 0; i < dash.widgetCount; i++) {
        // ... (ไม่มี case W_QR แล้ว)
    }
    
    // Pass 2: วาด primitives
    for (uint8_t i = 0; i < dash.widgetCount; i++) {
        // ...
    }
    
    // วาด QR ตอนท้าย (ถ้ามี)
    if (hasQr) {
        drawQr(dash.qrText, dash.qrX, dash.qrY, dash.qrW, dash.qrH,
               dash.qrBg, dash.qrFg);
    }
    
    dash.dirty = false;
}
```

### 4. แก้ handleApiDraw() JSON Parsing
```cpp
else if (!strcmp(type, "qr")) {
    // เก็บค่าใน Dashboard แทน Widget
    dash.qrX = wgt["x"] | 5;
    dash.qrY = wgt["y"] | 5;
    dash.qrW = wgt["w"] | 200;
    dash.qrH = wgt["h"] | 200;
    dash.qrBg = parseColor(wgt["color"]  | "", ST77XX_BLACK);
    dash.qrFg = parseColor(wgt["color2"] | "", ST77XX_WHITE);
    
    // PromptPay หรือ text ธรรมดา
    const char* ppId = wgt["promptpay_id"] | "";
    if (*ppId) {
        float amt = wgt["promptpay_amount"] | 0.0f;
        String payload = buildPromptPayPayload(ppId, amt);
        if (payload.length() > 0 && payload.length() < sizeof(dash.qrText)) {
            strlcpy(dash.qrText, payload.c_str(), sizeof(dash.qrText));
        }
    } else {
        const char* s = wgt["text"] | "";
        if (*s) strlcpy(dash.qrText, s, sizeof(dash.qrText));
    }
    
    // ไม่เพิ่ม widget เข้า array
    continue;
}
```

### 5. แก้การล้าง frame
```cpp
void handleApiDraw() {
    // ...
    dash.widgetCount = 0;
    dash.poolUsed = 0;
    dash.qrText[0] = '\0';  // ล้าง QR เก่า
}
```

---

## ผลลัพธ์

### Resource Usage

| Metric | Before (v3.5.3) | After (v3.6.0) | Change |
|--------|-----------------|----------------|--------|
| **RAM** | 49.4% (40,444 bytes) | 47.4% (38,860 bytes) | **-2.0% (-1,584 bytes)** |
| **Flash** | 48.5% (507,031 bytes) | 48.5% (506,679 bytes) | -0.0% (-352 bytes) |
| **Binary** | 499.2 KB | 498.9 KB | -0.3 KB |

**RAM ประหยัด:** 1,584 bytes (1,440 bytes ทฤษฎี + 144 bytes struct padding)

---

## Breaking Changes

### ⚠️ QR ไม่นับเป็น widget แล้ว

**เดิม:**
```json
{
  "widgets": [
    {"type": "text", "text": "ทอง"},
    {"type": "qr", "text": "https://example.com"}
  ]
}
```
→ `dash.widgetCount = 2`

**ใหม่:**
```json
{
  "widgets": [
    {"type": "text", "text": "ทอง"},
    {"type": "qr", "text": "https://example.com"}
  ]
}
```
→ `dash.widgetCount = 1` (QR ไม่นับ)

**ผลกระทบ:**
- ถ้าส่ง 10 widgets + 1 QR → รับได้ครบทั้ง 11 (เดิมจะ drop QR ถ้าเป็นตัวที่ 11)
- QR ไม่กิน widget slot แล้ว

---

## JSON API (ไม่เปลี่ยน)

```json
POST /api/draw
{
  "widgets": [
    {
      "type": "qr",
      "x": 20,
      "y": 20,
      "w": 200,
      "h": 200,
      "color": "#000000",
      "color2": "#FFFFFF",
      "promptpay_id": "0812345678",
      "promptpay_amount": 100.0
    }
  ]
}
```

หรือ text ธรรมดา:
```json
{
  "type": "qr",
  "text": "https://example.com"
}
```

**หมายเหตุ:** ถ้าส่ง QR หลายอัน → ใช้ตัวสุดท้าย (overwrite)

---

## ข้อจำกัด

1. **QR ได้แค่ 1 อัน** ต่อ dashboard (เป็น design constraint ตั้งแต่เดิมแล้ว)
2. **QR ไม่นับเป็น widget** ใน `widgetCount`
3. **ส่ง QR หลายอัน** → ตัวสุดท้ายเป็นตัวที่แสดง

---

## ข้อดี

✅ **ประหยัด RAM 1,584 bytes** (1.9%)  
✅ **Logic ชัดเจน** — QR ใช้ได้ 1 อันจริงๆ  
✅ **เช็ค hasQr เร็วขึ้น** — ไม่ต้อง loop 10 รอบ  
✅ **ไม่กิน widget slot** — ส่ง 10 widgets + QR ได้  
✅ **Binary เบาลง** 0.3 KB  

---

## การทดสอบ

### Test Case 1: QR PromptPay
```bash
curl --user admin:smartclock -X POST http://192.168.1.50/api/draw \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {
        "type": "qr",
        "promptpay_id": "0812345678",
        "promptpay_amount": 100.0
      }
    ]
  }'
```

**Expected:** QR code PromptPay 100 บาท แสดงกลางจอ

### Test Case 2: QR + Widgets อื่น
```bash
curl --user admin:smartclock -X POST http://192.168.1.50/api/draw \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {"type": "text", "text": "สแกนจ่าย", "x": 80, "y": 10},
      {"type": "qr", "x": 20, "y": 30, "w": 200, "h": 200, 
       "promptpay_id": "0812345678", "promptpay_amount": 50.0}
    ]
  }'
```

**Expected:** ข้อความ "สแกนจ่าย" + QR code

### Test Case 3: QR หลายอัน (overwrite)
```bash
curl --user admin:smartclock -X POST http://192.168.1.50/api/draw \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {"type": "qr", "text": "first"},
      {"type": "qr", "text": "second"}
    ]
  }'
```

**Expected:** แสดงเฉพาะ "second" (ตัวสุดท้าย overwrite ตัวแรก)

---

## Migration Guide

### ถ้าใช้ Widget Loop ภายนอก

**เดิม:**
```cpp
for (uint8_t i = 0; i < dash.widgetCount; i++) {
    if (dash.widgets[i].type == W_QR) {
        // process QR
    }
}
```

**ใหม่:**
```cpp
// เช็คตรงๆ
if (dash.qrText[0] != '\0') {
    // process QR at dash.qrX, dash.qrY
}
```

---

*Refactored in firmware v3.6.0 — 2026-08-11*
