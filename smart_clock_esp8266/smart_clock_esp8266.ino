#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <time.h>
#include <TZ.h>
#include "ThaiFont.h"
// ตัว encoder QR (qrencode.c/frame.c) ดัดแปลงจาก anunpanya/ESP8266_QRcode
// (ตัด wrapper เฉพาะ SSD1306 ทิ้ง เอาแต่ core อัลกอริทึมที่ไม่ผูกจอ)
#include "qrencode.h"

#define FW_VERSION "3.5.0"

// รหัสผ่านหน้าเว็บเริ่มต้น — ตัวเครื่องจะเตือนบนจอและบนหน้าเว็บจนกว่าจะเปลี่ยน
#define DEFAULT_WEB_USER "admin"
#define DEFAULT_WEB_PASS "smartclock"

// แหล่งข้อมูลจริง — เลือกตัวที่ไม่ต้องใช้ API key ทั้งคู่ ผู้ใช้จึงไม่ต้องสมัครอะไรเลย
// open-meteo ใช้ HTTP ธรรมดาได้ ประหยัด RAM เพราะไม่ต้องทำ TLS handshake
#define GEOCODE_HOST "http://geocoding-api.open-meteo.com"
#define WEATHER_HOST "http://api.open-meteo.com"
// gold-api.com บังคับ HTTPS จึงต้องจ่าย RAM ให้ BearSSL ราว 16-22 KB ตอนเชื่อมต่อ
#define GOLD_URL     "https://api.gold-api.com/price/XAU"

// ระยะห่างการดึงข้อมูล — ถ่วงระหว่างความสดของข้อมูลกับการกวน API ฟรี
#define WEATHER_INTERVAL_MS 600000UL  // 10 นาที
#define GOLD_INTERVAL_MS    300000UL  // 5 นาที
#define RETRY_INTERVAL_MS   60000UL   // ดึงไม่สำเร็จ ให้ลองใหม่เร็วขึ้นเป็น 1 นาที
#define HTTP_TIMEOUT_MS     8000

// จังหวะดูแล Wi-Fi — ทั้งหมดทำแบบไม่บล็อก loop()
#define WIFI_CHECK_MS       2000UL    // ตรวจสถานะทุก 2 วินาที
#define WIFI_RETRY_MS       15000UL   // หลุดแล้วยิงต่อใหม่ทุก 15 วินาที
#define WIFI_AP_FALLBACK_MS 120000UL  // หลุดนานเกิน 2 นาที เปิด AP ให้เข้ามาแก้ค่า

// Dashboard ที่ push เข้ามาทาง /api/draw — อยู่ใน RAM เท่านั้น ไม่เขียนแฟลชเลย
// จำนวนจุดจำกัดที่ 40 ต่อกราฟ เพราะกว้าง 230px หารแล้วได้ ~5px ถ้ามากกว่านี้จะอ่านไม่ออก
#define DASH_MAX_POINTS   40
#define DASH_MAX_WIDGETS  10
// 48 ไบต์ ไม่ใช่ 32 เพราะไทยกินตัวละ 3 ไบต์ใน UTF-8 — 32 ไบต์ได้แค่ 10 ตัวอักษร
// ซึ่งสั้นกว่าที่จอรับได้จริง (scale 2 วาดได้ 15 cell ต่อบรรทัดบนจอ 240px)
// และ strlcpy ที่ตัดกลาง codepoint จะทำให้ไบต์ท้ายกลายเป็นขยะบนจอ
#define DASH_TEXT_LEN     48
// คลังตัวเลขที่ทุก widget แชร์กัน — candles กินจุดละ 4 ค่า ที่เหลือกินจุดละ 1 ค่า
// 200 ช่อง = candles เต็ม 40 แท่ง (160) แล้วยังเหลือให้กราฟเส้นอีก 40 จุด
#define DASH_POOL_SIZE    200
// ก้อน JSON ที่รับได้ — 10 widget ที่มีกราฟหลายตัวราว 2.5KB จึงเผื่อไว้เท่าตัว
#define DASH_MAX_BODY     6144
// ArduinoJson ต้องการที่มากกว่าตัวข้อความ เพราะต้องกาง object tree ขึ้น RAM
#define DASH_DOC_SIZE     8192
// ไม่มีใคร push ต่อภายในเวลานี้ ให้กลับไปหน้านาฬิกาเอง กัน HA ล่มแล้วจอค้างข้อมูลเก่า
#define DASH_TTL_MS       600000UL  // 10 นาที

// GeekMagic SmallTV Pinout Configuration
#define TFT_MOSI 13 // GPIO13
#define TFT_SCLK 14 // GPIO14
#define TFT_DC    0 // GPIO0
#define TFT_RST   2 // GPIO2
#define TFT_CS   -1 // Tied to GND
#define TFT_BL    5 // GPIO5 (Active Low Backlight)

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// EEPROM layout v2.x (ก่อนมี auth) เก็บไว้เพื่อ migrate ค่า Wi-Fi ของผู้ใช้
struct ConfigV2 {
    char ssid[32];
    char password[64];
    char city[32];
    uint8_t brightness;
    bool celsius;
    bool hour12;
    uint8_t magic; // 0xAB
};

// EEPROM layout v3.0.0 (มี auth แล้ว แต่ยังไม่ cache พิกัด)
struct ConfigV300 {
    char ssid[32];
    char password[64];
    char city[32];
    uint8_t brightness;
    bool celsius;
    bool hour12;
    char webUser[24];
    char webPass[32];
    uint8_t magic; // 0xAC
};

struct ClockConfig {
    char ssid[32] = "";
    char password[64] = "";
    char city[32] = "Bangkok";
    uint8_t brightness = 80;
    bool celsius = true;
    bool hour12 = false;
    char webUser[24] = DEFAULT_WEB_USER;
    char webPass[32] = DEFAULT_WEB_PASS;
    // cache พิกัดที่ geocode ได้ เพื่อไม่ต้องยิง geocoding API ทุกครั้งที่บูต
    float lat = 0.0f;
    float lon = 0.0f;
    uint8_t magic = 0xAD; // ขึ้นเป็น 0xAD เพราะเพิ่ม lat/lon
};

ClockConfig sysConfig;
ESP8266WebServer server(80);
bool isAPModeActive = false;

// ข้อมูลอากาศจาก open-meteo
struct WeatherData {
    float tempC = 0.0f;
    int code = -1;          // WMO weather code, -1 = ยังไม่มีข้อมูล
    bool valid = false;     // เคยดึงสำเร็จอย่างน้อยหนึ่งครั้ง
    bool stale = false;     // ครั้งล่าสุดดึงไม่สำเร็จ ค่าที่โชว์เป็นของเก่า
    unsigned long lastOk = 0;
};

// ราคาทองจาก gold-api.com — ไม่มี % เปลี่ยนแปลงมาให้ จึงเทียบกับราคาครั้งก่อนเอง
struct GoldData {
    float price = 0.0f;
    float prevPrice = 0.0f; // ราคาครั้งก่อน ใช้ตัดสินสีของตัวเลข
    bool valid = false;
    bool stale = false;
    unsigned long lastOk = 0;
};

WeatherData weather;
GoldData gold;

// ---------------------------------------------------------------------------
// Dashboard model — เก็บ display list ที่ parse แล้วไว้ใน RAM
// เก็บไว้ (ไม่ใช่วาดทิ้ง) เพราะตอนสลับกลับจากหน้านาฬิกาต้องวาดซ้ำได้เอง
// ไม่ต้องรอ push ก้อนใหม่ และเพราะอยู่แค่ RAM รีบูตแล้วกลับเป็นนาฬิกาอัตโนมัติ
// ---------------------------------------------------------------------------

enum DisplayMode : uint8_t { MODE_CLOCK = 0, MODE_DASHBOARD = 1 };
DisplayMode displayMode = MODE_CLOCK;

enum WidgetType : uint8_t {
    W_NONE = 0,
    W_TEXT,      // ข้อความ ใช้ฟอนต์ไทย 178 glyphs
    W_CANDLES,   // แท่งเทียน OHLC จุดละ 4 ค่า
    W_COLUMN,    // กราฟแท่งตั้ง จุดละ 1 ค่า — ข้อมูลตามลำดับเวลา
    W_HBAR,      // กราฟแท่งนอน จุดละ 1 ค่า — เทียบอันดับระหว่างรายการ
    W_LINE,      // กราฟเส้น/sparkline จุดละ 1 ค่า — แนวโน้มต่อเนื่อง
    W_DONUT,     // โดนัท/พาย จุดละ 1 ค่า — สัดส่วนต่อยอดรวม
    W_KPI,       // ตัวเลขสรุปค่าเดียว พร้อมป้ายกำกับ
    W_GAUGE,     // แถบเกจแนวนอนค่าเดียว
    W_RECT,      // สี่เหลี่ยม ทึบหรือเส้นขอบ
    W_HLINE,     // เส้นแนวนอน ใช้เป็นเส้นอ้างอิง/เส้นคั่น
    W_QR         // QR code — เข้ารหัสจาก text ตรึง VERSION 7 / ECC-L (45x45 module)
};

// widget เดียวครอบทุกชนิด เปลืองบ้างตรง text ที่กราฟไม่ใช้
// แต่แลกกับ parser และ renderer ที่วนลูปเดียวจบ ไม่ต้องแยก slot ต่อชนิด
struct Widget {
    WidgetType type = W_NONE;
    int16_t x = 0, y = 0, w = 0, h = 0;
    uint16_t color = 0xFFFF;      // สีหลัก
    uint16_t color2 = 0xF800;     // สีรอง: แท่งขาลง / ค่าที่เกิน threshold
    uint8_t scale = 1;            // เฉพาะ W_TEXT ส่งเข้า drawThaiStringScaled ตรงๆ
    bool filled = true;           // W_RECT ทึบไหม / W_LINE ระบายใต้เส้นไหม
    bool axis = false;            // วาดกรอบ + ป้ายค่าสูงสุด-ต่ำสุดริมขวา
    bool autoscale = true;        // false = ใช้ vmin/vmax ที่ส่งมาตรึงสเกล
    float vmin = 0.0f, vmax = 0.0f;
    bool hasThreshold = false;    // จุดที่เกิน threshold ใช้ color2
    float threshold = 0.0f;
    bool values = false;          // พิมพ์ตัวเลขกำกับแท่ง (bar/column)
    uint8_t hole = 55;            // W_DONUT: รูกลางเป็น % ของรัศมี — 0 = พายเต็ม
    uint8_t decimals = 0;         // ทศนิยมของตัวเลขที่ renderer พิมพ์เอง
    uint16_t start = 0;           // ตำแหน่งเริ่มในคลังตัวเลข
    uint8_t count = 0;            // จำนวนจุด (candles = จำนวนแท่ง ไม่ใช่จำนวน float)
    char text[DASH_TEXT_LEN] = "";
    char label[DASH_TEXT_LEN] = ""; // ป้ายกำกับ: หัวเรื่อง KPI / ข้อความกลางโดนัท
    // QR เท่านั้น — payload จริง (เช่น สาย PromptPay) ยาวได้ถึง ~150 ไบต์
    // ยาวเกิน DASH_TEXT_LEN(48) มาก ถ้าใช้ text[] ร่วมจะโดน strlcpy ตัดจนพัง checksum/CRC
    // จึงต้องแยกบัฟเฟอร์ของตัวเอง ไม่ปนกับ text ที่ widget อื่นใช้โชว์บนจอ
    char qrText[160] = "";
};

struct Dashboard {
    Widget widgets[DASH_MAX_WIDGETS];
    uint8_t widgetCount = 0;

    // ทุก widget ตัดตัวเลขจากคลังก้อนเดียวกัน จองแยกต่อชนิดจะเปลือง RAM เกินจำเป็น
    float pool[DASH_POOL_SIZE];
    uint16_t poolUsed = 0;

    bool valid = false;           // มี frame ที่วาดได้อยู่
    bool dirty = false;           // มี frame ใหม่ที่ยังไม่ได้วาด
    unsigned long lastPush = 0;   // ใช้นับ TTL
};

Dashboard dash;

// forward declaration — ตัววาดจออยู่ก่อนชั้น network ในไฟล์ แต่ต้องเรียกตัวแปลง code นี้
const char* weatherCodeToThai(int code);

// true เมื่อยังใช้รหัสผ่านหน้าเว็บที่มาจากโรงงาน
bool usingDefaultWebPass() {
    return strcmp(sysConfig.webPass, DEFAULT_WEB_PASS) == 0;
}

void saveConfigEEPROM() {
    EEPROM.begin(512);
    sysConfig.magic = 0xAD;
    EEPROM.put(0, sysConfig);
    EEPROM.commit();
    EEPROM.end();
    Serial.println("Config Saved to EEPROM!");
}

