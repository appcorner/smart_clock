#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
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

#define FW_VERSION "3.6.0"

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
#define DASH_TEXT_LEN     96  // ป้ายกำกับ KPI บน / รองรับข้อความยาว
#define DASH_LABEL_LEN    48  // ค่าหลัก KPI / ข้อความกลาง Donut
// คลังตัวเลขที่ทุก widget แชร์กัน — candles กินจุดละ 4 ค่า ที่เหลือกินจุดละ 1 ค่า
// 200 ช่อง = candles เต็ม 40 แท่ง (160) แล้วยังเหลือให้กราฟเส้นอีก 40 จุด
#define DASH_POOL_SIZE    200
// ก้อน JSON ที่รับได้ — 10 widget ที่มีกราฟหลายตัวราว 2.5KB จึงเผื่อไว้เท่าตัว
#define DASH_MAX_BODY     6144
// ArduinoJson ต้องการที่มากกว่าตัวข้อความ เพราะต้องกาง object tree ขึ้น RAM
#define DASH_DOC_SIZE     8192
// ไม่มีใคร push ต่อภายในเวลานี้ ให้กลับไปหน้านาฬิกาเอง กัน HA ล่มแล้วจอค้างข้อมูลเก่า
#define DASH_TTL_MS       600000UL  // 10 นาที
#define IMAGE_TTL_MS      600000UL  // 10 นาที
#define LIVE_TTL_MS       30000UL   // 30 วินาที

// ---------------------------------------------------------------------------
// Dashboard model — เก็บ display list ที่ parse แล้วไว้ใน RAM
// ---------------------------------------------------------------------------

enum DisplayMode : uint8_t { 
    MODE_CLOCK = 0, 
    MODE_DASHBOARD = 1,
    MODE_IMAGE = 2,
    MODE_LIVE = 3
};
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
    W_VLINE      // เส้นแนวตั้ง ใช้เป็นเส้นอ้างอิง/เส้นคั่น
};

// widget เดียวครอบทุกชนิด
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
    char text[DASH_TEXT_LEN] = "";   // ป้ายกำกับบน KPI / ข้อความ W_TEXT
    char label[DASH_LABEL_LEN] = ""; // ค่าหลัก KPI / ข้อความกลางโดนัท
    // Scale reference: อ้างอิงสเกลจาก widget อื่น แทนใช้พิกัดแบบ pixel ตายตัว
    bool hasRef = false;        // true = ใช้ value + refType แทนพิกัด x/y ตายตัว
    float refValue = 0.0f;      // ค่าข้อมูล (เช่น ราคา, index) ที่จะ map ผ่านสเกล
    char refType[12] = "";      // ชนิด widget ที่จะอ้างอิง ("candles", "column", "line", ...)
};

struct Dashboard {
    char bgImg[64] = "";
    Widget widgets[DASH_MAX_WIDGETS];
    uint8_t widgetCount = 0;
    float pool[DASH_POOL_SIZE];
    uint16_t poolUsed = 0;
    bool valid = false;           // มี frame ที่วาดได้อยู่
    bool dirty = false;           // มี frame ใหม่ที่ยังไม่ได้วาด
    unsigned long lastPush = 0;   // ใช้นับ TTL
    char qrText[160] = "";
    int16_t qrX = 5, qrY = 5;
    int16_t qrW = 200, qrH = 200;
    uint16_t qrBg = ST77XX_BLACK;
    uint16_t qrFg = ST77XX_WHITE;
};

Dashboard dash;

// Wi-Fi upkeep state
unsigned long wifiLostSince = 0;   // 0 = ยังไม่หลุด
unsigned long lastReconnectTry = 0;

// In-Memory Ring Buffer Logger สำหรับ /log
#define LOG_BUFFER_SIZE 2048
char logBuffer[LOG_BUFFER_SIZE];
size_t logBufferIndex = 0;
bool logBufferFull = false;

void appLog(const String &msg) {
    Serial.println(msg);
    size_t len = msg.length();
    for (size_t i = 0; i < len; i++) {
        logBuffer[logBufferIndex] = msg[i];
        logBufferIndex = (logBufferIndex + 1) % LOG_BUFFER_SIZE;
        if (logBufferIndex == 0) logBufferFull = true;
    }
    logBuffer[logBufferIndex] = '\n';
    logBufferIndex = (logBufferIndex + 1) % LOG_BUFFER_SIZE;
    if (logBufferIndex == 0) logBufferFull = true;
}

String getLogs() {
    String out = "";
    out.reserve(LOG_BUFFER_SIZE + 10);
    if (logBufferFull) {
        for (size_t i = logBufferIndex; i < LOG_BUFFER_SIZE; i++) {
            out += logBuffer[i];
        }
    }
    for (size_t i = 0; i < logBufferIndex; i++) {
        out += logBuffer[i];
    }
    return out;
}

// GeekMagic SmallTV Pinout Configuration
#define TFT_MOSI 13 // GPIO13
#define TFT_SCLK 14 // GPIO14
#define TFT_DC    0 // GPIO0
#define TFT_RST   2 // GPIO2
#define TFT_CS   -1 // Tied to GND
#define TFT_BL    5 // GPIO5 (Active Low Backlight)

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// TJpg_Decoder output callback สำหรับ Adafruit_ST7789
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return 0;
    tft.drawRGBBitmap(x, y, bitmap, w, h);
    return 1;
}

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

// EEPROM layout v3.5.4 (cache พิกัดแล้ว แต่ยังไม่มี gmtOffset)
struct ConfigV354 {
    char ssid[32];
    char password[64];
    char city[32];
    uint8_t brightness;
    bool celsius;
    bool hour12;
    char webUser[24];
    char webPass[32];
    float lat;
    float lon;
    uint8_t magic; // 0xAD
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
    float lat = 0.0f;
    float lon = 0.0f;
    int32_t gmtOffset = 25200; // GMT+7 = 25200s (Bangkok)
    uint8_t magic = 0xAE; // ขึ้นเป็น 0xAE เพราะเพิ่ม gmtOffset
};

ClockConfig sysConfig;
ESP8266WebServer server(80);
bool isAPModeActive = false;

// จัดการการอัปโหลดไฟล์รูปภาพ
#define IMAGE_DIR "/image/"
File uploadFile;
char currentImagePath[64] = "";
bool imageDirty = false;
unsigned long lastImagePush = 0;

// Live message data สำหรับ /api/update
char liveLine1[64] = "";
char liveLine2[64] = "";
bool liveDirty = false;
unsigned long lastLivePush = 0;

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

// forward declaration — ตัววาดจออยู่ก่อนชั้น network ในไฟล์ แต่ต้องเรียกตัวแปลง code นี้
const char* weatherCodeToThai(int code);

// true เมื่อยังใช้รหัสผ่านหน้าเว็บที่มาจากโรงงาน
bool usingDefaultWebPass() {
    return strcmp(sysConfig.webPass, DEFAULT_WEB_PASS) == 0;
}

void saveConfigEEPROM() {
    EEPROM.begin(512);
    sysConfig.magic = 0xAE;
    EEPROM.put(0, sysConfig);
    EEPROM.commit();
    EEPROM.end();
    Serial.println(F("Config Saved to EEPROM!"));
}

