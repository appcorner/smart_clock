# Candle Widget Enhancements (v3.5.3+)

## Overview

Two new features for the dashboard widget system:
1. **Automatic last-price line** on candle charts
2. **Scale-referenced positioning** for hline/vline primitives

---

## Feature 1: Last-Price Line

### Description
Candle charts automatically draw a horizontal line showing the close price of the most recent (rightmost) candle.

### Visual Appearance
- **Color:** Yellow (`ST77XX_YELLOW`)
- **Position:** Mapped through the same scale as candle bodies
- **Width:** Spans full chart width (respects axis gutter if present)

### Activation
**Automatic** — no JSON configuration needed. Every candle chart with at least one candle (`count > 0`) gets a last-price line.

### Example
```json
{
  "widgets": [
    {
      "type": "candles",
      "x": 5, "y": 40, "w": 230, "h": 140,
      "data": [
        [2610, 2618, 2604, 2615],
        [2615, 2622, 2611, 2612],
        [2612, 2620, 2606, 2618]
      ]
    }
  ]
}
```

**Result:** Yellow horizontal line at Y-coordinate matching price 2618 (close of last candle).

---

## Feature 2: Scale-Referenced Positioning

### Description
`hline` and `vline` widgets can now position themselves by **data value** (e.g., price, index) rather than pixel coordinates, by referencing another chart's scale.

### New JSON Fields

| Field | Type | Description |
|---|---|---|
| `value` | float | Data value to position (e.g., `2615` for price) |
| `ref` | string | Widget type to reference: `"candles"`, `"column"`, `"bar"`, `"line"`, `"donut"` |

### Behavior
- **With `ref`:** Widget finds first matching chart, maps `value` through that chart's scale
- **Without `ref`:** Uses absolute pixel coordinates (backward compatible)
- **ref not found:** Widget is skipped (not drawn)

### Example 1: Price Reference Line
```json
{
  "widgets": [
    {
      "type": "candles",
      "x": 5, "y": 40, "w": 230, "h": 140,
      "data": [[2610,2618,2604,2615], [2615,2622,2611,2612], [2612,2620,2606,2618]]
    },
    {
      "type": "hline",
      "value": 2615,
      "ref": "candles",
      "color": "red"
    }
  ]
}
```

**Result:** Red horizontal line at Y-coordinate where price 2615 appears on the candle scale.

### Example 2: Multiple Reference Lines
```json
{
  "widgets": [
    {
      "type": "candles",
      "x": 5, "y": 40, "w": 230, "h": 140,
      "data": [[2610,2618,2604,2615], [2615,2622,2611,2612], [2612,2620,2606,2618]]
    },
    {
      "type": "hline",
      "value": 2615,
      "ref": "candles",
      "color": "red"
    },
    {
      "type": "hline",
      "value": 2610,
      "ref": "candles",
      "color": "cyan"
    }
  ]
}
```

**Result:** Yellow last-price line (automatic) + red line at 2615 + cyan line at 2610.

### Example 3: Backward Compatibility
```json
{
  "widgets": [
    {
      "type": "hline",
      "x": 10, "y": 100, "w": 220,
      "color": "cyan"
    }
  ]
}
```

**Result:** Cyan line at pixel Y=100 (no `ref` field → absolute positioning still works).

---

## Supported Reference Types

| `ref` value | Widget type | Stride | Notes |
|---|---|---|---|
| `"candles"` | W_CANDLES | 4 | OHLC data — scale from high/low range |
| `"column"` | W_COLUMN | 1 | Vertical bar chart |
| `"bar"` | W_HBAR | 1 | Horizontal bar chart |
| `"line"` | W_LINE | 1 | Line chart / sparkline |
| `"donut"` | W_DONUT | 1 | Donut/pie chart |

**Lookup behavior:** First widget of matching type is used. If multiple charts of same type exist, only the first one's scale is referenced.

---

## Technical Implementation

### Widget Struct Changes
```cpp
struct Widget {
    // ... existing fields
    bool hasRef = false;        // true = use value + refType instead of absolute x/y
    float refValue = 0.0f;      // data value to map through scale
    char refType[12] = "";      // widget type name to reference
};
```