// อ่าน config จาก EEPROM พร้อม migration chain 0xAB -> 0xAC -> 0xAD
// ทุก branch ต้องกัน string ที่ไม่มี null terminator ก่อนเรียก strlen
void loadConfigEEPROM() {
    EEPROM.begin(512);

    // ชั้นที่ 1: layout ปัจจุบัน (0xAD)
    ClockConfig cfgNew;
    EEPROM.get(0, cfgNew);
    if (cfgNew.magic == 0xAD) {
        cfgNew.ssid[sizeof(cfgNew.ssid) - 1] = '\0';
        if (strlen(cfgNew.ssid) > 0) {
            sysConfig = cfgNew;
            // กัน string ทุกช่องให้ปิดท้ายแน่นอน เผื่อ EEPROM เพี้ยน
            sysConfig.password[sizeof(sysConfig.password) - 1] = '\0';
            sysConfig.city[sizeof(sysConfig.city) - 1] = '\0';
            sysConfig.webUser[sizeof(sysConfig.webUser) - 1] = '\0';
            sysConfig.webPass[sizeof(sysConfig.webPass) - 1] = '\0';
            EEPROM.end();
            Serial.println("Loaded Config from EEPROM (0xAD).");
            Serial.printf("SSID: %s  lat=%.4f lon=%.4f\n", sysConfig.ssid, sysConfig.lat, sysConfig.lon);
            return;
        }
    }

    // ชั้นที่ 2: layout v3.0.0 (0xAC) — ยกทุกอย่างมา แต่ต้อง geocode พิกัดใหม่
    ConfigV300 cfg300;
    EEPROM.get(0, cfg300);
    if (cfg300.magic == 0xAC) {
        cfg300.ssid[sizeof(cfg300.ssid) - 1] = '\0';
        if (strlen(cfg300.ssid) > 0) {
            cfg300.password[sizeof(cfg300.password) - 1] = '\0';
            cfg300.city[sizeof(cfg300.city) - 1] = '\0';
            cfg300.webUser[sizeof(cfg300.webUser) - 1] = '\0';
            cfg300.webPass[sizeof(cfg300.webPass) - 1] = '\0';

            strlcpy(sysConfig.ssid, cfg300.ssid, sizeof(sysConfig.ssid));
            strlcpy(sysConfig.password, cfg300.password, sizeof(sysConfig.password));
            strlcpy(sysConfig.city, cfg300.city, sizeof(sysConfig.city));
            strlcpy(sysConfig.webUser, cfg300.webUser, sizeof(sysConfig.webUser));
            strlcpy(sysConfig.webPass, cfg300.webPass, sizeof(sysConfig.webPass));
            sysConfig.brightness = cfg300.brightness;
            sysConfig.celsius = cfg300.celsius;
            sysConfig.hour12 = cfg300.hour12;
            sysConfig.lat = 0.0f; // บังคับ geocode ใหม่ตอนบูต
            sysConfig.lon = 0.0f;

            EEPROM.end();
            Serial.println("Migrated config 0xAC -> 0xAD (will geocode city).");
            saveConfigEEPROM();
            return;
        }
    }

    // ชั้นที่ 3: layout v2.x (0xAB) — รหัสผ่านหน้าเว็บกลับเป็นค่าเริ่มต้น
    ConfigV2 cfg2;
    EEPROM.get(0, cfg2);
    if (cfg2.magic == 0xAB) {
        cfg2.ssid[sizeof(cfg2.ssid) - 1] = '\0';
        if (strlen(cfg2.ssid) > 0) {
            cfg2.password[sizeof(cfg2.password) - 1] = '\0';
            cfg2.city[sizeof(cfg2.city) - 1] = '\0';

            strlcpy(sysConfig.ssid, cfg2.ssid, sizeof(sysConfig.ssid));
            strlcpy(sysConfig.password, cfg2.password, sizeof(sysConfig.password));
            strlcpy(sysConfig.city, cfg2.city, sizeof(sysConfig.city));
            sysConfig.brightness = cfg2.brightness;
            sysConfig.celsius = cfg2.celsius;
            sysConfig.hour12 = cfg2.hour12;
            // webUser/webPass คงค่าเริ่มต้นไว้ ผู้ใช้ต้องตั้งรหัสใหม่เอง

            EEPROM.end();
            Serial.println("Migrated config 0xAB -> 0xAD (web password reset to default).");
            saveConfigEEPROM();
            return;
        }
    }

    EEPROM.end();
    Serial.println("No EEPROM Config found, using defaults.");
}