// อ่าน config จาก EEPROM พร้อม migration chain 0xAB -> 0xAC -> 0xAD -> 0xAE
// ทุก branch ต้องกัน string ที่ไม่มี null terminator ก่อนเรียก strlen
void loadConfigEEPROM() {
    EEPROM.begin(512);

    // ชั้นที่ 1: layout ปัจจุบัน (0xAE)
    ClockConfig cfgNew;
    EEPROM.get(0, cfgNew);
    if (cfgNew.magic == 0xAE) {
        cfgNew.ssid[sizeof(cfgNew.ssid) - 1] = '\0';
        if (strlen(cfgNew.ssid) > 0) {
            sysConfig = cfgNew;
            // กัน string ทุกช่องให้ปิดท้ายแน่นอน เผื่อ EEPROM เพี้ยน
            sysConfig.password[sizeof(sysConfig.password) - 1] = '\0';
            sysConfig.city[sizeof(sysConfig.city) - 1] = '\0';
            sysConfig.webUser[sizeof(sysConfig.webUser) - 1] = '\0';
            sysConfig.webPass[sizeof(sysConfig.webPass) - 1] = '\0';
            EEPROM.end();
            Serial.println(F("Loaded Config from EEPROM (0xAE)."));
            Serial.printf_P(PSTR("SSID: %s  lat=%.4f lon=%.4f gmt=%d\n"), sysConfig.ssid, sysConfig.lat, sysConfig.lon, sysConfig.gmtOffset);
            return;
        }
    }

    // ชั้นที่ 2: layout v3.5.4 (0xAD) — มี lat/lon แล้ว ขาด gmtOffset
    ConfigV354 cfg354;
    EEPROM.get(0, cfg354);
    if (cfg354.magic == 0xAD) {
        cfg354.ssid[sizeof(cfg354.ssid) - 1] = '\0';
        if (strlen(cfg354.ssid) > 0) {
            cfg354.password[sizeof(cfg354.password) - 1] = '\0';
            cfg354.city[sizeof(cfg354.city) - 1] = '\0';
            cfg354.webUser[sizeof(cfg354.webUser) - 1] = '\0';
            cfg354.webPass[sizeof(cfg354.webPass) - 1] = '\0';

            strlcpy(sysConfig.ssid, cfg354.ssid, sizeof(sysConfig.ssid));
            strlcpy(sysConfig.password, cfg354.password, sizeof(sysConfig.password));
            strlcpy(sysConfig.city, cfg354.city, sizeof(sysConfig.city));
            strlcpy(sysConfig.webUser, cfg354.webUser, sizeof(sysConfig.webUser));
            strlcpy(sysConfig.webPass, cfg354.webPass, sizeof(sysConfig.webPass));
            sysConfig.brightness = cfg354.brightness;
            sysConfig.celsius = cfg354.celsius;
            sysConfig.hour12 = cfg354.hour12;
            sysConfig.lat = cfg354.lat;
            sysConfig.lon = cfg354.lon;
            sysConfig.gmtOffset = 25200; // GMT+7 default

            EEPROM.end();
            Serial.println(F("Migrated config 0xAD -> 0xAE."));
            saveConfigEEPROM();
            return;
        }
    }

    // ชั้นที่ 3: layout v3.0.0 (0xAC) — ยกทุกอย่างมา แต่ต้อง geocode พิกัดใหม่
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
            sysConfig.gmtOffset = 25200;

            EEPROM.end();
            Serial.println(F("Migrated config 0xAC -> 0xAE (will geocode city)."));
            saveConfigEEPROM();
            return;
        }
    }

    // ชั้นที่ 4: layout v2.x (0xAB) — รหัสผ่านหน้าเว็บกลับเป็นค่าเริ่มต้น
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
            sysConfig.lat = 0.0f;
            sysConfig.lon = 0.0f;
            sysConfig.gmtOffset = 25200;
            // webUser/webPass คงค่าเริ่มต้นไว้ ผู้ใช้ต้องตั้งรหัสใหม่เอง

            EEPROM.end();
            Serial.println(F("Migrated config 0xAB -> 0xAE (web password reset to default)."));
            saveConfigEEPROM();
            return;
        }
    }

    EEPROM.end();
    Serial.println(F("No EEPROM Config found, using defaults."));
}