**Memory overhead:** +16 bytes per widget (struct padding included).

### Two-Pass Rendering
```cpp
void renderDashboard() {
    // Pass 1: Render charts (establish scale)
    for (charts: candles, column, bar, line, donut, kpi, gauge, qr)
        renderWidget(g);

    // Pass 2: Render primitives and text (can reference scales from pass 1)
    for (primitives: text, rect, hline, vline)
        renderWidget(g);
}
```

### Scale Lookup
```cpp
bool findChartScale(const char* refType, int16_t &rx, int16_t &ry, 
                   int16_t &rw, int16_t &rh, float &lo, float &hi) {
    // Search dash.widgets[] for first widget matching refType
    // Return bounds (x,y,w,h) and scale (lo,hi) via widgetRange()
    // Return false if not found
}
```

### Coordinate Mapping
Same formula as candle rendering:
```cpp
float frac = (value - lo) / (hi - lo);
frac = clamp(frac, 0.0f, 1.0f);
int16_t yMapped = ry + rh - 1 - (int16_t)(frac * (rh - 1));
```

---

## Edge Cases

| Case | Behavior |
|---|---|
| No candles (`count == 0`) | Last-price line not drawn |
| `ref` widget not found | Primitive skipped (not drawn) |
| Multiple charts of same type | First match used |
| `ref` to non-chart widget (e.g., `"text"`) | Not found, skipped |
| `value` outside scale range | Clamped to chart bounds (0.0–1.0 frac) |
| Doji candle (open == close) | Last-price line still draws at that Y |

---

## Testing

### Test Script
```bash
bash scripts/test_candle_features.sh <device-ip> <username> <password>
```

**Tests:**
1. Candle chart with automatic last-price line
2. Scale-referenced hline at specific price
3. Multiple reference lines (last-price + 2 custom)
4. Backward compatibility (absolute positioning)

### Manual Verification
```bash
# Test 1: Last-price line
curl --user admin:smartclock -X POST http://192.168.1.50/api/draw \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {"type": "candles", "x": 5, "y": 40, "w": 230, "h": 140,
       "data": [[2610,2618,2604,2615], [2615,2622,2611,2612], [2612,2620,2606,2618]]}
    ]
  }'

# Test 2: Scale-referenced hline
curl --user admin:smartclock -X POST http://192.168.1.50/api/draw \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {"type": "candles", "x": 5, "y": 40, "w": 230, "h": 140,
       "data": [[2610,2618,2604,2615], [2615,2622,2611,2612], [2612,2620,2606,2618]]},
      {"type": "hline", "value": 2615, "ref": "candles", "color": "red"}
    ]
  }'

# Return to clock mode
curl --user admin:smartclock 'http://192.168.1.50/api/mode?to=clock'
```

---

## Resource Impact

**Firmware v3.5.3:**
- **RAM:** 49.4% (40444/81920 bytes) — up 0.2% from v3.5.2
- **Flash:** 48.5% (507031/1044464 bytes) — stable
- **Binary size:** 499.2 KB (up 0.8 KB from v3.5.2)

**Per-widget overhead:**
- Widget struct: +16 bytes (refValue, refType[12], hasRef + padding)
- Code: ~100 lines added (~2 KB Flash)

---

## Limitations

1. **vline with scale reference:** Not fully implemented yet — only absolute positioning works
2. **First match only:** If multiple charts of same type exist, only first one's scale is used
3. **No color configuration:** Last-price line is always yellow (hardcoded `ST77XX_YELLOW`)
4. **No toggle:** Last-price line is always drawn (cannot be disabled via JSON)

---

## Future Enhancements

1. Make last-price line configurable:
   ```json
   {
     "type": "candles",
     "last_price_line": true,      // toggle on/off
     "last_price_color": "cyan"    // custom color
   }
   ```

2. Support vline scale reference for horizontal charts and time-series index mapping

3. Allow referencing specific widget by index or ID (instead of type-based first match)

4. Add `closest` ref mode to find nearest chart spatially (instead of first in array)

---

*Feature implemented in firmware v3.5.3 — 2026-08-11*