// Embedded Smart Web UI HTML in PROGMEM
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Clock Pro</title>
    <style>
        body { background: #0a0c10; color: #f3f4f6; font-family: sans-serif; padding: 15px; text-align: center; margin: 0; }
        .card { background: rgba(22,27,34,0.85); border: 1px solid rgba(255,255,255,0.1); border-radius: 12px; padding: 16px; margin: 10px auto; max-width: 420px; }
        h1 { color: #3b82f6; font-size: 20px; margin: 0 0 4px 0; }
        h3 { color: #93c5fd; margin: 0 0 10px 0; font-size: 15px; }
        input, select { width: 100%; padding: 10px; margin: 5px 0; border-radius: 8px; border: 1px solid #374151; background: #1f2937; color: white; box-sizing: border-box; font-size: 14px; }
        button { width: 100%; padding: 10px; margin: 5px 0; border-radius: 8px; border: none; font-weight: bold; cursor: pointer; font-size: 14px; }
        .btn-blue { background: #3b82f6; color: white; }
        .row { display: flex; justify-content: space-between; align-items: center; font-size: 13px; padding: 5px 2px; border-bottom: 1px solid rgba(255,255,255,0.06); }
        .k { color: #9ca3af; }
        .v { color: #f3f4f6; font-weight: bold; }
        .v.old { color: #f59e0b; }
        .v.up { color: #10b981; }
        .v.down { color: #ef4444; }
        .btn-green { background: #10b981; color: white; }
        .btn-yellow { background: #f59e0b; color: black; }
        .status { font-size: 13px; padding: 6px 10px; border-radius: 6px; margin: 4px 0; }
        .ok { background: #065f46; color: #6ee7b7; }
        .info { background: #1e3a5f; color: #93c5fd; }
        .warn { background: #7c2d12; color: #fdba74; border: 1px solid #ea580c; }
        label { display: block; text-align: left; font-size: 13px; color: #9ca3af; margin-top: 8px; }
        .btn-red { background: #dc2626; color: white; }
        .hint { font-size: 12px; color: #6b7280; text-align: left; margin: 6px 0 0 0; }
        .ver { font-size: 11px; color: #4b5563; margin-top: 2px; }
    </style>
</head>
<body>
    <div class="card">
        <h1>⏰ Smart Clock Pro</h1>
        <p id="wifiStatus" class="status info">กำลังโหลด...</p>
        <p id="ver" class="ver"></p>
    </div>

    <div id="passWarnCard" class="card" style="display:none">
        <p class="status warn">
            ⚠️ ยังใช้รหัสผ่านเริ่มต้นอยู่ ใครที่อยู่ในวง Wi-Fi เดียวกันก็เข้ามาแก้ค่าหรือเขียนเฟิร์มแวร์ทับได้ กรุณาตั้งรหัสใหม่ที่การ์ดด้านล่าง
        </p>
    </div>

    <div class="card">
        <h3>📊 ข้อมูลสดบนจอ</h3>
        <div class="row">
            <span class="k">อากาศ</span>
            <span><span id="wxVal" class="v">-</span><span id="wxDot" class="dot none"></span></span>
        </div>
        <div class="row">
            <span class="k">ราคาทอง XAU/USD</span>
            <span><span id="goldVal" class="v">-</span><span id="goldDot" class="dot none"></span></span>
        </div>
        <p id="dataMeta" class="hint"></p>
        <button id="refreshBtn" class="btn-green" onclick="doRefresh()">🔄 ดึงข้อมูลใหม่ตอนนี้</button>
        <p class="hint">ปกติอากาศดึงใหม่ทุก 10 นาที ทองทุก 5 นาที ถ้าดึงไม่สำเร็จจะลองใหม่ทุก 1 นาที และคงค่าเดิมไว้บนจอพร้อมจุดเหลืองเตือน</p>
    </div>

    <div class="card">
        <h3>📶 ตั้งค่า Wi-Fi</h3>
        <button class="btn-green" onclick="scanWifi()">🔍 สแกนหา Wi-Fi</button>
        <select id="wifi_list"><option value="">กดสแกนก่อน...</option></select>
        <label>รหัสผ่าน Wi-Fi</label>
        <input type="password" id="pass" placeholder="รหัสผ่าน">
        <button class="btn-blue" onclick="saveWifi()">💾 บันทึกและรีสตาร์ท</button>
    </div>

    <div class="card">
        <h3>🌤️ ตั้งค่าเมือง</h3>
        <label>ชื่อเมือง (ภาษาอังกฤษ)</label>
        <input type="text" id="city" placeholder="เช่น Bangkok">
        <button id="cityBtn" class="btn-blue" onclick="saveCity()">💾 บันทึกเมือง</button>
    </div>

    <div class="card">
        <h3>💡 ความสว่างหน้าจอ</h3>
        <input type="range" id="brightness" min="5" max="100" value="80"
               oninput="document.getElementById('brightnessVal').innerText = this.value + '%'"
               onchange="saveBrightness(this.value)">
        <p class="hint">ระดับปัจจุบัน: <span id="brightnessVal">80%</span> — ตอนโชว์ QR เครื่องจะหรี่ลงเองชั่วคราวให้กล้องมือถือโฟกัสง่ายขึ้น แล้วคืนค่านี้ตอนกลับหน้านาฬิกา</p>
    </div>

    <div class="card">
        <h3>🔒 รหัสผ่านหน้าเว็บ</h3>
        <label>ชื่อผู้ใช้</label>
        <input type="text" id="web_user" placeholder="admin" autocomplete="username">
        <label>รหัสผ่านใหม่ (อย่างน้อย 8 ตัวอักษร)</label>
        <input type="password" id="web_pass" placeholder="รหัสผ่านใหม่" autocomplete="new-password">
        <label>ยืนยันรหัสผ่านใหม่</label>
        <input type="password" id="web_pass2" placeholder="พิมพ์ซ้ำอีกครั้ง" autocomplete="new-password">
        <button class="btn-red" onclick="saveAuth()">🔐 บันทึกรหัสผ่าน</button>
        <p class="hint">หลังบันทึก เบราว์เซอร์จะถามรหัสใหม่ในการเข้าครั้งถัดไป ถ้าลืมรหัสต้องล้าง EEPROM ด้วยการแฟลชผ่านสาย</p>
    </div>

    <div class="card">
        <h3>🚀 อัปเดต Firmware (OTA)</h3>
        <form method="POST" action="/update_ota" enctype="multipart/form-data">
            <input type="file" name="update" accept=".bin">
            <button type="submit" class="btn-yellow">⚡ อัปโหลด .bin</button>
        </form>
    </div>

    <script>
        // โหลดค่าปัจจุบันจากเครื่องตอนเปิดหน้าเว็บ
        window.onload = function() {
            fetch('/config')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('city').value = d.city || '';
                    document.getElementById('web_user').value = d.web_user || 'admin';
                    document.getElementById('ver').innerText = 'Firmware v' + (d.version || '-');
                    if (typeof d.brightness !== 'undefined') {
                        document.getElementById('brightness').value = d.brightness;
                        document.getElementById('brightnessVal').innerText = d.brightness + '%';
                    }
                    let ws = document.getElementById('wifiStatus');
                    ws.innerText = 'เชื่อมต่อ Wi-Fi: ' + (d.ssid || '-');
                    ws.className = 'status ok';
                    // โชว์แบนเนอร์เตือนเฉพาะตอนที่ยังไม่เปลี่ยนรหัสเริ่มต้น
                    if (d.default_pass) {
                        document.getElementById('passWarnCard').style.display = 'block';
                    }
                    renderData(d);
                })
                .catch(() => {
                    document.getElementById('wifiStatus').innerText = 'ไม่สามารถโหลดข้อมูลได้';
                });
        };

        function scanWifi() {
            let sel = document.getElementById('wifi_list');
            sel.innerHTML = '<option>กำลังสแกน...</option>';
            fetch('/scanwifi')
                .then(r => r.json())
                .then(data => {
                    sel.innerHTML = '';
                    if (!data.length) { sel.innerHTML = '<option>ไม่พบ Wi-Fi</option>'; return; }
                    data.sort((a,b) => b.rssi - a.rssi);
                    data.forEach(item => {
                        let o = document.createElement('option');
                        o.value = item.ssid;
                        o.innerText = item.ssid + '  (' + item.rssi + ' dBm)';
                        sel.appendChild(o);
                    });
                })
                .catch(() => alert('สแกนไม่สำเร็จ'));
        }

        function saveAuth() {
            let u = document.getElementById('web_user').value.trim();
            let p = document.getElementById('web_pass').value;
            let p2 = document.getElementById('web_pass2').value;
            if (!u) { alert('กรุณากรอกชื่อผู้ใช้'); return; }
            if (p.length < 8) { alert('รหัสผ่านต้องยาวอย่างน้อย 8 ตัวอักษร'); return; }
            if (p !== p2) { alert('รหัสผ่านสองช่องไม่ตรงกัน'); return; }

            fetch('/api/set?key=web_user&value=' + encodeURIComponent(u))
                .then(r => {
                    if (!r.ok) throw new Error('ตั้งชื่อผู้ใช้ไม่สำเร็จ');
                    return fetch('/api/set?key=web_pass&value=' + encodeURIComponent(p));
                })
                .then(r => {
                    if (!r.ok) throw new Error('ตั้งรหัสผ่านไม่สำเร็จ');
                    alert('บันทึกรหัสผ่านใหม่แล้ว ครั้งถัดไปที่เปิดหน้านี้เบราว์เซอร์จะถามรหัสใหม่');
                    document.getElementById('web_pass').value = '';
                    document.getElementById('web_pass2').value = '';
                    document.getElementById('passWarnCard').style.display = 'none';
                })
                .catch(e => alert(e.message));
        }

        // วาดข้อมูลสดจาก /config ลงการ์ดด้านบน
        function renderData(d) {
            let w = d.weather || {};
            let wEl = document.getElementById('wxVal');
            let wDot = document.getElementById('wxDot');
            if (w.valid) {
                wEl.innerText = w.temp_c + '°C  ' + (w.desc || '');
                wDot.className = w.stale ? 'dot stale' : 'dot fresh';
                wDot.title = w.stale ? 'ข้อมูลเก่า ดึงใหม่ไม่สำเร็จ' : 'ข้อมูลสด';
            } else {
                wEl.innerText = 'ยังไม่มีข้อมูล';
                wDot.className = 'dot none';
                wDot.title = 'ยังดึงไม่สำเร็จเลย';
            }

            let g = d.gold || {};
            let gEl = document.getElementById('goldVal');
            let gDot = document.getElementById('goldDot');
            if (g.valid) {
                gEl.innerText = '$' + g.price;
                // สีบอกทิศทางเทียบราคาครั้งก่อน เหมือนที่แสดงบนจอ
                if (g.prev > 0 && g.price > g.prev) gEl.style.color = '#10b981';
                else if (g.prev > 0 && g.price < g.prev) gEl.style.color = '#ef4444';
                else gEl.style.color = '#f3f4f6';
                gDot.className = g.stale ? 'dot stale' : 'dot fresh';
                gDot.title = g.stale ? 'ข้อมูลเก่า ดึงใหม่ไม่สำเร็จ' : 'ข้อมูลสด';
            } else {
                gEl.innerText = 'ยังไม่มีข้อมูล';
                gEl.style.color = '#9ca3af';
                gDot.className = 'dot none';
                gDot.title = 'ยังดึงไม่สำเร็จเลย';
            }

            let meta = [];
            if (d.lat || d.lon) meta.push('พิกัด ' + d.lat + ', ' + d.lon);
            if (d.heap) meta.push('heap ' + d.heap + ' bytes');
            document.getElementById('dataMeta').innerText = meta.join('  •  ');
        }

        function reloadData() {
            return fetch('/config').then(r => r.json()).then(renderData);
        }

        // สั่งให้เครื่องดึงข้อมูลใหม่ทันที ไม่ต้องรอรอบถัดไป
        function doRefresh() {
            let btn = document.getElementById('refreshBtn');
            btn.disabled = true;
            btn.innerText = '⏳ กำลังดึง...';
            fetch('/refresh')
                .then(r => r.json())
                .then(() => reloadData())
                .catch(() => alert('ดึงข้อมูลไม่สำเร็จ'))
                .finally(() => {
                    btn.disabled = false;
                    btn.innerText = '🔄 ดึงข้อมูลใหม่ตอนนี้';
                });
        }

        // ฟังก์ชันนี้เคยหายไปตั้งแต่ v2.x ปุ่มบันทึกเมืองจึงกดไม่ทำงานมาตลอด
        function saveCity() {
            let c = document.getElementById('city').value.trim();
            if (!c) { alert('กรุณากรอกชื่อเมือง'); return; }
            let btn = document.getElementById('cityBtn');
            btn.disabled = true;
            btn.innerText = '⏳ กำลังค้นหาเมือง...';
            fetch('/api/set?key=city&value=' + encodeURIComponent(c))
                .then(r => r.text().then(t => ({ok: r.ok, text: t})))
                .then(res => {
                    if (!res.ok) throw new Error(res.text);
                    // เครื่อง geocode แล้วดึงอากาศให้ทันที ถ้าไม่สำเร็จจะบอกมาในข้อความ
                    if (res.text !== 'OK') alert('บันทึกเมืองแล้ว แต่ยังดึงอากาศไม่ได้: ' + res.text);
                    else alert('บันทึกเมือง "' + c + '" แล้ว');
                    return reloadData();
                })
                .catch(e => alert('บันทึกเมืองไม่สำเร็จ: ' + e.message))
                .finally(() => {
                    btn.disabled = false;
                    btn.innerText = '💾 บันทึกเมือง';
                });
        }

        function saveWifi() {
            let s = document.getElementById('wifi_list').value;
            let p = document.getElementById('pass').value;
            if (!s) { alert('กรุณาเลือก Wi-Fi ก่อน'); return; }
            fetch('/api/set?key=wifi_ssid&value=' + encodeURIComponent(s))
                .then(() => fetch('/api/set?key=wifi_pass&value=' + encodeURIComponent(p)))
                .then(() => {
                    alert('บันทึกข้อมูล Wi-Fi ลง EEPROM เรียบร้อย! เครื่องกำลังรีบูตเพื่อเชื่อมต่อ...');
                    setTimeout(() => fetch('/restart'), 500);
                });
        }

        // ลากแถบเสร็จแล้วค่อยยิง (onchange ไม่ใช่ oninput) กันสแปมคำสั่งไปเครื่องทุกพิกเซลที่ลาก
        function saveBrightness(v) {
            fetch('/api/set?key=lcd_brightness&value=' + encodeURIComponent(v))
                .catch(() => alert('ตั้งความสว่างไม่สำเร็จ'));
        }
    </script>
</body>
</html>
)rawliteral";

// Binary search for glyph offset in new font format
bool findGlyphOffset(uint32_t codepoint, uint32_t &offset) {
    int16_t lo = 0, hi = THAIFONT_GLYPH_COUNT - 1;
    while (lo <= hi) {
        int16_t mid = lo + ((hi - lo) / 2);
        uint32_t cp = pgm_read_dword(&ThaiFontIndex[mid].codepoint);
        if (cp == codepoint) { offset = pgm_read_dword(&ThaiFontIndex[mid].offset); return true; }
        if (cp < codepoint) lo = mid + 1; else hi = mid - 1;
    }
    return false;
}

bool loadGlyph(uint32_t cp, uint8_t rows[THAIFONT_HEIGHT]) {
    uint32_t offset;
    if (!findGlyphOffset(cp, offset)) return false;
    for (uint8_t y = 0; y < THAIFONT_HEIGHT; y++)
        rows[y] = pgm_read_byte(&ThaiFontBitmap[offset + y]);
    return true;
}

bool isThaiCombiningMark(uint32_t cp) {
    if (cp == 0x0E31) return true;                        // ั
    if (cp >= 0x0E34 && cp <= 0x0E3A) return true;        // ิ ี ึ ื ุ ู
    if (cp >= 0x0E47 && cp <= 0x0E4E) return true;        // ็ ่ ้ ๊ ๋ ์
    return false;
}

void drawCell(int16_t x, int16_t y, const uint8_t cell[THAIFONT_HEIGHT], uint16_t color) {
    for (uint8_t row = 0; row < THAIFONT_HEIGHT; row++) {
        uint8_t bits = cell[row];
        for (uint8_t col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) tft.drawPixel(x + col, y + row, color);
        }
    }
}

// drawThaiString: Cell-based renderer with Combining Mark (สระ) support
void drawThaiString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg) {
    uint8_t cell[THAIFONT_HEIGHT];
    uint8_t glyph[THAIFONT_HEIGHT];
    bool haveCell = false;
    int16_t curX = x;

    memset(cell, 0, THAIFONT_HEIGHT);
    const char* ptr = str;

    auto flushCell = [&]() {
        if (!haveCell) return;
        drawCell(curX, y, cell, color);
        curX += 8;
        memset(cell, 0, THAIFONT_HEIGHT);
        haveCell = false;
    };

    while (*ptr) {
        yield();
        uint32_t cp = 0;
        uint8_t b = (uint8_t)*ptr;

        if (b < 0x80) { cp = b; ptr++; }
        else if ((b & 0xE0) == 0xC0) { cp = ((b & 0x1F) << 6) | ((uint8_t)ptr[1] & 0x3F); ptr += 2; }
        else if ((b & 0xF0) == 0xE0) { cp = ((b & 0x0F) << 12) | (((uint8_t)ptr[1] & 0x3F) << 6) | ((uint8_t)ptr[2] & 0x3F); ptr += 3; }
        else { ptr++; continue; }

        if (cp == ' ') { flushCell(); curX += 8; continue; }
        if (cp == '\n') { flushCell(); curX = x; y += THAIFONT_HEIGHT; continue; }

        if (isThaiCombiningMark(cp)) {
            // Merge สระ ลงบน Cell ปัจจุบัน (ไม่เลื่อน cursor)
            if (haveCell && loadGlyph(cp, glyph)) {
                for (uint8_t i = 0; i < THAIFONT_HEIGHT; i++) cell[i] |= glyph[i];
            }
            continue;
        }

        // พยัญชนะหรือ ASCII ตัวใหม่ — flush cell เก่า แล้วเริ่ม cell ใหม่
        flushCell();
        if (curX + 8 > 240) { curX = x; y += THAIFONT_HEIGHT; }

        if (loadGlyph(cp, glyph)) {
            memcpy(cell, glyph, THAIFONT_HEIGHT);
            haveCell = true;
        }
    }
    flushCell();
}

// Scaled Thai renderer — scale=2 คือ font 16x32, scale=3 คือ 24x48
void drawThaiStringScaled(int16_t x, int16_t y, const char* str, uint16_t color, uint8_t scale) {
    if (scale < 1) scale = 1;
    uint8_t cell[THAIFONT_HEIGHT];
    uint8_t glyph[THAIFONT_HEIGHT];
    bool haveCell = false;
    int16_t curX = x;

    memset(cell, 0, THAIFONT_HEIGHT);
    const char* ptr = str;

    auto flushCellScaled = [&]() {
        if (!haveCell) return;
        // วาดแต่ละ pixel เป็น scale x scale block
        for (uint8_t row = 0; row < THAIFONT_HEIGHT; row++) {
            uint8_t bits = cell[row];
            for (uint8_t col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    tft.fillRect(
                        curX + col * scale,
                        y   + row * scale,
                        scale, scale,
                        color
                    );
                }
            }
        }
        curX += 8 * scale;
        memset(cell, 0, THAIFONT_HEIGHT);
        haveCell = false;
    };

    while (*ptr) {
        yield();
        uint32_t cp = 0;
        uint8_t b = (uint8_t)*ptr;

        if (b < 0x80) { cp = b; ptr++; }
        else if ((b & 0xE0) == 0xC0) { cp = ((b & 0x1F) << 6) | ((uint8_t)ptr[1] & 0x3F); ptr += 2; }
        else if ((b & 0xF0) == 0xE0) { cp = ((b & 0x0F) << 12) | (((uint8_t)ptr[1] & 0x3F) << 6) | ((uint8_t)ptr[2] & 0x3F); ptr += 3; }
        else { ptr++; continue; }

        if (cp == ' ') { flushCellScaled(); curX += 8 * scale; continue; }
        if (cp == '\n') { flushCellScaled(); curX = x; y += THAIFONT_HEIGHT * scale; continue; }

        if (isThaiCombiningMark(cp)) {
            if (haveCell && loadGlyph(cp, glyph)) {
                for (uint8_t i = 0; i < THAIFONT_HEIGHT; i++) cell[i] |= glyph[i];
            }
            continue;
        }

        flushCellScaled();
        if (curX + 8 * scale > 240) { curX = x; y += THAIFONT_HEIGHT * scale; }

        if (loadGlyph(cp, glyph)) {
            memcpy(cell, glyph, THAIFONT_HEIGHT);
            haveCell = true;
        }
    }
    flushCellScaled();
}

// Shortcut สำหรับ 2x (16x32 pixels per glyph)
void drawThaiString2x(int16_t x, int16_t y, const char* str, uint16_t color) {
    drawThaiStringScaled(x, y, str, color, 2);
}

void updateWifiStatusLCD() {
    // โหมด dashboard ยึดจอทั้งใบ ห้ามแถบสถานะแทรกทับ
    if (displayMode != MODE_CLOCK) return;

    tft.fillRect(0, 0, 240, 18, ST77XX_BLACK);
    tft.setTextSize(1);
    
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(10, 5);
        tft.printf("IP: %s", WiFi.localIP().toString().c_str());
        // เตือนบนจอเมื่อยังไม่ได้เปลี่ยนรหัสผ่านหน้าเว็บ
        if (usingDefaultWebPass()) {
            tft.setTextColor(ST77XX_RED);
            tft.setCursor(190, 5);
            tft.print("[!PW]");
        }
    } else if (isAPModeActive) {
        tft.setTextColor(ST77XX_ORANGE);
        tft.setCursor(10, 5);
        tft.print("AP: 192.168.4.1 (SmartClock)");
    } else {
        tft.setTextColor(ST77XX_RED);
        tft.setCursor(10, 5);
        tft.print("WiFi: Connecting...");
    }
}

// จุดเตือนเล็กๆ บอกว่าข้อมูลในพื้นที่นั้นเป็นของเก่า (ดึงครั้งล่าสุดไม่สำเร็จ)
void drawStaleDot(int16_t x, int16_t y, bool stale) {
    tft.fillCircle(x, y, 3, stale ? ST77XX_YELLOW : ST77XX_BLACK);
}

// พื้นที่ชื่อเมือง + อากาศ (y 68-140)
void drawWeatherArea() {
    if (displayMode != MODE_CLOCK) return;

    tft.fillRect(0, 68, 240, 74, ST77XX_BLACK);

    drawThaiString2x(5, 70, sysConfig.city, ST77XX_CYAN);

    char line[48];
    if (weather.valid) {
        float shown = sysConfig.celsius ? weather.tempC : (weather.tempC * 9.0f / 5.0f + 32.0f);
        snprintf(line, sizeof(line), "%s %.0f%s",
                 weatherCodeToThai(weather.code), shown,
                 sysConfig.celsius ? "°C" : "°F");
        drawThaiString2x(5, 106, line, ST77XX_GREEN);
    } else {
        drawThaiString2x(5, 106, "รออากาศ...", ST77XX_ORANGE);
    }

    drawStaleDot(230, 76, weather.stale);
}

// กล่องราคาทอง XAU/USD (y 155-235)
void drawGoldArea() {
    if (displayMode != MODE_CLOCK) return;

    tft.fillRect(0, 155, 240, 80, ST77XX_BLACK);
    tft.drawRect(10, 160, 220, 70, ST77XX_ORANGE);
    drawThaiString(18, 165, "ราคาทองคำ XAU/USD", ST77XX_ORANGE, ST77XX_BLACK);

    if (gold.valid) {
        // สีบอกทิศทางเทียบราคาครั้งก่อน ขึ้นเขียว ลงแดง เท่าเดิมหรือครั้งแรกเป็นขาว
        uint16_t color = ST77XX_WHITE;
        if (gold.prevPrice > 0.0f) {
            if (gold.price > gold.prevPrice)      color = ST77XX_GREEN;
            else if (gold.price < gold.prevPrice) color = ST77XX_RED;
        }
        char priceStr[16];
        snprintf(priceStr, sizeof(priceStr), "$%.2f", gold.price);
        tft.setTextColor(color);
        tft.setTextSize(3);
        tft.setCursor(20, 192);
        tft.print(priceStr);
    } else {
        tft.setTextColor(ST77XX_ORANGE);
        tft.setTextSize(2);
        tft.setCursor(20, 196);
        tft.print("Loading...");
    }

    drawStaleDot(218, 170, gold.stale);
}

void initStaticLCDScreen() {
    tft.fillScreen(ST77XX_BLACK);
    updateWifiStatusLCD();
    drawWeatherArea();
    drawGoldArea();
}

// หน้า loading ตอนบูต — โชว์ระหว่างต่อ Wi-Fi/ดึงข้อมูลรอบแรก กันจอมืดหรือค้าง
// จอเปล่าหลายวินาทีจนดูเหมือนเครื่องแฮงก์ ก่อนจะสลับไปหน้านาฬิกาจริงทีเดียว
void drawBootScreen() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(3);
    tft.setCursor(26, 78);
    tft.print("SMART");
    tft.setCursor(26, 108);
    tft.print("CLOCK");
    tft.setTextColor(ST77XX_BLUE);
    tft.setTextSize(1);
    tft.setCursor(92, 142);
    tft.printf("v%s", FW_VERSION);
}

// อัปเดตแค่บรรทัดสถานะ ไม่วาดทับโลโก้ทั้งจอซ้ำ — dotCount 0-3 ทำจุดวิ่งให้ดูไม่นิ่ง
void drawBootStatus(const char* thaiStatus, uint8_t dotCount) {
    tft.fillRect(0, 168, 240, 20, ST77XX_BLACK);
    char buf[64];
    char dots[4] = "";
    for (uint8_t i = 0; i < dotCount && i < 3; i++) dots[i] = '.';
    dots[dotCount > 3 ? 3 : dotCount] = '\0';
    snprintf(buf, sizeof(buf), "%s%s", thaiStatus, dots);
    drawThaiString(20, 170, buf, ST77XX_ORANGE, ST77XX_BLACK);
}

void updateTimeOnly() {
    if (displayMode != MODE_CLOCK) return;

    // ล้างเฉพาะพื้นที่แสดงเวลาด้านบน
    tft.fillRect(0, 20, 240, 45, ST77XX_BLACK);
    
    time_t now = time(nullptr);
    if (now > 1000000) {
        struct tm t;
        localtime_r(&now, &t);
        char timeStr[16];
        if (sysConfig.hour12) {
            int h12 = t.tm_hour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", h12, t.tm_min, t.tm_sec);
        } else {
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        }
        tft.setTextColor(ST77XX_YELLOW);
        tft.setTextSize(4); // ปรับเวลาเป็นตัวใหญ่ขึ้น 4 เท่า ให้เห็นชัดเจน
        tft.setCursor(25, 25);
        tft.print(timeStr);
    } else {
        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(2);
        tft.setCursor(45, 30);
        tft.print("NTP Syncing...");
    }
}

// ---------------------------------------------------------------------------
// Dashboard renderer — วาดจาก display list ที่อยู่ใน RAM
// ทุกอย่างเป็น primitive พิกัดสัมบูรณ์ที่แมป 1:1 ลง Adafruit_GFX
// ---------------------------------------------------------------------------

// แปลงชื่อสีเป็น RGB565 รับทั้งชื่อและ #RRGGBB
uint16_t parseColor(const char* s, uint16_t fallback) {
    if (!s || !*s) return fallback;

    if (s[0] == '#' && strlen(s) == 7) {
        long v = strtol(s + 1, nullptr, 16);
        uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    if (!strcmp(s, "white"))  return ST77XX_WHITE;
    if (!strcmp(s, "black"))  return ST77XX_BLACK;
    if (!strcmp(s, "red"))    return ST77XX_RED;
    if (!strcmp(s, "green"))  return ST77XX_GREEN;
    if (!strcmp(s, "blue"))   return ST77XX_BLUE;
    if (!strcmp(s, "cyan"))   return ST77XX_CYAN;
    if (!strcmp(s, "yellow")) return ST77XX_YELLOW;
    if (!strcmp(s, "orange")) return ST77XX_ORANGE;
    if (!strcmp(s, "magenta"))return ST77XX_MAGENTA;
    if (!strcmp(s, "grey") || !strcmp(s, "gray")) return 0x8410;
    return fallback;
}

// ความสว่างเต็มจ้าเกินไปสำหรับกล้องมือถือ (ผู้ใช้แจ้งว่าโฟกัส/สแกน QR ไม่ติด)
// หรี่ลงเฉพาะตอนเฟรมมี QR widget อยู่ ค่านี้ทดสอบแล้วยัง scan ติดชัดบนจอ ST7789
#define QR_DIM_BRIGHTNESS_PCT 30

// ตั้งความสว่างจอเป็น % (0-100) — ใช้ตัวเดียวกับที่ config handler ใช้อยู่แล้ว
// (TFT_BL active-low ผ่าน analogWrite จึงต้อง map กลับทาง 0-100 -> 1023-0)
void setBacklightPct(uint8_t pct) {
    if (pct > 100) pct = 100;
    analogWrite(TFT_BL, map(pct, 0, 100, 1023, 0));
}

#define DASH_AXIS_GUTTER 44   // ที่ริมขวาที่เว้นให้ป้ายค่าสูงสุด-ต่ำสุด
#define DASH_GRID_COLOR  0x4208
#define DASH_MUTED_COLOR 0x8410

// ตัวเลขจาก pool ของ widget — เข้าถึงผ่าน helper เพื่อกันอ่านเลย poolUsed
inline float wval(const Widget &g, uint16_t i) {
    uint16_t at = g.start + i;
    return (at < dash.poolUsed) ? dash.pool[at] : 0.0f;
}

// หาช่วงค่าที่จะใช้เป็นสเกลแกน — autoscale=false ใช้ vmin/vmax ที่ส่งมาตรงๆ
// stride>1 สำหรับ candles ที่จุดละ 4 ค่า (ดู hi/lo ทุกช่อง)
void widgetRange(const Widget &g, float &lo, float &hi, uint8_t stride = 1) {
    if (!g.autoscale) {
        lo = g.vmin;
        hi = g.vmax;
    } else {
        lo = hi = wval(g, 0);
        for (uint16_t i = 1; i < (uint16_t)g.count * stride; i++) {
            float v = wval(g, i);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        // กราฟแท่งต้องเห็นความยาวเทียบกัน จึงตรึงฐานที่ 0 เมื่อค่าเป็นบวกล้วน
        if (stride == 1 && (g.type == W_COLUMN || g.type == W_HBAR)) {
            if (lo > 0.0f) lo = 0.0f;
            if (hi < 0.0f) hi = 0.0f;
        }
    }

    // กันหารศูนย์ตอนค่านิ่งสนิททุกจุด
    if (hi - lo < 0.0001f) {
        float pad = (fabsf(hi) > 0.0001f) ? fabsf(hi) * 0.01f : 1.0f;
        lo -= pad / 2.0f;
        hi += pad / 2.0f;
    }
}

// ป้ายค่าสูงสุด/ต่ำสุดริมขวา + กรอบ plot ใช้ร่วมกันทุกกราฟที่มีแกน
void drawAxisFrame(const Widget &g, int16_t plotW, float lo, float hi) {
    tft.drawRect(g.x, g.y, plotW, g.h, DASH_GRID_COLOR);
    if (g.w - plotW < 20) return;

    char buf[16];
    tft.setTextSize(1);
    tft.setTextColor(DASH_MUTED_COLOR);

    snprintf(buf, sizeof(buf), "%.*f", g.decimals, hi);
    tft.setCursor(g.x + plotW + 3, g.y);
    tft.print(buf);

    snprintf(buf, sizeof(buf), "%.*f", g.decimals, lo);
    tft.setCursor(g.x + plotW + 3, g.y + g.h - 8);
    tft.print(buf);
}

// วาดแท่งเทียน autoscale จากช่วง high/low ของชุดข้อมูลที่ส่งมา
// ฝั่ง HA ส่งแค่ตัวเลข OHLC ไม่ต้องคิดพิกัดพิกเซลเอง
void drawCandles(const Widget &g) {
    float lo, hi;
    widgetRange(g, lo, hi, 4); // จุดละ 4 ค่า o,h,l,c
    const float span = hi - lo;

    const int16_t plotW = g.axis ? ((g.w > 60) ? g.w - DASH_AXIS_GUTTER : g.w) : g.w;
    auto toY = [&](float p) -> int16_t {
        float frac = (p - lo) / span;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        return (int16_t)(g.y + g.h - 1 - frac * (g.h - 1)); // ค่าสูง = y น้อย
    };

    if (g.axis) drawAxisFrame(g, plotW, lo, hi);

    // ความกว้างต่อแท่ง เหลือ 1px เป็นช่องไฟ
    int16_t slot = plotW / g.count;
    if (slot < 1) slot = 1;
    int16_t body = (slot > 2) ? (slot - 1) : 1;

    for (uint8_t i = 0; i < g.count; i++) {
        yield(); // จอ 240px วาด 40 แท่งใช้เวลาพอที่ต้องคืน CPU ให้ SDK

        float op = wval(g, i * 4), chi = wval(g, i * 4 + 1);
        float clo = wval(g, i * 4 + 2), cl = wval(g, i * 4 + 3);

        int16_t bx = g.x + i * slot;
        int16_t mid = bx + body / 2;
        uint16_t col = (cl >= op) ? g.color : g.color2;

        tft.drawLine(mid, toY(chi), mid, toY(clo), col); // ไส้เทียน high-low

        // ตัวแท่ง open-close — ราคาเท่ากันให้เหลือเส้นบาง 1px ไม่ให้หาย
        int16_t yO = toY(op), yC = toY(cl);
        int16_t top = (yO < yC) ? yO : yC;
        int16_t bh = abs(yC - yO);
        if (bh < 1) bh = 1;
        tft.fillRect(bx, top, body, bh, col);
    }
}

// Column chart — แท่งตั้งเรียงซ้ายไปขวา ระบายจากเส้นฐาน (ค่า 0 ถ้าอยู่ในช่วง)
// เหมาะกับข้อมูลตามลำดับเวลา เพราะแกนนอนอ่านเป็นไทม์ไลน์ได้ตรงตัว
void drawColumns(const Widget &g) {
    float lo, hi;
    widgetRange(g, lo, hi);
    const float span = hi - lo;

    const int16_t plotW = g.axis ? ((g.w > 60) ? g.w - DASH_AXIS_GUTTER : g.w) : g.w;
    auto toY = [&](float v) -> int16_t {
        float frac = (v - lo) / span;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        return (int16_t)(g.y + g.h - 1 - frac * (g.h - 1));
    };

    if (g.axis) drawAxisFrame(g, plotW, lo, hi);

    const int16_t yBase = toY((lo <= 0.0f && hi >= 0.0f) ? 0.0f : lo);

    int16_t slot = plotW / g.count;
    if (slot < 1) slot = 1;
    int16_t body = (slot > 2) ? (slot - 1) : 1;

    for (uint8_t i = 0; i < g.count; i++) {
        yield();
        float v = wval(g, i);
        int16_t yv = toY(v);

        int16_t top = (yv < yBase) ? yv : yBase;
        int16_t bh = abs(yv - yBase);
        if (bh < 1) bh = 1; // ค่าเท่าฐานพอดี ยังต้องเห็นขีดบางๆ

        uint16_t col = (g.hasThreshold && v >= g.threshold) ? g.color2 : g.color;
        tft.fillRect(g.x + i * slot, top, body, bh, col);
    }

    // เส้นฐานทับบนแท่ง ให้เห็นว่าค่าลบเริ่มจากไหน
    if (lo < 0.0f && hi > 0.0f) {
        tft.drawFastHLine(g.x, yBase, plotW, DASH_MUTED_COLOR);
    }
}

// Horizontal bar chart — แท่งนอนเรียงบนลงล่าง เทียบอันดับระหว่างรายการ
// ป้ายชื่อรายการอยู่ในกรอบซ้าย ค่าพิมพ์ท้ายแท่ง (values=true)
void drawHBars(const Widget &g) {
    float lo, hi;
    widgetRange(g, lo, hi);
    const float span = hi - lo;

    // เว้นขวาให้ตัวเลขท้ายแท่งเมื่อสั่ง values ไม่งั้นใช้ความกว้างเต็ม
    const int16_t plotW = (g.values && g.w > 60) ? g.w - 40 : g.w;
    const int16_t xBase = (lo < 0.0f && hi > 0.0f)
                        ? (int16_t)(g.x + (0.0f - lo) / span * (plotW - 1))
                        : g.x;

    int16_t slot = g.h / g.count;
    if (slot < 1) slot = 1;
    int16_t body = (slot > 2) ? (slot - 1) : 1;

    for (uint8_t i = 0; i < g.count; i++) {
        yield();
        float v = wval(g, i);
        float frac = (v - lo) / span;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;

        int16_t xEnd = (int16_t)(g.x + frac * (plotW - 1));
        int16_t bx = (xEnd < xBase) ? xEnd : xBase;
        int16_t bw = abs(xEnd - xBase);
        if (bw < 1) bw = 1;

        int16_t by = g.y + i * slot;
        uint16_t col = (g.hasThreshold && v >= g.threshold) ? g.color2 : g.color;
        tft.fillRect(bx, by, bw, body, col);

        if (g.values) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.*f", g.decimals, v);
            tft.setTextSize(1);
            tft.setTextColor(DASH_MUTED_COLOR);
            // จัดกลางแนวตั้งของแท่ง ฟอนต์ในตัวสูง 8px
            tft.setCursor(g.x + plotW + 3, by + (body > 8 ? (body - 8) / 2 : 0));
            tft.print(buf);
        }
    }

    if (lo < 0.0f && hi > 0.0f) {
        tft.drawFastVLine(xBase, g.y, g.h, DASH_MUTED_COLOR);
    }
}

// Line graph — จุดต่อเส้น เหมาะกับแนวโน้มต่อเนื่อง
// filled=true ระบายใต้เส้นด้วยเส้นตั้งถี่ ทำเป็น area chart ได้โดยไม่ต้องมี polygon fill
void drawLineChart(const Widget &g) {
    float lo, hi;
    widgetRange(g, lo, hi);
    const float span = hi - lo;

    const int16_t plotW = g.axis ? ((g.w > 60) ? g.w - DASH_AXIS_GUTTER : g.w) : g.w;
    auto toY = [&](float v) -> int16_t {
        float frac = (v - lo) / span;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        return (int16_t)(g.y + g.h - 1 - frac * (g.h - 1));
    };

    if (g.axis) drawAxisFrame(g, plotW, lo, hi);

    // จุดเดียวไม่มีเส้น วาดเป็นขีดแนวนอนให้เห็นระดับค่า
    if (g.count == 1) {
        tft.drawFastHLine(g.x, toY(wval(g, 0)), plotW, g.color);
        return;
    }

    const float step = (float)(plotW - 1) / (float)(g.count - 1);
    int16_t px = g.x, py = toY(wval(g, 0));

    for (uint8_t i = 1; i < g.count; i++) {
        yield();
        int16_t cx = (int16_t)(g.x + step * i);
        int16_t cy = toY(wval(g, i));

        if (g.filled) {
            // ระบายจากเส้นถึงขอบล่างของกรอบ ทีละคอลัมน์ระหว่างสองจุด
            for (int16_t sx = px; sx <= cx; sx++) {
                float t = (cx > px) ? (float)(sx - px) / (float)(cx - px) : 0.0f;
                int16_t sy = (int16_t)(py + t * (cy - py));
                tft.drawFastVLine(sx, sy, g.y + g.h - sy, g.color2);
            }
        }

        tft.drawLine(px, py, cx, cy, g.color);
        px = cx;
        py = cy;
    }
}

// Donut / Pie — สัดส่วนต่อยอดรวมเท่านั้น ค่าลบไม่มีความหมายจึงตัดออก
// วาดด้วยการยิงเส้นรัศมีทีละองศา เพราะ Adafruit_GFX ไม่มี fillArc
// hole=0 ได้พายเต็มวง, hole>0 เจาะรูกลางเป็น % ของรัศมี
// เดิมยิงเส้นรัศมีทีละครึ่งองศา (drawLine จากรัศมีในไปนอก) แล้วพบบนฮาร์ดแวร์จริง
// ว่าขอบนอกแหว่งเป็นซี่ๆ ที่แนวทแยง — สาเหตุคือ (int16_t) ตัดทศนิยมเข้าหาศูนย์
// ปัดรัศมีจริงของปลายเส้นไม่เท่ากันทุกมุม (ที่ 45° ได้ 63.64 ไม่ใช่ 65 ที่ตั้งใจ)
// ทำให้ขอบนอกแกว่ง ~1.4px และ 25% ของเส้นวาดทับตำแหน่งเดิมเปล่าๆ
// เปลี่ยนมาใช้ fillTriangle เป็นพัด (เสี้ยวละ ≤20° กันคอร์ดเว้าจนเห็นเหลี่ยม)
// จากจุดศูนย์กลางออกไปถึง rOut แล้วเจาะรูกลางด้วยวงกลมทึบทับ
// ผลจากการจำลอง: รูในเนื้อวงเหลือ 0% (จากเดิม 3.55%) และถูกกว่าด้วย — เฉลี่ย
// ต้องมีสามเหลี่ยมแค่ ~18 รูปเทียบ drawLine 722 ครั้งสำหรับวงเต็ม
// วาดช้า (ผู้ใช้รายงาน) เพราะ cosf/sinf เป็น float software emulation บน ESP8266
// (ไม่มี FPU ฮาร์ดแวร์) แพงกว่าการวาดเองมาก — ตามไอเดียจาก fillArc2 ของ bodmer
// (forum.arduino.cc/t/adafruit_gfx-fillarc/397741) จึงลดจำนวนครั้งที่เรียก
// สองทาง: (1) ขยายเสี้ยวจาก 8° เป็น 20° — sagitta ที่ rOut สูงสุด ~120px ยังต่ำกว่า
// 1px (65*[1-cos10°]≈0.99px) จึงยังไม่เห็นเหลี่ยม แต่ลดจำนวนเสี้ยวลง ~60%
// (2) จุดเริ่มของเสี้ยวถัดไปคือจุดจบของเสี้ยวก่อน ไม่ต้องคำนวณ cosf/sinf ซ้ำ
// สองข้อรวมกันลด cosf/sinf ต่อวงจาก ~180 ครั้งเหลือ ~19 ครั้ง (~90%)
void drawDonut(const Widget &g) {
    // จานสีวนใช้ตามลำดับ segment — ผู้ใช้ส่งสีเองไม่ได้เพราะ 1 widget มีสีหลักสองช่อง
    static const uint16_t palette[] = {
        0x07E0, 0xFD20, 0x001F, 0xF800, 0x07FF, 0xFFE0, 0xF81F, 0x8410
    };
    const uint8_t paletteCount = sizeof(palette) / sizeof(palette[0]);

    float total = 0.0f;
    for (uint8_t i = 0; i < g.count; i++) {
        float v = wval(g, i);
        if (v > 0.0f) total += v;
    }
    if (total <= 0.0f) return;

    // วงกลมต้องกลม จึงใช้ด้านที่สั้นกว่าเป็นตัวคุมรัศมี
    const int16_t side = (g.w < g.h) ? g.w : g.h;
    const int16_t rOut = side / 2;
    const int16_t rIn = (int16_t)(rOut * (g.hole > 95 ? 95 : g.hole) / 100);
    const int16_t cx = g.x + g.w / 2;
    const int16_t cy = g.y + g.h / 2;
    if (rOut < 4) return;

    float angle = -90.0f; // เริ่มที่ 12 นาฬิกา ตามที่คนอ่านกราฟวงกลมคาด

    // จุดบนขอบนอกที่ตำแหน่ง angle ปัจจุบัน — คำนวณครั้งเดียวตอนเริ่ม แล้วรีไซเคิล
    // เป็นจุดเริ่มของเสี้ยวถัดไปเรื่อยๆ กัน cosf/sinf ซ้ำที่จุดเดียวกันสองครั้ง
    float rad0 = angle * 0.01745329f;
    int16_t xPrev = cx + (int16_t)roundf(cosf(rad0) * rOut);
    int16_t yPrev = cy + (int16_t)roundf(sinf(rad0) * rOut);

    for (uint8_t i = 0; i < g.count; i++) {
        float v = wval(g, i);
        if (v <= 0.0f) continue;

        float sweep = v / total * 360.0f;
        uint16_t col = palette[i % paletteCount];

        // เสี้ยวไม่เกิน 20° ต่อชิ้น — ที่ rOut สูงสุด ~120px บนจอนี้ ความเว้าของคอร์ด
        // (sagitta) ที่ 20° ยังต่ำกว่า 1px จึงไม่เห็นเหลี่ยม แต่เรียก cosf/sinf น้อยลง
        // กว่าเดิม (8°) ~60% — cosf/sinf เป็น software float บน ESP8266 (ไม่มี FPU)
        // แพงกว่าการวาดเองมาก ตามไอเดียจาก fillArc2 ของ bodmer
        // (forum.arduino.cc/t/adafruit_gfx-fillarc/397741)
        uint8_t n = (uint8_t)(sweep / 20.0f) + 1;
        float step = sweep / n;
        for (uint8_t k = 0; k < n; k++) {
            float a1 = (angle + (k + 1) * step) * 0.01745329f;
            int16_t x1 = cx + (int16_t)roundf(cosf(a1) * rOut);
            int16_t y1 = cy + (int16_t)roundf(sinf(a1) * rOut);
            tft.fillTriangle(cx, cy, xPrev, yPrev, x1, y1, col);
            xPrev = x1;
            yPrev = y1;
        }
        angle += sweep;
        yield(); // สูงสุด ~18 สามเหลี่ยมต่อวง แต่ยังคืน CPU ให้ SDK ทุก segment
    }

    // เจาะรูกลาง — สมมติพื้นหลังเป็นดำเสมอ เหมือน widget อื่นในชุดนี้ที่วาดบนจอที่
    // fillScreen(BLACK) มาก่อนแล้ว ถ้าจะซ้อนโดนัทบนพื้นสีอื่นต้องแก้จุดนี้เพิ่ม
    if (rIn > 0) tft.fillCircle(cx, cy, rIn, ST77XX_BLACK);

    // ข้อความกลางโดนัท เช่นยอดรวม — วางได้เฉพาะเมื่อรูใหญ่พอ
    if (g.label[0] && rIn >= 14) {
        int16_t tw = (int16_t)strlen(g.label) * 6; // ฟอนต์ในตัว 6px ต่อตัวอักษร
        tft.setTextSize(1);
        tft.setTextColor(g.color);
        tft.setCursor(cx - tw / 2, cy - 4);
        tft.print(g.label);
    }
}

// KPI card — ตัวเลขสรุปค่าเดียว ป้ายกำกับตัวเล็กด้านบน
// ใช้ฟอนต์ในตัวของ GFX ไม่ใช่ฟอนต์ไทย เพราะต้องรู้ความกว้างเพื่อจัดกลาง
void drawKpi(const Widget &g) {
    if (g.axis) tft.drawRect(g.x, g.y, g.w, g.h, DASH_GRID_COLOR);

    // ป้ายกำกับ 1x บนสุดของการ์ด
    if (g.label[0]) {
        int16_t tw = (int16_t)strlen(g.label) * 6;
        tft.setTextSize(1);
        tft.setTextColor(DASH_MUTED_COLOR);
        tft.setCursor(g.x + (g.w - tw) / 2, g.y + 4);
        tft.print(g.label);
    }

    // ตัวเลขหลัก: ใช้ text ถ้าส่งมา (จัดรูปแบบฝั่ง HA ได้อิสระ) ไม่งั้นพิมพ์จาก data[0]
    char buf[DASH_TEXT_LEN];
    if (g.text[0]) {
        strlcpy(buf, g.text, sizeof(buf));
    } else if (g.count > 0) {
        snprintf(buf, sizeof(buf), "%.*f", g.decimals, wval(g, 0));
    } else {
        return;
    }

    // ย่อ scale ลงจนพอดีกรอบ ดีกว่าปล่อยตัวเลขล้นออกนอกการ์ด
    uint8_t sc = (g.scale < 1) ? 1 : g.scale;
    int16_t len = (int16_t)strlen(buf);
    while (sc > 1 && len * 6 * sc > g.w - 4) sc--;

    int16_t tw = len * 6 * sc;
    int16_t th = 8 * sc;
    uint16_t col = g.color;
    if (g.hasThreshold && g.count > 0 && wval(g, 0) >= g.threshold) col = g.color2;

    tft.setTextSize(sc);
    tft.setTextColor(col);
    // จัดกลางกรอบ เผื่อที่ป้ายกำกับด้านบน 10px เมื่อมีป้าย
    int16_t top = g.y + (g.label[0] ? 10 : 0);
    int16_t boxH = g.h - (g.label[0] ? 10 : 0);
    tft.setCursor(g.x + (g.w - tw) / 2, top + (boxH - th) / 2);
    tft.print(buf);
}

// เกจแนวนอนค่าเดียว — เทียบค่ากับช่วง vmin..vmax ที่ตรึงไว้
void drawGauge(const Widget &g) {
    if (g.count == 0) return;

    float lo = g.vmin, hi = g.vmax;
    if (hi - lo < 0.0001f) { lo = 0.0f; hi = 100.0f; } // ไม่ส่งช่วงมา ถือเป็นเปอร์เซ็นต์

    float frac = (wval(g, 0) - lo) / (hi - lo);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    tft.drawRect(g.x, g.y, g.w, g.h, DASH_GRID_COLOR);
    int16_t fillW = (int16_t)(frac * (g.w - 2));
    uint16_t col = (g.hasThreshold && wval(g, 0) >= g.threshold) ? g.color2 : g.color;
    if (fillW > 0) tft.fillRect(g.x + 1, g.y + 1, fillW, g.h - 2, col);
}

// ---------------------------------------------------------------------------
// PromptPay QR payload builder (มาตรฐาน EMVCo QR Code for Payment Systems /
// ธนาคารแห่งประเทศไทย "Thai QR Payment") — ประกอบ tag-length-value string ตาม
// สเปกสาธารณะ ไม่ใช่ของลับใดๆ (ตัวอย่างสาย, mask CRC ฯลฯ หาอ่านได้ทั่วไป)
// รับ id ได้ทั้งเบอร์โทร (10 หลัก ขึ้นต้น 0) และเลขบัตรประชาชน (13 หลัก)
// amount<=0 หมายถึงไม่ตรึงยอด ผู้จ่ายกรอกเองปลายทาง (เปิดสาย tag 54 ทิ้งไว้)
// ---------------------------------------------------------------------------

// helper: เติม tag(2 หลัก) + length(2 หลัก) + value ต่อ buffer พร้อมขยับ length รวม
static void ppAppend(String &out, const char* tag, const String &val) {
    char len[3];
    snprintf(len, sizeof(len), "%02u", (unsigned)val.length());
    out += tag;
    out += len;
    out += val;
}

// CRC16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0x0000)
// ตรวจกับ test vector มาตรฐานของ PromptPay แล้วว่าตรง (payload ตัวอย่างจากสเปก
// ที่ลงท้าย ...6304DC9E คำนวณ CRC ได้ DC9E ตรงกับที่สเปกกำหนดไว้พอดี)
uint16_t crc16CcittFalse(const char* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

// สร้างสาย PromptPay เต็ม พร้อม CRC ต่อท้าย — คืน String ว่างถ้า id ไม่ใช่รูปแบบที่รองรับ
String buildPromptPayPayload(const char* id, float amount) {
    if (!id || !*id) return String();

    // เก็บเฉพาะตัวเลขจาก id ที่รับมา กันเผื่อผู้ใช้พิมพ์ขีด/เว้นวรรคปนมา
    String digits;
    for (const char* p = id; *p; p++) if (isdigit((unsigned char)*p)) digits += *p;

    String targetTag, targetVal;
    if (digits.length() == 10 && digits[0] == '0') {
        // เบอร์โทร — แปลงเป็น 66 + เบอร์แบบตัด 0 นำหน้า ตามสเปก tag 01 (mobile)
        targetTag = "01";
        targetVal = "0066" + digits.substring(1);
    } else if (digits.length() == 13) {
        // เลขบัตรประชาชน 13 หลัก — tag 02 (national ID)
        targetTag = "02";
        targetVal = digits;
    } else {
        return String(); // รูปแบบไม่ตรงทั้งเบอร์โทรและเลขบัตร ปฏิเสธไปเลยดีกว่าสร้าง QR ผิด
    }

    // Merchant Account Information (tag 29) ของ PromptPay: GUID + ตัวระบุผู้รับเงิน
    String mai;
    ppAppend(mai, "00", "A000000677010111"); // GUID มาตรฐาน PromptPay ตรึงตายตัว
    ppAppend(mai, targetTag.c_str(), targetVal);

    String payload;
    ppAppend(payload, "00", "01");             // Payload Format Indicator
    ppAppend(payload, "01", amount > 0 ? "12" : "11"); // Point of Initiation: 12=ตรึงยอด, 11=ไม่ตรึง
    ppAppend(payload, "29", mai);
    ppAppend(payload, "53", "764");            // Transaction Currency = THB (ISO 4217)
    if (amount > 0) {
        char amtBuf[16];
        snprintf(amtBuf, sizeof(amtBuf), "%.2f", amount);
        ppAppend(payload, "54", String(amtBuf));
    }
    ppAppend(payload, "58", "TH");             // Country Code

    // ต่อ tag CRC (63) พร้อม length "04" ก่อนคำนวณ เพราะ CRC ต้องครอบคลุมส่วนนี้ด้วย
    payload += "6304";
    uint16_t crc = crc16CcittFalse(payload.c_str(), payload.length());
    char crcHex[5];
    snprintf(crcHex, sizeof(crcHex), "%04X", crc);
    payload += crcHex;

    return payload;
}

// QR code — เข้ารหัส g.qrText (บัฟเฟอร์แยก 160 ไบต์ ไม่ใช้ g.text/DASH_TEXT_LEN=48
// เพราะ payload PromptPay จริงยาว 80-90+ ตัวอักษรอยู่แล้ว ถ้าใช้ g.text จะถูก strlcpy
// ตัดทิ้งแบบเงียบๆ กลายเป็น QR ที่สแกนได้แต่จ่ายเงินผิด/error) ด้วยไลบรารีที่พอร์ตมาจาก
// github.com/anunpanya/ESP8266_QRcode (ตัวเดิม tz1/qrduino) ตรึงไว้ที่ VERSION 7 / ECC-L
// เป็นตาราง 45x45 module รองรับข้อมูลดิบ ~150 ไบต์
// ไลบรารีเดิมมี wrapper ผูกกับจอ SSD1306 โดยเฉพาะ (qrcode.cpp/.h ไม่ได้พอร์ตมา) จึงเรียก
// qrencode() ตรงๆ แล้ววาด module เองด้วย fillRect รวมแนวนอนเป็นแถบเดียวต่อ run
// (สีเดียวกันติดกันในแนว x) กัน SPI call ถี่เกินจำเป็น แบบเดียวกับที่ทำใน drawDonut
void drawQr(const Widget &g) {
    if (!g.qrText[0]) return;

    // strinbuf ยาว 270 ไบต์ (ประกาศจริงใน frame.c) แต่ qrencode.h ประกาศแบบ
    // extern unsigned char strinbuf[] (incomplete type) เอา sizeof() ไม่ได้ — ต้องฮาร์ดโค้ด
    // ตรึง VERSION7/ECC-L ใช้จริงแค่ ~150 ไบต์แรก ตัดข้อความยาวเกินไว้ก่อนกันเผื่อ
    const size_t STRINBUF_LEN = 270;
    size_t len = strlen(g.qrText);
    if (len > 150) len = 150;
    memset(strinbuf, 0, STRINBUF_LEN);
    memcpy(strinbuf, g.qrText, len);
    strinbuf[len] = 0;

    qrencode(); // strinbuf เข้า, qrframe ออก เป็นตาราง WD x WD (45x45) บิตแพ็ก

    const int16_t side = (g.w < g.h) ? g.w : g.h;
    int16_t px = side / WD; // ขนาดพิกเซลต่อ module
    if (px < 1) px = 1;
    const int16_t qrSize = px * WD;
    const int16_t ox = g.x + (g.w - qrSize) / 2; // จัดกลางกรอบที่ขอ
    const int16_t oy = g.y + (g.h - qrSize) / 2;

    uint16_t dark = g.color;
    uint16_t light = g.color2;

    // เคลียร์พื้นด้วย light ก่อน ครอบคลุม quiet zone รอบขอบด้วย
    tft.fillRect(g.x, g.y, g.w, g.h, light);

    for (uint8_t y = 0; y < WD; y++) {
        uint8_t x = 0;
        while (x < WD) {
            uint8_t bit = QRBIT(x, y);
            uint8_t runStart = x;
            while (x < WD && QRBIT(x, y) == bit) x++;
            if (bit) { // วาดเฉพาะ module มืด เพราะพื้น light ทาไว้แล้วทั้งกรอบ
                tft.fillRect(ox + runStart * px, oy + y * px, (x - runStart) * px, px, dark);
            }
        }
        yield();
    }
}

void renderDashboard() {
    // ล้างทั้งจอเฉพาะตอนวาด frame ใหม่ ไม่ได้ล้างทุกรอบ loop
    tft.fillScreen(ST77XX_BLACK);

    // มี QR อยู่ในเฟรมนี้ไหม — ใช้หรี่จอกันสว่างจ้าจนกล้องมือถือโฟกัสไม่ติด (ผู้ใช้แจ้งบัค)
    // เช็คก่อนวาด ไม่ใช่ระหว่างวาด เพราะ QR อาจไม่ใช่ widget แรกในลิสต์
    bool hasQr = false;
    for (uint8_t i = 0; i < dash.widgetCount; i++) {
        if (dash.widgets[i].type == W_QR && dash.widgets[i].qrText[0]) { hasQr = true; break; }
    }
    setBacklightPct(hasQr ? QR_DIM_BRIGHTNESS_PCT : sysConfig.brightness);

    for (uint8_t i = 0; i < dash.widgetCount; i++) {
        yield();
        const Widget &g = dash.widgets[i];

        // กราฟทุกชนิดต้องมีจุดอย่างน้อยหนึ่งจุด ยกเว้น text/rect/hline ที่ไม่ใช้ pool
        switch (g.type) {
            case W_TEXT:
                if (g.text[0]) drawThaiStringScaled(g.x, g.y, g.text, g.color, g.scale);
                break;
            case W_CANDLES: if (g.count) drawCandles(g);    break;
            case W_COLUMN:  if (g.count) drawColumns(g);    break;
            case W_HBAR:    if (g.count) drawHBars(g);      break;
            case W_LINE:    if (g.count) drawLineChart(g);  break;
            case W_DONUT:   if (g.count) drawDonut(g);      break;
            case W_KPI:     drawKpi(g);                     break;
            case W_GAUGE:   drawGauge(g);                   break;
            case W_QR:      if (g.qrText[0]) drawQr(g);      break;
            case W_RECT:
                if (g.filled) tft.fillRect(g.x, g.y, g.w, g.h, g.color);
                else          tft.drawRect(g.x, g.y, g.w, g.h, g.color);
                break;
            case W_HLINE:
                tft.drawFastHLine(g.x, g.y, g.w, g.color);
                break;
            default: break;
        }
    }

    dash.dirty = false;
}

// สลับโหมด — ล้างจอเฉพาะจังหวะเปลี่ยนโหมด กัน flicker ตอน refresh ปกติ
void switchToDashboard() {
    if (!dash.valid) return;
    displayMode = MODE_DASHBOARD;
    renderDashboard();
}

void switchToClock() {
    displayMode = MODE_CLOCK;
    // กลับหน้านาฬิกาแล้วต้องคืนความสว่างเดิมด้วย เผื่อออกมาจากหน้า QR ที่หรี่ไว้
    setBacklightPct(sysConfig.brightness);
    initStaticLCDScreen(); // วาดใหม่ทั้งใบ เพราะ dashboard ทับพื้นที่เดิมไปหมด
    updateTimeOnly();
}

// ---------- ชั้นดึงข้อมูลจริงจาก API ----------

// encode ค่าที่จะใส่ใน query string เพราะชื่อเมืองอาจเป็นภาษาไทยหรือมีช่องว่าง
String urlEncode(const char* src) {
    String out;
    for (const char* p = src; *p; p++) {
        char c = *p;
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            out += buf;
        }
    }
    return out;
}

// แปลง WMO weather code ของ open-meteo เป็นคำไทย
// ตารางเต็มมี ~28 ค่า ย่อเป็นกลุ่มที่สื่อความหมายพอสำหรับจอ 240px
const char* weatherCodeToThai(int code) {
    if (code == 0) return "แจ่มใส";
    if (code == 1) return "แดดจัด";
    if (code == 2) return "มีเมฆบาง";
    if (code == 3) return "เมฆมาก";
    if (code == 45 || code == 48) return "หมอก";
    if (code >= 51 && code <= 57) return "ฝนละออง";
    if (code >= 61 && code <= 67) return "ฝนตก";
    if (code >= 71 && code <= 77) return "หิมะ";
    if (code >= 80 && code <= 82) return "ฝนซู่";
    if (code == 85 || code == 86) return "หิมะซู่";
    if (code == 95) return "ฝนฟ้าคะนอง";
    if (code == 96 || code == 99) return "พายุลูกเห็บ";
    return "ไม่ทราบ";
}

// แปลงชื่อเมืองเป็นพิกัด ทำครั้งเดียวแล้ว cache ลง EEPROM
bool geocodeCity() {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    // ขอ HTTP/1.0 บังคับให้ server ตอบแบบไม่ chunked เพราะ getStream() อ่านสตรีมดิบ
    // ตรงเข้า deserializeJson ไม่มีการ dechunk ให้ (dechunk มีแต่ใน getString()/writeToStream())
    // ถ้า server ตอบ Transfer-Encoding: chunked จะเจอ hex chunk-size ปนมาในสตรีม parse fail ตลอด
    http.useHTTP10(true);

    String url = String(GEOCODE_HOST) + "/v1/search?count=1&language=th&format=json&name=" + urlEncode(sysConfig.city);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("Geocode failed: HTTP %d\n", code);
        http.end();
        return false;
    }

    // อ่านแค่ field ที่ใช้ ลด RAM ที่ ArduinoJson ต้องจอง
    StaticJsonDocument<80> filter;
    filter["results"][0]["latitude"] = true;
    filter["results"][0]["longitude"] = true;

    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("Geocode JSON error: %s\n", err.c_str());
        return false;
    }

    JsonArray results = doc["results"].as<JsonArray>();
    if (results.isNull() || results.size() == 0) {
        Serial.println("Geocode: city not found");
        return false;
    }

    sysConfig.lat = results[0]["latitude"] | 0.0f;
    sysConfig.lon = results[0]["longitude"] | 0.0f;
    Serial.printf("Geocoded %s -> %.4f, %.4f\n", sysConfig.city, sysConfig.lat, sysConfig.lon);
    saveConfigEEPROM();
    return true;
}

bool fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return false;

    // ยังไม่มีพิกัด (เครื่องใหม่ หรือผู้ใช้เพิ่งเปลี่ยนเมือง) ต้อง geocode ก่อน
    if (sysConfig.lat == 0.0f && sysConfig.lon == 0.0f) {
        if (!geocodeCity()) return false;
    }

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    // เหตุผลเดียวกับ geocodeCity() — กัน chunked transfer-encoding ทำ JSON parse fail
    http.useHTTP10(true);

    char url[160];
    snprintf(url, sizeof(url),
             "%s/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
             WEATHER_HOST, sysConfig.lat, sysConfig.lon);

    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("Weather failed: HTTP %d\n", code);
        http.end();
        return false;
    }

    StaticJsonDocument<80> filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"] = true;

    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("Weather JSON error: %s\n", err.c_str());
        return false;
    }

    JsonObject current = doc["current"];
    if (current.isNull() || !current.containsKey("temperature_2m")) {
        Serial.println("Weather: unexpected payload");
        return false;
    }

    weather.tempC = current["temperature_2m"] | 0.0f;
    weather.code = current["weather_code"] | -1;
    weather.valid = true;
    weather.stale = false;
    weather.lastOk = millis();
    Serial.printf("Weather: %.1fC code=%d (%s)\n", weather.tempC, weather.code, weatherCodeToThai(weather.code));
    return true;
}

bool fetchGold() {
    if (WiFi.status() != WL_CONNECTED) return false;

    // BearSSL กิน RAM มาก เช็คก่อนว่าเหลือพอ ไม่งั้นข้ามรอบนี้ไปดีกว่าค้าง
    if (ESP.getFreeHeap() < 24000) {
        Serial.printf("Gold: skipped, heap too low (%u)\n", ESP.getFreeHeap());
        return false;
    }

    WiFiClientSecure client;
    // ไม่ตรวจใบรับรอง เพราะ ESP8266 ไม่มี RAM พอเก็บ CA store
    // ข้อมูลนี้เป็นราคาสาธารณะอ่านอย่างเดียว ความเสี่ยงจึงต่ำ
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    // เหตุผลเดียวกับ geocodeCity() — กัน chunked transfer-encoding ทำ JSON parse fail
    http.useHTTP10(true);
    if (!http.begin(client, GOLD_URL)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("Gold failed: HTTP %d\n", code);
        http.end();
        return false;
    }

    StaticJsonDocument<48> filter;
    filter["price"] = true;

    StaticJsonDocument<96> doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("Gold JSON error: %s\n", err.c_str());
        return false;
    }

    if (!doc.containsKey("price")) {
        Serial.println("Gold: unexpected payload");
        return false;
    }

    float newPrice = doc["price"] | 0.0f;
    if (newPrice <= 0.0f) {
        Serial.println("Gold: price out of range");
        return false;
    }

    // เก็บราคาเดิมไว้ตัดสินสี ครั้งแรกให้ prev เท่ากับราคาใหม่ สีจะเป็นขาว
    gold.prevPrice = gold.valid ? gold.price : newPrice;
    gold.price = newPrice;
    gold.valid = true;
    gold.stale = false;
    gold.lastOk = millis();
    Serial.printf("Gold: $%.2f (prev $%.2f)\n", gold.price, gold.prevPrice);
    return true;
}

// ตรวจ HTTP Basic Auth ทุกครั้ง คืน false เมื่อยังไม่ผ่าน (และตอบ 401 ให้แล้ว)
bool requireAuth() {
    if (server.authenticate(sysConfig.webUser, sysConfig.webPass)) return true;
    server.requestAuthentication(BASIC_AUTH, "Smart Clock", "ต้องเข้าสู่ระบบก่อนใช้งาน\n");
    return false;
}

void handleScanWifi() {
    int n = WiFi.scanNetworks();
    StaticJsonDocument<2048> doc;
    JsonArray array = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        JsonObject obj = array.createNestedObject();
        obj["ssid"] = WiFi.SSID(i);
        obj["rssi"] = WiFi.RSSI(i);
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleConfig() {
    // 768 เพราะเพิ่ม nested object weather/gold เข้ามา 512 เดิมจะไม่พอ
    StaticJsonDocument<768> doc;
    doc["ssid"] = sysConfig.ssid;
    doc["city"] = sysConfig.city;
    doc["brightness"] = sysConfig.brightness;
    doc["celsius"] = sysConfig.celsius;
    doc["hour12"] = sysConfig.hour12;
    doc["version"] = FW_VERSION;
    doc["web_user"] = sysConfig.webUser;
    // ส่งแค่สถานะว่ายังเป็นรหัสเริ่มต้นอยู่ไหม ไม่ส่งตัวรหัสผ่านออกไป
    doc["default_pass"] = usingDefaultWebPass();

    // ข้อมูลสดที่ดึงมาได้ ให้หน้าเว็บเอาไปแสดงได้เลยไม่ต้องยิง API เอง
    JsonObject w = doc.createNestedObject("weather");
    w["valid"] = weather.valid;
    w["stale"] = weather.stale;
    if (weather.valid) {
        w["temp_c"] = serialized(String(weather.tempC, 1));
        w["code"] = weather.code;
        w["desc"] = weatherCodeToThai(weather.code);
    }

    JsonObject g = doc.createNestedObject("gold");
    g["valid"] = gold.valid;
    g["stale"] = gold.stale;
    if (gold.valid) {
        g["price"] = serialized(String(gold.price, 2));
        g["prev"] = serialized(String(gold.prevPrice, 2));
    }

    doc["lat"] = serialized(String(sysConfig.lat, 4));
    doc["lon"] = serialized(String(sysConfig.lon, 4));
    doc["heap"] = ESP.getFreeHeap();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiSet() {
    if (server.hasArg("key") && server.hasArg("value")) {
        String key = server.arg("key");
        String value = server.arg("value");
        
        if (key == "city") {
            strlcpy(sysConfig.city, value.c_str(), sizeof(sysConfig.city));
            // เปลี่ยนเมืองแล้วพิกัดเดิมใช้ไม่ได้ ล้างทิ้งให้ geocode ใหม่
            sysConfig.lat = 0.0f;
            sysConfig.lon = 0.0f;
            weather.valid = false;
            weather.stale = false;
            saveConfigEEPROM();
            // ดึงอากาศของเมืองใหม่ทันที ผู้ใช้จะเห็นผลบนจอเลยไม่ต้องรอ 10 นาที
            if (WiFi.status() == WL_CONNECTED) {
                fetchWeather();
                drawWeatherArea();
            }
            server.send(200, "text/plain", weather.valid ? "OK" : "saved, but weather fetch failed");
            return;
        }
        else if (key == "wifi_ssid") strlcpy(sysConfig.ssid, value.c_str(), sizeof(sysConfig.ssid));
        else if (key == "wifi_pass") strlcpy(sysConfig.password, value.c_str(), sizeof(sysConfig.password));
        else if (key == "lcd_brightness") {
            sysConfig.brightness = value.toInt();
            // ใช้ helper กลางตัวเดียวกับที่ renderDashboard()/switchToClock() เรียก
            // กันไม่ให้ map(pct,0,100,1023,0) ไปกระจายอยู่หลายที่จนแก้ไม่ครบ (เช่นตอนแก้บัค analogWriteRange)
            setBacklightPct(sysConfig.brightness);
        }
        else if (key == "web_user") {
            if (value.length() < 1) {
                server.send(400, "text/plain", "username must not be empty");
                return;
            }
            strlcpy(sysConfig.webUser, value.c_str(), sizeof(sysConfig.webUser));
        }
        else if (key == "web_pass") {
            // กันตั้งรหัสว่างหรือสั้นเกินไป ไม่งั้น auth จะไร้ความหมาย
            if (value.length() < 8) {
                server.send(400, "text/plain", "password must be at least 8 characters");
                return;
            }
            strlcpy(sysConfig.webPass, value.c_str(), sizeof(sysConfig.webPass));
        }

        saveConfigEEPROM(); // Persist to EEPROM
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

// ---------------------------------------------------------------------------
// POST /api/draw — รับ JSON template แล้วแปลงเป็น display list ใน RAM
// ไม่วาดในนี้ แค่ตั้งธง dirty ให้ loop() วาด เพื่อตอบ HTTP กลับให้เร็ว
// ---------------------------------------------------------------------------
void handleApiDraw() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "expected JSON body");
        return;
    }

    const String &bodyRef = server.arg("plain");
    if (bodyRef.length() > DASH_MAX_BODY) {
        server.send(413, "text/plain", "body too large");
        return;
    }

    // กันไว้แบบเดียวกับตอนดึงทอง (heap < 24000 ข้ามรอบ) — ถ้า heap ไม่พอจอง
    // document ให้ตอบ 503 ตรงๆ ดีกว่าปล่อยให้ deserialize พังแล้วตอบ 400 ซึ่งชี้ผิดที่
    if (ESP.getFreeHeap() < DASH_DOC_SIZE + 8000) {
        Serial.printf("Draw: rejected, heap too low (%u)\n", ESP.getFreeHeap());
        server.send(503, "text/plain", "heap too low, try again");
        return;
    }

    // DynamicJsonDocument เพราะก้อนเข้ามาไม่คงที่ ปล่อยคืน heap ทันทีเมื่อออกจาก scope
    DynamicJsonDocument doc(DASH_DOC_SIZE);
    DeserializationError err = deserializeJson(doc, bodyRef);
    if (err) {
        server.send(400, "text/plain", String("JSON error: ") + err.c_str());
        return;
    }

    JsonArray widgets = doc["widgets"].as<JsonArray>();
    if (widgets.isNull()) {
        server.send(400, "text/plain", "missing widgets array");
        return;
    }

    // ล้าง frame เดิมทั้งหมดก่อนรับของใหม่ — frame ต้องเป็นภาพสมบูรณ์ในตัวเอง
    dash.widgetCount = 0;
    dash.poolUsed = 0;

    // ตัดตัวเลขจากคลังกลาง คืนจำนวนจุดที่รับได้จริง (อาจน้อยกว่าที่ส่งมาถ้าคลังเต็ม)
    // valsPerPoint>1 สำหรับ candles ที่หนึ่งจุดกิน 4 ช่อง — ต้องลงครบชุดหรือไม่ลงเลย
    auto takeSeries = [&](JsonArray data, Widget &g, uint8_t valsPerPoint) -> uint8_t {
        uint16_t total = data.size();
        uint16_t cap = (DASH_POOL_SIZE - dash.poolUsed) / valsPerPoint;
        if (cap > DASH_MAX_POINTS) cap = DASH_MAX_POINTS;

        // ข้อมูลเกินที่รับได้ ให้เก็บจุดท้ายสุดเพราะเป็นของใหม่กว่า
        uint16_t skip = (total > cap) ? (total - cap) : 0;

        g.start = dash.poolUsed;
        uint8_t n = 0;
        uint16_t idx = 0;

        for (JsonVariant item : data) {
            if (idx++ < skip) continue;
            if (n >= cap) break;

            if (valsPerPoint == 1) {
                dash.pool[dash.poolUsed++] = item.as<float>();
            } else {
                JsonArray tuple = item.as<JsonArray>();
                if (tuple.isNull() || tuple.size() < valsPerPoint) continue;
                for (uint8_t k = 0; k < valsPerPoint; k++) {
                    dash.pool[dash.poolUsed++] = tuple[k] | 0.0f;
                }
            }
            n++;
        }
        g.count = n;
        return n;
    };

    // อ่านคุณสมบัติที่กราฟทุกชนิดใช้ร่วมกัน ค่าปริยายคือกรอบกลางจอใต้หัวเรื่อง
    auto readCommon = [&](JsonObject wgt, Widget &g) {
        g.x = wgt["x"] | 5;
        g.y = wgt["y"] | 40;
        g.w = wgt["w"] | 230;
        g.h = wgt["h"] | 140;
        g.color  = parseColor(wgt["color"]  | "", ST77XX_GREEN);
        g.color2 = parseColor(wgt["color2"] | "", ST77XX_RED);
        g.axis    = wgt["axis"]   | true;
        g.filled  = wgt["fill"]   | false;
        g.values  = wgt["values"] | false;
        g.decimals = wgt["decimals"] | 0;
        if (g.decimals > 4) g.decimals = 4;

        // ส่ง min/max มาคู่กัน = ตรึงสเกล ไม่ส่ง = autoscale จากข้อมูล
        bool hasMin = wgt.containsKey("min"), hasMax = wgt.containsKey("max");
        g.autoscale = !(hasMin && hasMax);
        g.vmin = wgt["min"] | 0.0f;
        g.vmax = wgt["max"] | 0.0f;

        g.hasThreshold = wgt.containsKey("threshold");
        g.threshold = wgt["threshold"] | 0.0f;

        const char* lab = wgt["label"] | "";
        if (*lab) strlcpy(g.label, lab, sizeof(g.label));
    };

    for (JsonObject wgt : widgets) {
        if (dash.widgetCount >= DASH_MAX_WIDGETS) break;

        const char* type = wgt["type"] | "";
        Widget &g = dash.widgets[dash.widgetCount];
        g = Widget(); // รีเซ็ตเป็นค่าปริยาย กัน field ค้างจาก frame ก่อน

        // ---- ข้อความ: text / title ----
        if (!strcmp(type, "text") || !strcmp(type, "title")) {
            const char* s = wgt["text"] | "";
            if (!*s) continue;

            g.type = W_TEXT;
            strlcpy(g.text, s, sizeof(g.text));
            // title เป็นทางสั้น: ตัวใหญ่ 2x ปักหัวจอให้เลย ไม่ต้องส่งพิกัด
            bool isTitle = !strcmp(type, "title");
            g.x = wgt["x"] | 5;
            g.y = wgt["y"] | (isTitle ? 6 : 190);
            g.scale = wgt["size"] | 2;
            if (g.scale < 1) g.scale = 1;
            if (g.scale > 3) g.scale = 3;
            g.color = parseColor(wgt["color"] | "", ST77XX_WHITE);
        }
        // ---- กราฟที่กินชุดตัวเลข ----
        else if (!strcmp(type, "candles") || !strcmp(type, "column") ||
                 !strcmp(type, "bar")     || !strcmp(type, "line")   ||
                 !strcmp(type, "sparkline") || !strcmp(type, "donut") ||
                 !strcmp(type, "pie")) {
            JsonArray data = wgt["data"].as<JsonArray>();
            if (data.isNull() || data.size() == 0) continue;

            readCommon(wgt, g);

            if (!strcmp(type, "candles")) {
                g.type = W_CANDLES;
                if (takeSeries(data, g, 4) == 0) continue;
            } else if (!strcmp(type, "column")) {
                g.type = W_COLUMN;
                if (takeSeries(data, g, 1) == 0) continue;
            } else if (!strcmp(type, "bar")) {
                g.type = W_HBAR;
                if (takeSeries(data, g, 1) == 0) continue;
            } else if (!strcmp(type, "donut") || !strcmp(type, "pie")) {
                g.type = W_DONUT;
                // พายคือโดนัทที่ไม่มีรู ให้ default ต่างกันตามชื่อที่เรียกมา
                g.hole = wgt["hole"] | (!strcmp(type, "pie") ? 0 : 55);
                if (g.hole > 95) g.hole = 95;
                g.color = parseColor(wgt["color"] | "", ST77XX_WHITE); // สีข้อความกลางวง
                if (takeSeries(data, g, 1) == 0) continue;
            } else {
                g.type = W_LINE;
                // sparkline คือเส้นเปล่า ไม่มีกรอบไม่มีป้าย เว้นแต่สั่งมาเอง
                if (!strcmp(type, "sparkline")) g.axis = wgt["axis"] | false;
                g.color2 = parseColor(wgt["color2"] | "", 0x0208); // เงาใต้เส้นแบบจาง
                if (takeSeries(data, g, 1) == 0) continue;
            }
        }
        // ---- KPI card: ค่าเดียว ----
        else if (!strcmp(type, "kpi")) {
            readCommon(wgt, g);
            g.h = wgt["h"] | 60;
            g.color = parseColor(wgt["color"] | "", ST77XX_WHITE);
            g.axis  = wgt["axis"] | false; // การ์ดไม่มีกรอบเป็นค่าปริยาย
            g.scale = wgt["size"] | 3;
            if (g.scale < 1) g.scale = 1;
            if (g.scale > 6) g.scale = 6;
            g.type = W_KPI;

            const char* s = wgt["text"] | "";
            if (*s) strlcpy(g.text, s, sizeof(g.text));

            // รับได้ทั้ง value เดี่ยวและ data array — ต้องมีอย่างใดอย่างหนึ่งกับ text
            if (wgt.containsKey("value") && dash.poolUsed < DASH_POOL_SIZE) {
                g.start = dash.poolUsed;
                dash.pool[dash.poolUsed++] = wgt["value"] | 0.0f;
                g.count = 1;
            } else {
                JsonArray data = wgt["data"].as<JsonArray>();
                if (!data.isNull() && data.size() > 0) takeSeries(data, g, 1);
            }
            if (!g.text[0] && g.count == 0) continue;
        }
        // ---- เกจแนวนอน ----
        else if (!strcmp(type, "gauge")) {
            readCommon(wgt, g);
            g.h = wgt["h"] | 14;
            g.type = W_GAUGE;
            if (!wgt.containsKey("value") || dash.poolUsed >= DASH_POOL_SIZE) continue;
            g.start = dash.poolUsed;
            dash.pool[dash.poolUsed++] = wgt["value"] | 0.0f;
            g.count = 1;
        }
        // ---- QR code: เข้ารหัสจาก text ตรึง VERSION7/ECC-L (45x45 module) ----
        // ไม่ใช้ readCommon() เพราะดีฟอลต์ color2=แดงของกราฟไม่เหมาะเป็นพื้นขาว
        // ของ QR — ตั้งดีฟอลต์ตรงนี้เอง: color=ดำ(มืด), color2=ขาว(สว่าง/quiet zone)
        else if (!strcmp(type, "qr")) {
            g.x = wgt["x"] | 5;
            g.y = wgt["y"] | 5;
            g.w = wgt["w"] | 200;
            g.h = wgt["h"] | 200;
            g.color  = parseColor(wgt["color"]  | "", ST77XX_BLACK);
            g.color2 = parseColor(wgt["color2"] | "", ST77XX_WHITE);
            g.type = W_QR;

            // สองทาง: ส่ง text ตรงมาเลย (สำหรับ URL/ข้อความทั่วไป) หรือส่ง promptpay_id
            // (+ promptpay_amount ถ้าต้องการตรึงยอด) ให้เฟิร์มแวร์ประกอบสาย EMV เอง
            // กันผู้ใช้ต้องคำนวณ CRC/tag-length-value เองฝั่ง caller ซึ่งพลาดง่ายมาก
            const char* ppId = wgt["promptpay_id"] | "";
            if (*ppId) {
                float amt = wgt["promptpay_amount"] | 0.0f;
                String payload = buildPromptPayPayload(ppId, amt);
                if (payload.length() == 0 || payload.length() >= sizeof(g.qrText)) {
                    continue; // id ไม่ใช่รูปแบบที่รองรับ (ไม่ใช่เบอร์โทร 10 หลัก/บัตร 13 หลัก) หรือยาวเกินบัฟเฟอร์
                }
                strlcpy(g.qrText, payload.c_str(), sizeof(g.qrText));
            } else {
                const char* s = wgt["text"] | "";
                if (!*s) continue; // ไม่มีข้อความให้เข้ารหัส ข้าม widget นี้
                // ใช้ qrText[160] ไม่ใช่ text[48] เพราะ payload PromptPay จริงยาว 80-90+ ตัวอักษร
                strlcpy(g.qrText, s, sizeof(g.qrText));
            }
        }
        // ---- primitive วางเลย์เอาต์ ----
        else if (!strcmp(type, "rect") || !strcmp(type, "hline")) {
            g.x = wgt["x"] | 0;
            g.y = wgt["y"] | 0;
            g.w = wgt["w"] | 240;
            g.h = wgt["h"] | 1;
            g.color = parseColor(wgt["color"] | "", DASH_GRID_COLOR);
            g.filled = wgt["fill"] | true;
            g.type = !strcmp(type, "rect") ? W_RECT : W_HLINE;
        }
        else {
            continue; // ชนิดที่ไม่รู้จัก ข้ามไปเงียบๆ ให้ widget อื่นใน frame ยังวาดได้
        }

        dash.widgetCount++;
    }

    if (dash.widgetCount == 0) {
        server.send(400, "text/plain", "no drawable widget");
        return;
    }

    dash.valid = true;
    dash.dirty = true;
    dash.lastPush = millis();

    // เข้าโหมด dashboard ให้เลย เว้นแต่สั่ง mode=clock มาพร้อมกัน
    const char* mode = doc["mode"] | "dashboard";
    if (!strcmp(mode, "dashboard")) displayMode = MODE_DASHBOARD;

    server.send(200, "text/plain", "OK");
}

void handleOTAUpdate() {
    HTTPUpload& upload = server.upload();

    // สำคัญ: ต้องกันตั้งแต่ไบต์แรก ไม่ใช่รอไปเช็คใน handler หลัก
    // เพราะ upload handler ทำงานก่อน ถ้าไม่กันที่นี่ไฟล์จะถูกเขียนลงแฟลชไปแล้ว
    if (!server.authenticate(sysConfig.webUser, sysConfig.webPass)) {
        if (upload.status == UPLOAD_FILE_START) {
            Serial.println("OTA rejected: unauthorized");
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update Start: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & ~0xFFF;
        if (!Update.begin(maxSketchSpace)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("Update Success: %u bytes\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}

void setupWebServer() {
    server.on("/", HTTP_GET, [](){
        if (!requireAuth()) return;
        server.send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/scanwifi", HTTP_GET, [](){
        if (!requireAuth()) return;
        handleScanWifi();
    });

    server.on("/config", HTTP_GET, [](){
        if (!requireAuth()) return;
        handleConfig();
    });

    // สั่งดึงข้อมูลใหม่ทันที ไม่ต้องรอรอบถัดไป
    server.on("/refresh", HTTP_GET, [](){
        if (!requireAuth()) return;
        if (WiFi.status() != WL_CONNECTED) {
            server.send(503, "text/plain", "Wi-Fi not connected");
            return;
        }
        // ใช้กติกาเดียวกับ scheduler: ดึงไม่สำเร็จแต่เคยมีข้อมูล = ของเก่า ต้องขึ้นจุดเหลือง
        if (!fetchWeather() && weather.valid) weather.stale = true;
        drawWeatherArea();
        if (!fetchGold() && gold.valid) gold.stale = true;
        drawGoldArea();
        handleConfig(); // ตอบค่าล่าสุดกลับไปเลย หน้าเว็บไม่ต้องยิง /config ตาม
    });

    server.on("/api/set", HTTP_GET, [](){
        if (!requireAuth()) return;
        handleApiSet();
    });

    // รับ dashboard template — POST เท่านั้น เพราะเป็นคำสั่งเปลี่ยนสภาพจอ
    server.on("/api/draw", HTTP_POST, [](){
        if (!requireAuth()) return;
        handleApiDraw();
    });

    // สลับโหมดด้วยมือ /api/mode?to=clock|dashboard|toggle
    server.on("/api/mode", HTTP_GET, [](){
        if (!requireAuth()) return;
        String to = server.hasArg("to") ? server.arg("to") : "toggle";

        if (to == "clock") switchToClock();
        else if (to == "dashboard" || to == "toggle") {
            bool wantDash = (to == "dashboard") || (displayMode == MODE_CLOCK);
            if (wantDash) {
                if (!dash.valid) {
                    server.send(409, "text/plain", "no dashboard frame pushed yet");
                    return;
                }
                switchToDashboard();
            } else {
                switchToClock();
            }
        } else {
            server.send(400, "text/plain", "to must be clock|dashboard|toggle");
            return;
        }

        server.send(200, "text/plain", displayMode == MODE_DASHBOARD ? "dashboard" : "clock");
    });

    // OTA: ต้องเช็ค auth ทั้งใน upload handler และตอนตอบกลับ
    // เพราะ ESP8266WebServer เรียก upload handler ก่อนถึง handler หลัก
    server.on("/update_ota", HTTP_POST, [](){
        if (!requireAuth()) return;
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        delay(500);
        ESP.restart();
    }, handleOTAUpdate);

    server.on("/restart", HTTP_GET, [](){
        if (!requireAuth()) return;
        server.send(200, "text/plain", "Restarting...");
        delay(500);
        ESP.restart();
    });

    server.onNotFound([](){
        server.send(404, "text/plain", "Not Found");
    });
}

// ---------------------------------------------------------------------------
// Wi-Fi upkeep — ไม่บล็อก loop() เลย ใช้ millis() คุมจังหวะทั้งหมด
// ---------------------------------------------------------------------------

static unsigned long wifiLostSince = 0;   // 0 = ยังไม่หลุด
static unsigned long lastReconnectTry = 0;

void maintainWifi() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < WIFI_CHECK_MS) return;
    lastCheck = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (wifiLostSince != 0) {
            Serial.print("Wi-Fi reconnected. IP: ");
            Serial.println(WiFi.localIP());
            wifiLostSince = 0;
            // กลับมาต่อได้แล้ว ปิด AP ที่เปิดไว้ตอนหลุด
            if (isAPModeActive) {
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                isAPModeActive = false;
            }
            updateWifiStatusLCD();
        }
        return;
    }

    // เพิ่งหลุด — จำเวลาไว้เพื่อนับว่าหลุดมานานแค่ไหน
    if (wifiLostSince == 0) {
        wifiLostSince = millis();
        Serial.println("Wi-Fi lost. Will retry.");
        updateWifiStatusLCD();
    }

    // ยิงต่อใหม่เป็นจังหวะ ไม่ใช่รัวทุกรอบ loop
    if (millis() - lastReconnectTry >= WIFI_RETRY_MS) {
        lastReconnectTry = millis();
        Serial.println("Reconnecting to Wi-Fi...");
        WiFi.disconnect();
        WiFi.begin(sysConfig.ssid, sysConfig.password);
    }

    // หลุดนานเกินกำหนดและยังไม่ได้เปิด AP — เปิดให้ผู้ใช้เข้ามาแก้ค่าได้
    if (!isAPModeActive && millis() - wifiLostSince >= WIFI_AP_FALLBACK_MS) {
        Serial.println("Wi-Fi down too long. Enabling AP mode for recovery.");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("SmartClock-AP", "12345678");
        isAPModeActive = true;
        updateWifiStatusLCD();
    }
}

// ---------------------------------------------------------------------------
// ตารางการดึงข้อมูล — ดึงทีละอย่าง ไม่ดึงพร้อมกัน เพื่อไม่ให้ RAM พีคซ้อนกัน
// ---------------------------------------------------------------------------

void updateDataIfDue() {
    if (WiFi.status() != WL_CONNECTED) return;

    static unsigned long lastWeatherTry = 0;
    static unsigned long lastGoldTry = 0;

    // ดึงไม่สำเร็จให้ลองใหม่เร็วขึ้น สำเร็จแล้วค่อยรอรอบยาว
    unsigned long weatherGap = weather.valid && !weather.stale
                                 ? WEATHER_INTERVAL_MS : RETRY_INTERVAL_MS;
    unsigned long goldGap = gold.valid && !gold.stale
                                 ? GOLD_INTERVAL_MS : RETRY_INTERVAL_MS;

    if (lastWeatherTry == 0 || millis() - lastWeatherTry >= weatherGap) {
        lastWeatherTry = millis();
        // ดึงไม่สำเร็จแต่เคยมีข้อมูล = ค่าบนจอเป็นของเก่า ต้องขึ้นจุดเหลืองเตือน
        if (!fetchWeather() && weather.valid) weather.stale = true;
        drawWeatherArea();
        return; // คืน CPU ให้ loop ก่อน ไม่ดึงสองอย่างติดกันในรอบเดียว
    }

    if (lastGoldTry == 0 || millis() - lastGoldTry >= goldGap) {
        lastGoldTry = millis();
        if (!fetchGold() && gold.valid) gold.stale = true;
        drawGoldArea();
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== GeekMagic SmallTV Firmware v" FW_VERSION " ===");

    // Active Low Backlight Fix
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);

    // core ESP8266 Arduino >= 3.0.0 เปลี่ยน analogWrite() default range จาก 1023 เป็น 255
    // (breaking change) แต่โค้ดทั้งไฟล์ map(pct, 0, 100, 1023, 0) ผูกกับ 1023 มาตั้งแต่ต้น
    // ถ้าไม่ตั้งกลับ ค่า pct > ~76% จะ map ได้ตัวเลข > 255 แล้วโดน core clamp เป็น 255 (max)
    // ซึ่ง backlight เป็น active-low ค่า max = ปิดจอสนิท ผู้ใช้แจ้งว่าความสว่าง <76% จอมืดสนิท
    analogWriteRange(1023);

    // Initialize ST7789 Display
    tft.init(240, 240, SPI_MODE3);
    tft.setRotation(2);

    // Load EEPROM Persistent Config
    loadConfigEEPROM();

    // หน้า loading — โชว์ทันทีหลัง EEPROM แทนจอมืด/ค้างระหว่างต่อ Wi-Fi
    drawBootScreen();
    drawBootStatus("กำลังต่อ Wi-Fi", 0);

    // WiFi STA Connection Attempt
    WiFi.mode(WIFI_STA);
    // ให้ SDK ช่วยต่อใหม่เองด้วย ส่วน maintainWifi() คุมกรณีที่ SDK เอาไม่อยู่
    WiFi.setAutoReconnect(true);
    // ไม่เขียนค่า Wi-Fi ลงแฟลชทุกครั้งที่ต่อ เพราะเราเก็บใน EEPROM เองแล้ว
    WiFi.persistent(false);
    WiFi.begin(sysConfig.ssid, sysConfig.password);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        drawBootStatus("กำลังต่อ Wi-Fi", retries % 4); // จุดวิ่ง กันดูเหมือนจอค้าง
        retries++;
        yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi Connected! IP: ");
        Serial.println(WiFi.localIP());
        isAPModeActive = false;
        WiFi.softAPdisconnect(true);
        drawBootStatus("ต่อสำเร็จ", 0);
    } else {
        Serial.println("WiFi STA Failed. Enabling Smart AP Mode...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("SmartClock-AP", "12345678");
        isAPModeActive = true;
        // นับเวลาหลุดตั้งแต่บูต เพื่อให้ maintainWifi() ทำงานต่อได้ถูกต้อง
        wifiLostSince = millis();
        drawBootStatus("เปิดโหมด AP", 0);
    }

    // NTP sync สำหรับ ESP8266 - ใช้ TZ string ของไทย UTC+7
    configTime("ICT-7", "pool.ntp.org", "time.cloudflare.com");
    setupWebServer();
    server.begin();

    Serial.printf("Web UI protected. user=%s\n", sysConfig.webUser);
    if (usingDefaultWebPass()) {
        Serial.println("WARNING: still using the default web password. Change it at the web UI.");
    }

    // ดึงข้อมูลจริงรอบแรกทันทีที่ต่อเน็ตได้ ไม่ต้องรอครบรอบ interval
    if (WiFi.status() == WL_CONNECTED) {
        drawBootStatus("กำลังดึงข้อมูล", 0);
        fetchWeather();
        fetchGold();
    }

    // สลับจากหน้า loading ไปหน้านาฬิกาจริงทีเดียว ตอนข้อมูลรอบแรกพร้อมแล้ว
    initStaticLCDScreen();
    drawWeatherArea();
    drawGoldArea();
}

void loop() {
    server.handleClient();
    yield();

    maintainWifi();
    updateDataIfDue();

    // มี frame ใหม่ที่ push เข้ามา — วาดที่นี่ ไม่วาดใน HTTP handler
    // เพื่อให้ตอบ 200 กลับไปก่อน ฝั่ง HA ไม่ต้องรอจนวาดเสร็จ
    if (displayMode == MODE_DASHBOARD && dash.dirty) {
        renderDashboard();
    }

    // HA เงียบไปนานเกิน TTL — กลับหน้านาฬิกาเอง ไม่ค้างข้อมูลเก่าไว้บนจอ
    if (displayMode == MODE_DASHBOARD && millis() - dash.lastPush >= DASH_TTL_MS) {
        Serial.println("Dashboard TTL expired. Back to clock.");
        dash.valid = false;
        switchToClock();
    }

    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 1000) {
        lastDisplayUpdate = millis();
        // ทั้งสองตัวมี guard เช็คโหมดในตัวแล้ว เรียกได้ตรงๆ
        updateTimeOnly();
        updateWifiStatusLCD();
    }
}