// Embedded Smart Web UI HTML in PROGMEM
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="th"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Smart Clock Pro</title><style>body{background:#0a0c10;color:#f3f4f6;font-family:sans-serif;padding:15px;text-align:center;margin:0}.card{background:rgba(22,27,34,0.85);border:1px solid rgba(255,255,255,0.1);border-radius:12px;padding:16px;margin:10px auto;max-width:420px}h1{color:#3b82f6;font-size:20px;margin:0 0 4px 0}h3{color:#93c5fd;margin:0 0 10px 0;font-size:15px}input,select{width:100%;padding:10px;margin:5px 0;border-radius:8px;border:1px solid #374151;background:#1f2937;color:#fff;box-sizing:border-box;font-size:14px}button{width:100%;padding:10px;margin:5px 0;border-radius:8px;border:none;font-weight:700;cursor:pointer;font-size:14px}.btn-blue{background:#3b82f6;color:#fff}.row{display:flex;justify-content:space-between;align-items:center;font-size:13px;padding:5px 2px;border-bottom:1px solid rgba(255,255,255,0.06)}.k{color:#9ca3af}.v{color:#f3f4f6;font-weight:700}.v.old{color:#f59e0b}.v.up{color:#10b981}.v.down{color:#ef4444}.btn-green{background:#10b981;color:#fff}.btn-yellow{background:#f59e0b;color:#000}.status{font-size:13px;padding:6px 10px;border-radius:6px;margin:4px 0}.ok{background:#065f46;color:#6ee7b7}.info{background:#1e3a5f;color:#93c5fd}.warn{background:#7c2d12;color:#fdba74;border:1px solid #ea580c}label{display:block;text-align:left;font-size:13px;color:#9ca3af;margin-top:8px}.btn-red{background:#dc2626;color:#fff}.hint{font-size:12px;color:#6b7280;text-align:left;margin:6px 0 0 0}.ver{font-size:11px;color:#4b5563;margin-top:2px}</style></head><body><div class="card"><h1>⏰ Smart Clock Pro</h1><p id="wifiStatus" class="status info">กำลังโหลด...</p><p id="ver" class="ver"></p></div><div id="passWarnCard" class="card" style="display:none"><p class="status warn">⚠️ ยังใช้รหัสผ่านเริ่มต้นอยู่ ใครที่อยู่ในวง Wi-Fi เดียวกันก็เข้ามาแก้ค่าหรือเขียนเฟิร์มแวร์ทับได้ กรุณาตั้งรหัสใหม่ที่การ์ดด้านล่าง</p></div><div class="card"><h3>📊 ข้อมูลสดบนจอ</h3><div class="row"><span class="k">อากาศ</span><span><span id="wxVal" class="v">-</span><span id="wxDot" class="dot none"></span></span></div><div class="row"><span class="k">ราคาทอง XAU/USD</span><span><span id="goldVal" class="v">-</span><span id="goldDot" class="dot none"></span></span></div><p id="dataMeta" class="hint"></p><button id="refreshBtn" class="btn-green" onclick="doRefresh()">🔄 ดึงข้อมูลใหม่ตอนนี้</button><p class="hint">ปกติอากาศดึงใหม่ทุก 10 นาที ทองทุก 5 นาที ถ้าดึงไม่สำเร็จจะลองใหม่ทุก 1 นาที และคงค่าเดิมไว้บนจอพร้อมจุดเหลืองเตือน</p></div><div class="card"><h3>📶 ตั้งค่า Wi-Fi</h3><button class="btn-green" onclick="scanWifi()">🔍 สแกนหา Wi-Fi</button><select id="wifi_list"><option value="">กดสแกนก่อน...</option></select><label>รหัสผ่าน Wi-Fi</label><input type="password" id="pass" placeholder="รหัสผ่าน"><button class="btn-blue" onclick="saveWifi()">💾 บันทึกและรีสตาร์ท</button></div><div class="card"><h3>🌤️ ตั้งค่าเมือง</h3><label>ชื่อเมือง (ภาษาอังกฤษ)</label><input type="text" id="city" placeholder="เช่น Bangkok"><button id="cityBtn" class="btn-blue" onclick="saveCity()">💾 บันทึกเมือง</button></div><div class="card"><h3>💡 ความสว่างหน้าจอ</h3><input type="range" id="brightness" min="5" max="100" value="80" oninput="document.getElementById('brightnessVal').innerText=this.value+'%'" onchange="saveBrightness(this.value)"><p class="hint">ระดับปัจจุบัน: <span id="brightnessVal">80%</span> — ตอนโชว์ QR เครื่องจะหรี่ลงเองชั่วคราวให้กล้องมือถือโฟกัสง่ายขึ้น แล้วคืนค่านี้ตอนกลับหน้านาฬิกา</p></div><div class="card"><h3>🔒 รหัสผ่านหน้าเว็บ</h3><label>ชื่อผู้ใช้</label><input type="text" id="web_user" placeholder="admin" autocomplete="username"><label>รหัสผ่านใหม่ (อย่างน้อย 8 ตัวอักษร)</label><input type="password" id="web_pass" placeholder="รหัสผ่านใหม่" autocomplete="new-password"><label>ยืนยันรหัสผ่านใหม่</label><input type="password" id="web_pass2" placeholder="พิมพ์ซ้ำอีกครั้ง" autocomplete="new-password"><button class="btn-red" onclick="saveAuth()">🔐 บันทึกรหัสผ่าน</button><p class="hint">หลังบันทึก เบราว์เซอร์จะถามรหัสใหม่ในการเข้าครั้งถัดไป ถ้าลืมรหัสต้องล้าง EEPROM ด้วยการแฟลชผ่านสาย</p></div><div class="card"><h3>🚀 อัปเดต Firmware (OTA)</h3><form method="POST" action="/update_ota" enctype="multipart/form-data"><input type="file" name="update" accept=".bin"><button type="submit" class="btn-yellow">⚡ อัปโหลด .bin</button></form></div><script>window.onload=function(){fetch('/config').then(r=>r.json()).then(d=>{document.getElementById('city').value=d.city||'';document.getElementById('web_user').value=d.web_user||'admin';document.getElementById('ver').innerText='Firmware v'+(d.version||'-');if(typeof d.brightness!=='undefined'){document.getElementById('brightness').value=d.brightness;document.getElementById('brightnessVal').innerText=d.brightness+'%';}let ws=document.getElementById('wifiStatus');ws.innerText='เชื่อมต่อ Wi-Fi: '+(d.ssid||'-');ws.className='status ok';if(d.default_pass){document.getElementById('passWarnCard').style.display='block';}renderData(d);}).catch(()=>{document.getElementById('wifiStatus').innerText='ไม่สามารถโหลดข้อมูลได้';});};function scanWifi(){let sel=document.getElementById('wifi_list');sel.innerHTML='<option>กำลังสแกน...</option>';fetch('/scanwifi').then(r=>r.json()).then(data=>{sel.innerHTML='';if(!data.length){sel.innerHTML='<option>ไม่พบ Wi-Fi</option>';return;}data.sort((a,b)=>b.rssi-a.rssi);data.forEach(item=>{let o=document.createElement('option');o.value=item.ssid;o.innerText=item.ssid+'  ('+item.rssi+' dBm)';sel.appendChild(o);});}).catch(()=>alert('สแกนไม่สำเร็จ'));}function saveAuth(){let u=document.getElementById('web_user').value.trim(),p=document.getElementById('web_pass').value,p2=document.getElementById('web_pass2').value;if(!u){alert('กรุณากรอกชื่อผู้ใช้');return;}if(p.length<8){alert('รหัสผ่านต้องยาวอย่างน้อย 8 ตัวอักษร');return;}if(p!==p2){alert('รหัสผ่านสองช่องไม่ตรงกัน');return;}fetch('/api/set?key=web_user&value='+encodeURIComponent(u)).then(r=>{if(!r.ok)throw new Error('ตั้งชื่อผู้ใช้ไม่สำเร็จ');return fetch('/api/set?key=web_pass&value='+encodeURIComponent(p));}).then(r=>{if(!r.ok)throw new Error('ตั้งรหัสผ่านไม่สำเร็จ');alert('บันทึกรหัสผ่านใหม่แล้ว ครั้งถัดไปที่เปิดหน้านี้เบราว์เซอร์จะถามรหัสใหม่');document.getElementById('web_pass').value='';document.getElementById('web_pass2').value='';document.getElementById('passWarnCard').style.display='none';}).catch(e=>alert(e.message));}function renderData(d){let w=d.weather||{},wEl=document.getElementById('wxVal'),wDot=document.getElementById('wxDot');if(w.valid){wEl.innerText=w.temp_c+'°C  '+(w.desc||'');wDot.className=w.stale?'dot stale':'dot fresh';wDot.title=w.stale?'ข้อมูลเก่า ดึงใหม่ไม่สำเร็จ':'ข้อมูลสด';}else{wEl.innerText='ยังไม่มีข้อมูล';wDot.className='dot none';wDot.title='ยังดึงไม่สำเร็จเลย';}let g=d.gold||{},gEl=document.getElementById('goldVal'),gDot=document.getElementById('goldDot');if(g.valid){gEl.innerText='$'+g.price;if(g.prev>0&&g.price>g.prev)gEl.style.color='#10b981';else if(g.prev>0&&g.price<g.prev)gEl.style.color='#ef4444';else gEl.style.color='#f3f4f6';gDot.className=g.stale?'dot stale':'dot fresh';gDot.title=g.stale?'ข้อมูลเก่า ดึงใหม่ไม่สำเร็จ':'ข้อมูลสด';}else{gEl.innerText='ยังไม่มีข้อมูล';gEl.style.color='#9ca3af';gDot.className='dot none';gDot.title='ยังดึงไม่สำเร็จเลย';}let meta=[];if(d.lat||d.lon)meta.push('พิกัด '+d.lat+', '+d.lon);if(d.heap)meta.push('heap '+d.heap+' bytes');document.getElementById('dataMeta').innerText=meta.join('  •  ');}function reloadData(){return fetch('/config').then(r=>r.json()).then(renderData);}function doRefresh(){let btn=document.getElementById('refreshBtn');btn.disabled=true;btn.innerText='⏳ กำลังดึง...';fetch('/refresh').then(r=>r.json()).then(()=>reloadData()).catch(()=>alert('ดึงข้อมูลไม่สำเร็จ')).finally(()=>{btn.disabled=false;btn.innerText='🔄 ดึงข้อมูลใหม่ตอนนี้';});}function saveCity(){let c=document.getElementById('city').value.trim();if(!c){alert('กรุณากรอกชื่อเมือง');return;}let btn=document.getElementById('cityBtn');btn.disabled=true;btn.innerText='⏳ กำลังค้นหาเมือง...';fetch('/api/set?key=city&value='+encodeURIComponent(c)).then(r=>r.text().then(t=>({ok:r.ok,text:t}))).then(res=>{if(!res.ok)throw new Error(res.text);if(res.text!=='OK')alert('บันทึกเมืองแล้ว แต่ยังดึงอากาศไม่ได้: '+res.text);else alert('บันทึกเมือง "'+c+'" แล้ว');return reloadData();}).catch(e=>alert('บันทึกเมืองไม่สำเร็จ: '+e.message)).finally(()=>{btn.disabled=false;btn.innerText='💾 บันทึกเมือง';});}function saveWifi(){let s=document.getElementById('wifi_list').value,p=document.getElementById('pass').value;if(!s){alert('กรุณาเลือก Wi-Fi ก่อน');return;}fetch('/api/set?key=wifi_ssid&value='+encodeURIComponent(s)).then(()=>fetch('/api/set?key=wifi_pass&value='+encodeURIComponent(p))).then(()=>{alert('บันทึกข้อมูล Wi-Fi ลง EEPROM เรียบร้อย! เครื่องกำลังรีบูตเพื่อเชื่อมต่อ...');setTimeout(()=>fetch('/restart'),500);});}function saveBrightness(v){fetch('/api/set?key=lcd_brightness&value='+encodeURIComponent(v)).catch(()=>alert('ตั้งความสว่างไม่สำเร็จ'));}</script></body></html>)rawliteral";

// Binary search for glyph offset in new font format
bool findGlyphOffset(uint32_t codepoint, uint32_t &offset) {
    if (codepoint > 0xFFFF) return false;
    uint16_t targetCp = (uint16_t)codepoint;
    int16_t lo = 0, hi = THAIFONT_GLYPH_COUNT - 1;
    while (lo <= hi) {
        int16_t mid = lo + ((hi - lo) / 2);
        uint16_t cp = pgm_read_word(&ThaiFontIndex[mid].codepoint);
        if (cp == targetCp) { offset = pgm_read_word(&ThaiFontIndex[mid].offset); return true; }
        if (cp < targetCp) lo = mid + 1; else hi = mid - 1;
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
    if (cp == 0x0E33) return true;                        // ำ
    if (cp >= 0x0E34 && cp <= 0x0E3A) return true;        // ิ ี ึ ื ุ ู
    if (cp >= 0x0E47 && cp <= 0x0E4E) return true;        // ็ ่ ้ ๊ ๋ ์
    return false;
}

// Scaled Thai renderer with Run-Length Acceleration — scale=1 คือ 8x16, scale=2 คือ 16x32, scale=3 คือ 24x48
void drawThaiStringScaled(int16_t x, int16_t y, const char* str, uint16_t color, uint8_t scale) {
    if (scale < 1) scale = 1;
    uint8_t cell[THAIFONT_HEIGHT];
    uint8_t glyph[THAIFONT_HEIGHT];
    bool haveCell = false;
    bool haveUpperVowel = false;
    int16_t curX = x;

    memset(cell, 0, THAIFONT_HEIGHT);
    const char* ptr = str;

    auto flushCellScaled = [&]() {
        if (!haveCell) return;
        // รวม pixel ที่ติดกันในแนวนอน (horizontal run-length) แล้ววาดด้วย fillRect ครั้งเดียว ลด SPI calls มหาศาล
        for (uint8_t row = 0; row < THAIFONT_HEIGHT; row++) {
            uint8_t bits = cell[row];
            if (!bits) continue;
            int16_t py = y + row * scale;
            uint8_t col = 0;
            while (col < 8) {
                if (bits & (0x80 >> col)) {
                    uint8_t startCol = col;
                    while (col < 8 && (bits & (0x80 >> col))) col++;
                    tft.fillRect(curX + startCol * scale, py, (col - startCol) * scale, scale, color);
                } else {
                    col++;
                }
            }
        }
        curX += 8 * scale;
        memset(cell, 0, THAIFONT_HEIGHT);
        haveCell = false;
        haveUpperVowel = false;
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
            // สระ ำ (U+0E33) ประกอบด้วย ํ (U+0E4D) + า (U+0E32)
            if (cp == 0x0E33) {
                if (haveCell && loadGlyph(0x0E4D, glyph)) {
                    for (uint8_t i = 0; i < THAIFONT_HEIGHT; i++) cell[i] |= glyph[i];
                }
                flushCellScaled();
                if (curX + 8 * scale > 240) { curX = x; y += THAIFONT_HEIGHT * scale; }
                if (loadGlyph(0x0E32, glyph)) {
                    memcpy(cell, glyph, THAIFONT_HEIGHT);
                    haveCell = true;
                }
                continue;
            }

            // Merge สระ/วรรณยุกต์อื่นๆ ลงบน Cell ปัจจุบัน
            if (haveCell && loadGlyph(cp, glyph)) {
                if (!haveUpperVowel && cp >= 0x0E48 && cp <= 0x0E4C) {
                    uint32_t next_cp = 0;
                    const char* next_ptr = ptr;
                    if (*next_ptr) {
                        uint8_t nb = (uint8_t)*next_ptr;
                        if (nb < 0x80) { next_cp = nb; }
                        else if ((nb & 0xE0) == 0xC0) { next_cp = ((nb & 0x1F) << 6) | ((uint8_t)next_ptr[1] & 0x3F); }
                        else if ((nb & 0xF0) == 0xE0) { next_cp = ((nb & 0x0F) << 12) | (((uint8_t)next_ptr[1] & 0x3F) << 6) | ((uint8_t)next_ptr[2] & 0x3F); }
                    }

                    if (next_cp != 0x0E33) {
                        for (int16_t i = THAIFONT_HEIGHT - 1; i >= THAIFONT_TONES_HEIGHT; i--) {
                            glyph[i] = glyph[i - THAIFONT_TONES_HEIGHT];
                        }
                        for (uint8_t i = 0; i < THAIFONT_TONES_HEIGHT; i++) glyph[i] = 0;
                    }
                } else {
                    haveUpperVowel = (cp >= 0x0E34 && cp <= 0x0E37);
                }
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

// 1x Thai String (wrapper ไปยัง drawThaiStringScaled เพื่อประหยัด Flash)
inline void drawThaiString(int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg = ST77XX_BLACK) {
    (void)bg;
    drawThaiStringScaled(x, y, str, color, 1);
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
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
        tft.setCursor(10, 5);
        tft.print(F("IP: "));
        tft.print(WiFi.localIP());
        // เตือนบนจอเมื่อยังไม่ได้เปลี่ยนรหัสผ่านหน้าเว็บ
        if (usingDefaultWebPass()) {
            tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
            tft.setCursor(190, 5);
            tft.print(F("[!PW]"));
        }
    } else if (isAPModeActive) {
        tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
        tft.setCursor(10, 5);
        tft.print(F("AP: 192.168.4.1 (SmartClock)"));
    } else {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
        tft.setCursor(10, 5);
        tft.print(F("WiFi: Connecting..."));
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
        tft.setTextColor(color, ST77XX_BLACK);
        tft.setTextSize(3);
        tft.setCursor(20, 192);
        tft.print(priceStr);
    } else {
        tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
        tft.setTextSize(2);
        tft.setCursor(20, 196);
        tft.print(F("Loading..."));
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
void drawBootScreen() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setTextSize(3);
    tft.setCursor(26, 78);
    tft.print(F("SMART"));
    tft.setCursor(26, 108);
    tft.print(F("CLOCK"));
    tft.setTextColor(ST77XX_BLUE, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setCursor(92, 142);
    tft.printf_P(PSTR("v%s"), FW_VERSION);
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

    static bool hadValidTime = false;
    time_t now = time(nullptr);
    if (now > 1000000) {
        if (!hadValidTime) {
            tft.fillRect(0, 20, 240, 45, ST77XX_BLACK);
            hadValidTime = true;
        }
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
        // วาดตัวเลขทับด้วย text color + background color ทันที (flicker-free)
        tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
        tft.setTextSize(4);
        tft.setCursor(25, 25);
        tft.print(timeStr);
    } else {
        hadValidTime = false;
        tft.fillRect(0, 20, 240, 45, ST77XX_BLACK);
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
        tft.setTextSize(2);
        tft.setCursor(45, 30);
        tft.print(F("NTP Syncing..."));
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

    // วาดเส้นราคาล่าสุด (close ของแท่งสุดท้าย)
    /*if (g.count > 0) {
        float lastClose = wval(g, (g.count - 1) * 4 + 3);
        int16_t yLast = toY(lastClose);
        tft.drawFastHLine(g.x, yLast, plotW, ST77XX_YELLOW);
    }*/
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

    // ป้ายกำกับ 1x บนสุดของการ์ด - ใช้ g.text
    if (g.text[0]) {
        int16_t tw = (int16_t)strlen(g.text) * 6;
        tft.setTextSize(1);
        tft.setTextColor(DASH_MUTED_COLOR);
        tft.setCursor(g.x + (g.w - tw) / 2, g.y + 4);
        tft.print(g.text);
    }

    // ตัวเลขหลัก: ใช้ label ถ้าส่งมา (จัดรูปแบบฝั่ง HA ได้อิสระ) ไม่งั้นพิมพ์จาก data[0]
    char buf[DASH_LABEL_LEN];
    if (g.label[0]) {
        strlcpy(buf, g.label, sizeof(buf));
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
    int16_t top = g.y + (g.text[0] ? 10 : 0);
    int16_t boxH = g.h - (g.text[0] ? 10 : 0);
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
static void ppAppend(String &out, const char* tag, const char* val, size_t valLen) {
    char len[3];
    snprintf(len, sizeof(len), "%02u", (unsigned)valLen);
    out += tag;
    out += len;
    out.concat(val, valLen);
}

static inline void ppAppend(String &out, const char* tag, const String &val) {
    ppAppend(out, tag, val.c_str(), val.length());
}

static inline void ppAppend(String &out, const char* tag, const char* val) {
    ppAppend(out, tag, val, strlen(val));
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
    char digits[16];
    uint8_t dlen = 0;
    for (const char* p = id; *p && dlen < 15; p++) {
        if (isdigit((unsigned char)*p)) digits[dlen++] = *p;
    }
    digits[dlen] = '\0';

    const char* targetTag = nullptr;
    char targetVal[20];
    if (dlen == 10 && digits[0] == '0') {
        // เบอร์โทร — แปลงเป็น 66 + เบอร์แบบตัด 0 นำหน้า ตามสเปก tag 01 (mobile)
        targetTag = "01";
        snprintf(targetVal, sizeof(targetVal), "0066%s", digits + 1);
    } else if (dlen == 13) {
        // เลขบัตรประชาชน 13 หลัก — tag 02 (national ID)
        targetTag = "02";
        strlcpy(targetVal, digits, sizeof(targetVal));
    } else {
        return String(); // รูปแบบไม่ตรงทั้งเบอร์โทรและเลขบัตร ปฏิเสธไปเลยดีกว่าสร้าง QR ผิด
    }

    // Merchant Account Information (tag 29) ของ PromptPay: GUID + ตัวระบุผู้รับเงิน
    String mai;
    mai.reserve(48);
    ppAppend(mai, "00", "A000000677010111"); // GUID มาตรฐาน PromptPay ตรึงตายตัว
    ppAppend(mai, targetTag, targetVal);

    String payload;
    payload.reserve(128);
    ppAppend(payload, "00", "01");             // Payload Format Indicator
    ppAppend(payload, "01", amount > 0 ? "12" : "11"); // Point of Initiation: 12=ตรึงยอด, 11=ไม่ตรึง
    ppAppend(payload, "29", mai);
    ppAppend(payload, "53", "764");            // Transaction Currency = THB (ISO 4217)
    if (amount > 0) {
        char amtBuf[16];
        int amtLen = snprintf(amtBuf, sizeof(amtBuf), "%.2f", amount);
        ppAppend(payload, "54", amtBuf, amtLen);
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

// QR code — เข้ารหัสจาก text (payload ยาวได้ถึง ~150 ไบต์) ด้วยไลบรารีที่พอร์ตมาจาก
// github.com/anunpanya/ESP8266_QRcode (ตัวเดิม tz1/qrduino) ตรึงไว้ที่ VERSION 7 / ECC-L
// เป็นตาราง 45x45 module รองรับข้อมูลดิบ ~150 ไบต์
// ไลบรารีเดิมมี wrapper ผูกกับจอ SSD1306 โดยเฉพาะ (qrcode.cpp/.h ไม่ได้พอร์ตมา) จึงเรียก
// qrencode() ตรงๆ แล้ววาด module เองด้วย fillRect รวมแนวนอนเป็นแถบเดียวต่อ run
// (สีเดียวกันติดกันในแนว x) กัน SPI call ถี่เกินจำเป็น แบบเดียวกับที่ทำใน drawDonut
void drawQr(const char* text, int16_t x, int16_t y, int16_t w, int16_t h,
            uint16_t bg, uint16_t fg) {
    if (!text || !text[0]) return;

    // strinbuf ยาว 270 ไบต์ (ประกาศจริงใน frame.c) แต่ qrencode.h ประกาศแบบ
    // extern unsigned char strinbuf[] (incomplete type) เอา sizeof() ไม่ได้ — ต้องฮาร์ดโค้ด
    // ตรึง VERSION7/ECC-L ใช้จริงแค่ ~150 ไบต์แรก ตัดข้อความยาวเกินไว้ก่อนกันเผื่อ
    const size_t STRINBUF_LEN = 270;
    size_t len = strlen(text);
    if (len > 150) len = 150;
    memset(strinbuf, 0, STRINBUF_LEN);
    memcpy(strinbuf, text, len);
    strinbuf[len] = 0;

    qrencode(); // strinbuf เข้า, qrframe ออก เป็นตาราง WD x WD (45x45) บิตแพ็ก

    const int16_t side = (w < h) ? w : h;
    int16_t px = side / WD; // ขนาดพิกเซลต่อ module
    if (px < 1) px = 1;
    const int16_t qrSize = px * WD;
    const int16_t ox = x + (w - qrSize) / 2; // จัดกลางกรอบที่ขอ
    const int16_t oy = y + (h - qrSize) / 2;

    // เคลียร์พื้นด้วย bg ก่อน ครอบคลุม quiet zone รอบขอบด้วย
    tft.fillRect(x, y, w, h, bg);

    for (uint8_t yy = 0; yy < WD; yy++) {
        uint8_t xx = 0;
        while (xx < WD) {
            uint8_t bit = QRBIT(xx, yy);
            uint8_t runStart = xx;
            while (xx < WD && QRBIT(xx, yy) == bit) xx++;
            if (bit) { // วาดเฉพาะ module มืด เพราะพื้น bg ทาไว้แล้วทั้งกรอบ
                tft.fillRect(ox + runStart * px, oy + yy * px, (xx - runStart) * px, px, fg);
            }
        }
        yield();
    }
}

// ค้นหา widget ตามชนิด แล้วอ่านสเกลและขอบเขต สำหรับ primitive ที่อ้างอิงสเกล
// คืน false ถ้าไม่เจอ (ตัวแรกที่เจอ — ถ้ามีหลายตัวเลือกตัวแรก)
bool findChartScale(const char* refType, int16_t &rx, int16_t &ry, int16_t &rw, int16_t &rh, float &lo, float &hi) {
    for (uint8_t i = 0; i < dash.widgetCount; i++) {
        const Widget &g = dash.widgets[i];
        bool match = false;
        uint8_t stride = 1;

        if (!strcmp(refType, "candles") && g.type == W_CANDLES) { match = true; stride = 4; }
        else if (!strcmp(refType, "column") && g.type == W_COLUMN) { match = true; stride = 1; }
        else if (!strcmp(refType, "bar") && g.type == W_HBAR) { match = true; stride = 1; }
        else if (!strcmp(refType, "line") && g.type == W_LINE) { match = true; stride = 1; }
        else if (!strcmp(refType, "donut") && g.type == W_DONUT) { match = true; stride = 1; }

        if (match) {
            rx = g.x; ry = g.y; rw = g.w; rh = g.h;
            widgetRange(g, lo, hi, stride);
            return true;
        }
    }
    return false;
}

void renderDashboard() {
    // ล้างหรือวาดภาพพื้นหลังเฉพาะตอนวาด frame ใหม่
    if (dash.bgImg[0] != '\0' && LittleFS.exists(dash.bgImg)) {
        renderJpgImage(dash.bgImg);
    } else {
        tft.fillScreen(ST77XX_BLACK);
    }

    // มี QR อยู่ในเฟรมนี้ไหม — ใช้หรี่จอกันสว่างจ้าจนกล้องมือถือโฟกัสไม่ติด (ผู้ใช้แจ้งบัค)
    bool hasQr = (dash.qrText[0] != '\0');
    setBacklightPct(hasQr ? QR_DIM_BRIGHTNESS_PCT : sysConfig.brightness);

    // Pass 1: วาดกราฟ — สร้างสเกลให้ primitive อ้างอิงได้
    for (uint8_t i = 0; i < dash.widgetCount; i++) {
        yield();
        const Widget &g = dash.widgets[i];

        switch (g.type) {
            case W_CANDLES: if (g.count) drawCandles(g);    break;
            case W_COLUMN:  if (g.count) drawColumns(g);    break;
            case W_HBAR:    if (g.count) drawHBars(g);      break;
            case W_LINE:    if (g.count) drawLineChart(g);  break;
            case W_DONUT:   if (g.count) drawDonut(g);      break;
            case W_KPI:     drawKpi(g);                     break;
            case W_GAUGE:   drawGauge(g);                   break;
            default: break;
        }
    }

    // Pass 2: วาด primitive และข้อความ — อ้างอิงสเกลจาก pass 1 ได้
    for (uint8_t i = 0; i < dash.widgetCount; i++) {
        yield();
        const Widget &g = dash.widgets[i];

        switch (g.type) {
            case W_TEXT:
                if (g.text[0]) drawThaiStringScaled(g.x, g.y, g.text, g.color, g.scale);
                break;
            case W_RECT:
                if (g.filled) tft.fillRect(g.x, g.y, g.w, g.h, g.color);
                else          tft.drawRect(g.x, g.y, g.w, g.h, g.color);
                break;
            case W_HLINE:
                if (g.hasRef) {
                    int16_t rx, ry, rw, rh;
                    float lo, hi;
                    if (findChartScale(g.refType, rx, ry, rw, rh, lo, hi)) {
                        float span = hi - lo;
                        if (span < 0.0001f) span = 0.0001f;
                        float frac = (g.refValue - lo) / span;
                        if (frac < 0.0f) frac = 0.0f;
                        if (frac > 1.0f) frac = 1.0f;
                        int16_t yMapped = ry + rh - 1 - (int16_t)(frac * (rh - 1));
                        tft.drawFastHLine(rx, yMapped, rw, g.color);
                    }
                    // ถ้าหาไม่เจอ ข้ามไป (ไม่วาดอะไร)
                } else {
                    tft.drawFastHLine(g.x, g.y, g.w, g.color);
                }
                break;
            case W_VLINE:
                if (g.hasRef) {
                    // TODO: map value เป็น X coordinate สำหรับกราฟแนวนอน
                    // ตอนนี้ยังไม่ implement — ข้ามไป
                } else {
                    tft.drawFastVLine(g.x, g.y, g.h, g.color);
                }
                break;
            default: break;
        }
    }

    // วาด QR code ตอนท้าย (ถ้ามี)
    if (hasQr) {
        drawQr(dash.qrText, dash.qrX, dash.qrY, dash.qrW, dash.qrH,
               dash.qrBg, dash.qrFg);
    }

    dash.dirty = false;
}

// สลับโหมด — ล้างจอเฉพาะจังหวะเปลี่ยนโหมด กัน flicker ตอน refresh ปกติ
void switchToDashboard() {
    if (!dash.valid) return;
    displayMode = MODE_DASHBOARD;
    renderDashboard();
}

void renderJpgImage(const char* path) {
    if (!LittleFS.exists(path)) {
        appLog(String(F("Image not found: ")) + path);
        return;
    }
    File jpgFile = LittleFS.open(path, "r");
    if (!jpgFile) {
        appLog(String(F("Failed to open image: ")) + path);
        return;
    }
    tft.startWrite();
    TJpgDec.drawFsJpg(0, 0, jpgFile);
    tft.endWrite();
    jpgFile.close();
    imageDirty = false;
}

void renderLiveMessage() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 30);
    tft.println(liveLine1);
    if (liveLine2[0] != '\0') {
        tft.setCursor(10, 70);
        tft.setTextColor(ST77XX_CYAN);
        tft.println(liveLine2);
    }
    liveDirty = false;
}

void switchToImage(const char* path) {
    strlcpy(currentImagePath, path, sizeof(currentImagePath));
    displayMode = MODE_IMAGE;
    imageDirty = true;
    lastImagePush = millis();
    renderJpgImage(currentImagePath);
}

void switchToLive() {
    displayMode = MODE_LIVE;
    liveDirty = true;
    lastLivePush = millis();
    renderLiveMessage();
}

void switchToClock() {
    displayMode = MODE_CLOCK;
    // กลับหน้านาฬิกาแล้วต้องคืนความสว่างเดิมด้วย เผื่อออกมาจากหน้า QR ที่หรี่ไว้
    setBacklightPct(sysConfig.brightness);
    initStaticLCDScreen(); // วาดใหม่ทั้งใบ เพราะ dashboard/image ทับพื้นที่เดิมไปหมด
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
    http.useHTTP10(true);

    String url = String(GEOCODE_HOST) + "/v1/search?count=1&language=th&format=json&name=" + urlEncode(sysConfig.city);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf_P(PSTR("Geocode failed: HTTP %d\n"), code);
        http.end();
        return false;
    }

    StaticJsonDocument<80> filter;
    filter["results"][0]["latitude"] = true;
    filter["results"][0]["longitude"] = true;

    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf_P(PSTR("Geocode JSON error: %s\n"), err.c_str());
        return false;
    }

    JsonArray results = doc["results"].as<JsonArray>();
    if (results.isNull() || results.size() == 0) {
        Serial.println(F("Geocode: city not found"));
        return false;
    }

    sysConfig.lat = results[0]["latitude"] | 0.0f;
    sysConfig.lon = results[0]["longitude"] | 0.0f;
    Serial.printf_P(PSTR("Geocoded %s -> %.4f, %.4f\n"), sysConfig.city, sysConfig.lat, sysConfig.lon);
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
    http.useHTTP10(true);

    char url[160];
    snprintf(url, sizeof(url),
             "%s/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
             WEATHER_HOST, sysConfig.lat, sysConfig.lon);

    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf_P(PSTR("Weather failed: HTTP %d\n"), code);
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
        Serial.printf_P(PSTR("Weather JSON error: %s\n"), err.c_str());
        return false;
    }

    JsonObject current = doc["current"];
    if (current.isNull() || !current.containsKey("temperature_2m")) {
        Serial.println(F("Weather: unexpected payload"));
        return false;
    }

    weather.tempC = current["temperature_2m"] | 0.0f;
    weather.code = current["weather_code"] | -1;
    weather.valid = true;
    weather.stale = false;
    weather.lastOk = millis();
    Serial.printf_P(PSTR("Weather: %.1fC code=%d (%s)\n"), weather.tempC, weather.code, weatherCodeToThai(weather.code));
    return true;
}

bool fetchGold() {
    if (WiFi.status() != WL_CONNECTED) return false;

    // BearSSL buffer ถูกตั้งค่าให้เล็ก (1024/512) จึงต้องการ Heap เพียง ~8 KB
    if (ESP.getFreeHeap() < 8000) {
        Serial.printf_P(PSTR("Gold: skipped, heap too low (%u)\n"), ESP.getFreeHeap());
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setBufferSizes(1024, 512); // ปรับ buffer size ของ BearSSL เพื่อลดการใช้ Heap ลง ~12KB

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.useHTTP10(true);
    if (!http.begin(client, GOLD_URL)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf_P(PSTR("Gold failed: HTTP %d\n"), code);
        http.end();
        return false;
    }

    StaticJsonDocument<48> filter;
    filter["price"] = true;

    StaticJsonDocument<96> doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf_P(PSTR("Gold JSON error: %s\n"), err.c_str());
        return false;
    }

    if (!doc.containsKey("price")) {
        Serial.println(F("Gold: unexpected payload"));
        return false;
    }

    float newPrice = doc["price"] | 0.0f;
    if (newPrice <= 0.0f) {
        Serial.println(F("Gold: price out of range"));
        return false;
    }

    // เก็บราคาเดิมไว้ตัดสินสี ครั้งแรกให้ prev เท่ากับราคาใหม่ สีจะเป็นขาว
    gold.prevPrice = gold.valid ? gold.price : newPrice;
    gold.price = newPrice;
    gold.valid = true;
    gold.stale = false;
    gold.lastOk = millis();
    Serial.printf_P(PSTR("Gold: $%.2f (prev $%.2f)\n"), gold.price, gold.prevPrice);
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
    server.send(200, F("application/json"), response);
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
    server.send(200, F("application/json"), response);
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
            server.send(200, F("text/plain"), weather.valid ? F("OK") : F("saved, but weather fetch failed"));
            return;
        }
        else if (key == "wifi_ssid") strlcpy(sysConfig.ssid, value.c_str(), sizeof(sysConfig.ssid));
        else if (key == "wifi_pass") strlcpy(sysConfig.password, value.c_str(), sizeof(sysConfig.password));
        else if (key == "lcd_brightness") {
            sysConfig.brightness = value.toInt();
            setBacklightPct(sysConfig.brightness);
        }
        else if (key == "web_user") {
            if (value.length() < 1) {
                server.send(400, F("text/plain"), F("username must not be empty"));
                return;
            }
            strlcpy(sysConfig.webUser, value.c_str(), sizeof(sysConfig.webUser));
        }
        else if (key == "web_pass") {
            // กันตั้งรหัสว่างหรือสั้นเกินไป ไม่งั้น auth จะไร้ความหมาย
            if (value.length() < 8) {
                server.send(400, F("text/plain"), F("password must be at least 8 characters"));
                return;
            }
            strlcpy(sysConfig.webPass, value.c_str(), sizeof(sysConfig.webPass));
        }

        saveConfigEEPROM(); // Persist to EEPROM
        server.send(200, F("text/plain"), F("OK"));
    } else {
        server.send(400, F("text/plain"), F("Bad Request"));
    }
}

// ---------------------------------------------------------------------------
// GeekMagic Standard API Handlers (Compatible with Home Assistant geekmagic-hacs)
// ---------------------------------------------------------------------------

void handleAppJson() {
    StaticJsonDocument<256> doc;
    doc["theme"] = 0;
    doc["brt"] = sysConfig.brightness;
    doc["img"] = currentImagePath;
    doc["gmtOffset"] = sysConfig.gmtOffset;
    String res;
    serializeJson(doc, res);
    server.send(200, F("application/json"), res);
}

void handleSpaceJson() {
    FSInfo fs_info;
    LittleFS.info(fs_info);
    StaticJsonDocument<128> doc;
    doc["total"] = fs_info.totalBytes;
    doc["free"] = fs_info.totalBytes - fs_info.usedBytes;
    String res;
    serializeJson(doc, res);
    server.send(200, F("application/json"), res);
}

void handleBrtJson() {
    String res = "{\"brt\":\"" + String(sysConfig.brightness) + "\"}";
    server.send(200, F("application/json"), res);
}

void handleVersionJson() {
    String res = "{\"version\":\"" FW_VERSION "\"}";
    server.send(200, F("application/json"), res);
}

void handleSet() {
    bool updated = false;

    if (server.hasArg("brt")) {
        int brt = server.arg("brt").toInt();
        sysConfig.brightness = constrain(brt, 0, 100);
        setBacklightPct(sysConfig.brightness);
        saveConfigEEPROM();
        updated = true;
    }

    if (server.hasArg("gmt")) {
        sysConfig.gmtOffset = server.arg("gmt").toInt();
        saveConfigEEPROM();
        configTime(sysConfig.gmtOffset, 0, "pool.ntp.org", "time.cloudflare.com");
        updated = true;
    }

    if (server.hasArg("img")) {
        String img = server.arg("img");
        strlcpy(currentImagePath, img.c_str(), sizeof(currentImagePath));
        displayMode = MODE_IMAGE;
        imageDirty = true;
        lastImagePush = millis();
        renderJpgImage(currentImagePath);
        updated = true;
    }

    if (server.hasArg("clear")) {
        if (server.arg("clear") == "image") {
            Dir dir = LittleFS.openDir(IMAGE_DIR);
            while (dir.next()) {
                LittleFS.remove(dir.fileName());
            }
            currentImagePath[0] = '\0';
            if (displayMode == MODE_IMAGE) switchToClock();
            updated = true;
        }
    }

    server.send(200, F("text/plain"), updated ? F("OK") : F("No action"));
}

void handleFileUpload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        String dir = IMAGE_DIR;
        if (server.hasArg("dir")) {
            dir = server.arg("dir");
        }
        if (!dir.endsWith("/")) dir += "/";
        if (!LittleFS.exists(dir)) {
            LittleFS.mkdir(dir);
        }

        String filepath = dir + filename;
        uploadFile = LittleFS.open(filepath, "w");
        appLog(String(F("Upload start: ")) + filepath);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            appLog(String(F("Upload complete: ")) + upload.filename + String(F(" (")) + String(upload.totalSize) + String(F(" bytes)")));
        }
    }
}

void handleUploadDone() {
    server.send(200, F("text/plain"), F("OK"));
}

void handleDelete() {
    if (server.hasArg("file")) {
        String filepath = server.arg("file");
        if (LittleFS.remove(filepath)) {
            if (String(currentImagePath) == filepath) {
                currentImagePath[0] = '\0';
                if (displayMode == MODE_IMAGE) switchToClock();
            }
            server.send(200, F("text/plain"), F("Deleted"));
        } else {
            server.send(404, F("text/plain"), F("Not found"));
        }
    } else {
        server.send(400, F("text/plain"), F("Missing file parameter"));
    }
}

void handleApiUpdate() {
    if (server.hasArg("plain")) {
        DynamicJsonDocument doc(1024);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (!err) {
            if (doc.containsKey("line1")) {
                strlcpy(liveLine1, doc["line1"] | "", sizeof(liveLine1));
            } else {
                liveLine1[0] = '\0';
            }
            if (doc.containsKey("line2")) {
                strlcpy(liveLine2, doc["line2"] | "", sizeof(liveLine2));
            } else {
                liveLine2[0] = '\0';
            }
            displayMode = MODE_LIVE;
            liveDirty = true;
            lastLivePush = millis();
            renderLiveMessage();
            server.send(200, F("text/plain"), F("OK"));
            return;
        }
        server.send(400, F("text/plain"), F("JSON parse error"));
    } else {
        server.send(400, F("text/plain"), F("No JSON body"));
    }
}

void handleReconfigureWiFi() {
    server.send(200, F("text/plain"), F("WiFi Reconfiguration triggered. Device restarting to AP mode."));
    delay(200);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("SmartClock-AP", "12345678");
    isAPModeActive = true;
    wifiLostSince = millis();
    switchToClock();
}

void handleFactoryReset() {
    server.send(200, F("text/plain"), F("Factory Reset triggered. Clearing data and restarting..."));
    delay(200);
    WiFi.disconnect(true);
    ESP.eraseConfig();
    LittleFS.format();
    sysConfig = ClockConfig();
    saveConfigEEPROM();
    delay(500);
    ESP.restart();
}

void handleLog() {
    server.send(200, F("text/plain"), getLogs());
}

void handleConnect() {
    if (!server.hasArg("ssid")) {
        server.send(400, F("text/plain"), F("Missing SSID parameter"));
        return;
    }
    String ssid = server.arg("ssid");
    String pass = server.hasArg("password") ? server.arg("password") : (server.hasArg("pass") ? server.arg("pass") : "");
    strlcpy(sysConfig.ssid, ssid.c_str(), sizeof(sysConfig.ssid));
    strlcpy(sysConfig.password, pass.c_str(), sizeof(sysConfig.password));
    saveConfigEEPROM();
    server.send(200, F("text/plain"), F("Connecting to WiFi... Device will restart."));
    delay(1000);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// POST /api/draw — รับ JSON template แล้วแปลงเป็น display list ใน RAM
// ไม่วาดในนี้ แค่ตั้งธง dirty ให้ loop() วาด เพื่อตอบ HTTP กลับให้เร็ว
// ---------------------------------------------------------------------------
void handleApiDraw() {
    if (!server.hasArg("plain")) {
        server.send(400, F("text/plain"), F("expected JSON body"));
        return;
    }

    const String &bodyRef = server.arg("plain");
    if (bodyRef.length() > DASH_MAX_BODY) {
        server.send(413, F("text/plain"), F("body too large"));
        return;
    }

    // กันไว้แบบเดียวกับตอนดึงทอง — ถ้า heap ไม่พอจอง document ให้ตอบ 503 ตรงๆ
    if (ESP.getFreeHeap() < DASH_DOC_SIZE + 8000) {
        Serial.printf_P(PSTR("Draw: rejected, heap too low (%u)\n"), ESP.getFreeHeap());
        server.send(503, F("text/plain"), F("heap too low, try again"));
        return;
    }

    // DynamicJsonDocument เพราะก้อนเข้ามาไม่คงที่ ปล่อยคืน heap ทันทีเมื่อออกจาก scope
    DynamicJsonDocument doc(DASH_DOC_SIZE);
    DeserializationError err = deserializeJson(doc, bodyRef);
    if (err) {
        server.send(400, F("text/plain"), String(F("JSON error: ")) + err.c_str());
        return;
    }

    JsonArray widgets = doc["widgets"].as<JsonArray>();
    if (widgets.isNull()) {
        server.send(400, F("text/plain"), F("missing widgets array"));
        return;
    }

    // ล้าง frame เดิมทั้งหมดก่อนรับของใหม่ — frame ต้องเป็นภาพสมบูรณ์ในตัวเอง
    dash.widgetCount = 0;
    dash.poolUsed = 0;
    dash.qrText[0] = '\0';  // ล้าง QR เก่า
    dash.bgImg[0] = '\0';   // ล้างภาพพื้นหลังเก่า

    // อ่านพาธภาพพื้นหลัง (ถ้ามี)
    if (doc.containsKey("bg_img")) {
        const char* bg = doc["bg_img"] | "";
        strlcpy(dash.bgImg, bg, sizeof(dash.bgImg));
    }

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

            // ป้ายกำกับบน (text)
            const char* s = wgt["text"] | "";
            if (*s) strlcpy(g.text, s, sizeof(g.text));

            // ค่าหลัก (label หรือ value)
            s = wgt["label"] | wgt["value"] | "";
            if (*s) strlcpy(g.label, s, sizeof(g.label));

            // รับได้ทั้ง value เดี่ยวและ data array — ต้องมีอย่างใดอย่างหนึ่งกับ label
            if (!g.label[0]) {
                if (wgt.containsKey("value") && dash.poolUsed < DASH_POOL_SIZE) {
                    g.start = dash.poolUsed;
                    dash.pool[dash.poolUsed++] = wgt["value"] | 0.0f;
                    g.count = 1;
                } else {
                    JsonArray data = wgt["data"].as<JsonArray>();
                    if (!data.isNull() && data.size() > 0) takeSeries(data, g, 1);
                }
            }
            if (!g.label[0] && g.count == 0) continue;
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
        else if (!strcmp(type, "qr")) {
            dash.qrX = wgt["x"] | 5;
            dash.qrY = wgt["y"] | 5;
            dash.qrW = wgt["w"] | 200;
            dash.qrH = wgt["h"] | 200;
            dash.qrBg = parseColor(wgt["color"]  | "", ST77XX_BLACK);  // พื้นหลัง
            dash.qrFg = parseColor(wgt["color2"] | "", ST77XX_WHITE); // module

            const char* ppId = wgt["promptpay_id"] | "";
            if (*ppId) {
                float amt = wgt["promptpay_amount"] | 0.0f;
                String payload = buildPromptPayPayload(ppId, amt);
                if (payload.length() == 0 || payload.length() >= sizeof(dash.qrText)) {
                    continue; // id ไม่ใช่รูปแบบที่รองรับ หรือยาวเกินบัฟเฟอร์
                }
                strlcpy(dash.qrText, payload.c_str(), sizeof(dash.qrText));
            } else {
                const char* s = wgt["text"] | "";
                if (!*s) continue; // ไม่มีข้อความให้เข้ารหัส ข้าม
                strlcpy(dash.qrText, s, sizeof(dash.qrText));
            }
            continue;
        }
        // ---- primitive วางเลย์เอาต์ ----
        else if (!strcmp(type, "rect") || !strcmp(type, "hline") || !strcmp(type, "vline")) {
            g.x = wgt["x"] | 0;
            g.y = wgt["y"] | 0;
            g.w = wgt["w"] | 240;
            g.h = wgt["h"] | 1;
            g.color = parseColor(wgt["color"] | "", DASH_GRID_COLOR);
            g.filled = wgt["fill"] | true;

            if (!strcmp(type, "rect")) g.type = W_RECT;
            else if (!strcmp(type, "hline")) g.type = W_HLINE;
            else if (!strcmp(type, "vline")) g.type = W_VLINE;

            // scale reference: อ้างอิงสเกลจากกราฟอื่น
            if (wgt.containsKey("ref")) {
                const char* refStr = wgt["ref"] | "";
                if (*refStr) {
                    g.hasRef = true;
                    g.refValue = wgt["value"] | 0.0f;
                    strlcpy(g.refType, refStr, sizeof(g.refType));
                }
            }
        }
        else {
            continue; // ชนิดที่ไม่รู้จัก ข้ามไปเงียบๆ ให้ widget อื่นใน frame ยังวาดได้
        }

        dash.widgetCount++;
    }

    if (dash.widgetCount == 0) {
        server.send(400, F("text/plain"), F("no drawable widget"));
        return;
    }

    dash.valid = true;
    dash.dirty = true;
    dash.lastPush = millis();

    // เข้าโหมด dashboard ให้เลย เว้นแต่สั่ง mode=clock มาพร้อมกัน
    const char* mode = doc["mode"] | "dashboard";
    if (!strcmp(mode, "dashboard")) displayMode = MODE_DASHBOARD;

    server.send(200, F("text/plain"), F("OK"));
}

void handleOTAUpdate() {
    HTTPUpload& upload = server.upload();

    if (!server.authenticate(sysConfig.webUser, sysConfig.webPass)) {
        if (upload.status == UPLOAD_FILE_START) {
            Serial.println(F("OTA rejected: unauthorized"));
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf_P(PSTR("Update Start: %s\n"), upload.filename.c_str());
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
            Serial.printf_P(PSTR("Update Success: %u bytes\n"), upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}

void setupWebServer() {
    // Web UI
    server.on("/", HTTP_GET, [](){
        if (!requireAuth()) return;
        server.send_P(200, "text/html", INDEX_HTML);
    });

    // GeekMagic Standard Compatibility Endpoints (เข้ากันได้กับ Home Assistant geekmagic-hacs)
    server.on("/app.json", HTTP_GET, handleAppJson);
    server.on("/space.json", HTTP_GET, handleSpaceJson);
    server.on("/brt.json", HTTP_GET, handleBrtJson);
    server.on("/version.json", HTTP_GET, handleVersionJson);
    server.on("/set", HTTP_GET, handleSet);
    server.on("/delete", HTTP_GET, handleDelete);
    server.on("/api/update", HTTP_POST, handleApiUpdate);
    server.on("/doUpload", HTTP_POST, handleUploadDone, handleFileUpload);
    server.on("/log", HTTP_GET, handleLog);
    server.on("/reconfigurewifi", HTTP_GET, handleReconfigureWiFi);
    server.on("/factoryreset", HTTP_GET, handleFactoryReset);
    server.on("/scan", HTTP_GET, handleScanWifi);
    server.on("/connect", HTTP_GET, handleConnect);

    // OTA Routes
    server.on("/update", HTTP_POST, [](){
        server.send(200, F("text/plain"), (Update.hasError()) ? F("FAIL") : F("OK - Rebooting..."));
        delay(500);
        ESP.restart();
    }, handleOTAUpdate);

    server.on("/update_ota", HTTP_POST, [](){
        if (!requireAuth()) return;
        server.send(200, F("text/plain"), (Update.hasError()) ? F("FAIL") : F("OK"));
        delay(500);
        ESP.restart();
    }, handleOTAUpdate);

    // SmartClock Specific Endpoints
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
            server.send(503, F("text/plain"), F("Wi-Fi not connected"));
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

    // สลับโหมดด้วยมือ /api/mode?to=clock|dashboard|image|toggle
    server.on("/api/mode", HTTP_GET, [](){
        if (!requireAuth()) return;
        String to = server.hasArg("to") ? server.arg("to") : "toggle";

        if (to == "clock") switchToClock();
        else if (to == "dashboard" || to == "toggle") {
            bool wantDash = (to == "dashboard") || (displayMode == MODE_CLOCK);
            if (wantDash) {
                if (!dash.valid) {
                    server.send(409, F("text/plain"), F("no dashboard frame pushed yet"));
                    return;
                }
                switchToDashboard();
            } else {
                switchToClock();
            }
        } else if (to == "image") {
            if (currentImagePath[0] != '\0' && LittleFS.exists(currentImagePath)) {
                switchToImage(currentImagePath);
            } else {
                server.send(409, F("text/plain"), F("no image file available"));
                return;
            }
        } else {
            server.send(400, F("text/plain"), F("to must be clock|dashboard|image|toggle"));
            return;
        }

        server.send(200, F("text/plain"), displayMode == MODE_DASHBOARD ? F("dashboard") : (displayMode == MODE_IMAGE ? F("image") : F("clock")));
    });

    server.on("/restart", HTTP_GET, [](){
        if (!requireAuth()) return;
        server.send(200, F("text/plain"), F("Restarting..."));
        delay(500);
        ESP.restart();
    });

    server.onNotFound([](){
        server.send(404, F("text/plain"), F("Not Found"));
    });
}

// ---------------------------------------------------------------------------
// Wi-Fi upkeep — ไม่บล็อก loop() เลย ใช้ millis() คุมจังหวะทั้งหมด
// ---------------------------------------------------------------------------

void maintainWifi() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < WIFI_CHECK_MS) return;
    lastCheck = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (wifiLostSince != 0) {
            Serial.print(F("Wi-Fi reconnected. IP: "));
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
        Serial.println(F("Wi-Fi lost. Will retry."));
        updateWifiStatusLCD();
    }

    // ยิงต่อใหม่เป็นจังหวะ ไม่ใช่รัวทุกรอบ loop
    if (millis() - lastReconnectTry >= WIFI_RETRY_MS) {
        lastReconnectTry = millis();
        Serial.println(F("Reconnecting to Wi-Fi..."));
        WiFi.disconnect();
        WiFi.begin(sysConfig.ssid, sysConfig.password);
    }

    // หลุดนานเกินกำหนดและยังไม่ได้เปิด AP — เปิดให้ผู้ใช้เข้ามาแก้ค่าได้
    if (!isAPModeActive && millis() - wifiLostSince >= WIFI_AP_FALLBACK_MS) {
        Serial.println(F("Wi-Fi down too long. Enabling AP mode for recovery."));
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
    Serial.println(F("\n=== GeekMagic SmallTV Firmware v" FW_VERSION " ==="));

    // Active Low Backlight Fix
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);

    // core ESP8266 Arduino >= 3.0.0 เปลี่ยน analogWrite() default range จาก 1023 เป็น 255
    analogWriteRange(1023);

    // Initialize ST7789 Display
    tft.init(240, 240, SPI_MODE3);
    tft.setRotation(2);

    // Initialize TJpg_Decoder
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tft_output);

    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println(F("LittleFS mount failed, formatting..."));
        LittleFS.format();
        LittleFS.begin();
    }
    Serial.println(F("LittleFS mounted."));

    // Load EEPROM Persistent Config
    loadConfigEEPROM();

    // หน้า loading — โชว์ทันทีหลัง EEPROM แทนจอมืด/ค้างระหว่างต่อ Wi-Fi
    drawBootScreen();
    drawBootStatus("กำลังต่อ Wi-Fi", 0);

    // WiFi STA Connection Attempt
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(sysConfig.ssid, sysConfig.password);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        drawBootStatus("กำลังต่อ Wi-Fi", retries % 4);
        retries++;
        yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("WiFi Connected! IP: "));
        Serial.println(WiFi.localIP());
        isAPModeActive = false;
        WiFi.softAPdisconnect(true);
        drawBootStatus("ต่อสำเร็จ", 0);
    } else {
        Serial.println(F("WiFi STA Failed. Enabling Smart AP Mode..."));
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("SmartClock-AP", "12345678");
        isAPModeActive = true;
        wifiLostSince = millis();
        drawBootStatus("เปิดโหมด AP", 0);
    }

    // NTP sync สำหรับ ESP8266 โดยใช้ gmtOffset จาก config
    configTime(sysConfig.gmtOffset, 0, "pool.ntp.org", "time.cloudflare.com");
    setupWebServer();
    server.begin();

    // เริ่มต้น mDNS (smartclock.local) สำหรับ Home Assistant discovery
    if (MDNS.begin("smartclock")) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "model", "SmartClock");
        MDNS.addServiceTxt("http", "tcp", "vendor", "Custom");
        MDNS.addServiceTxt("http", "tcp", "api", "geekmagic");
        MDNS.addServiceTxt("http", "tcp", "version", FW_VERSION);
        Serial.println(F("mDNS responder started: smartclock.local"));
    }

    Serial.printf_P(PSTR("Web UI protected. user=%s\n"), sysConfig.webUser);
    if (usingDefaultWebPass()) {
        Serial.println(F("WARNING: still using the default web password. Change it at the web UI."));
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
    MDNS.update();
    yield();

    maintainWifi();
    updateDataIfDue();

    // มี frame Dashboard ใหม่ที่ push เข้ามา — วาดที่นี่ ไม่วาดใน HTTP handler
    if (displayMode == MODE_DASHBOARD && dash.dirty) {
        renderDashboard();
    }

    // Dashboard TTL หมด — กลับหน้านาฬิกาเอง
    if (displayMode == MODE_DASHBOARD && millis() - dash.lastPush >= DASH_TTL_MS) {
        Serial.println(F("Dashboard TTL expired. Back to clock."));
        dash.valid = false;
        switchToClock();
    }

    // Image TTL หมด — กลับหน้านาฬิกาเอง
    if (displayMode == MODE_IMAGE && millis() - lastImagePush >= IMAGE_TTL_MS) {
        Serial.println(F("Image TTL expired. Back to clock."));
        switchToClock();
    }

    // Live Message TTL หมด — กลับหน้านาฬิกาเอง
    if (displayMode == MODE_LIVE && millis() - lastLivePush >= LIVE_TTL_MS) {
        Serial.println(F("Live message TTL expired. Back to clock."));
        switchToClock();
    }

    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 1000) {
        lastDisplayUpdate = millis();
        if (displayMode == MODE_CLOCK) {
            updateTimeOnly();
        }
        updateWifiStatusLCD();
    }
}
