# API Draw Specification

## Endpoint

```
POST /api/draw
```

Send JSON dashboard template to draw charts and widgets on ST7789 240×240px display

**Authentication**: Requires HTTP Basic Auth

---

## Request Format

```json
{
  "mode": "dashboard" | "clock",
  "widgets": [ ... ]
}
```

### Parameters

- **mode** (string, optional): Display mode after successful push
  - `"dashboard"` (default): Switch to dashboard mode immediately
  - `"clock"`: Push data but stay in clock mode (let user switch manually)

- **widgets** (array, required): List of widgets to draw (max `DASH_MAX_WIDGETS` items)

---

## Widget Types

### 1. Text / Title

Display plain text

```json
{
  "type": "text" | "title",
  "text": "Message",
  "x": 5,
  "y": 190,
  "size": 2,
  "color": "white"
}
```

**Parameters:**
- `text` (string, required): Text to display (max 48 characters)
- `x` (int, optional): X coordinate (default: `5`)
- `y` (int, optional): Y coordinate
  - `text`: default `190`
  - `title`: default `6` (top of screen)
- `size` (int, optional): Font size `1-3` (default: `2`)
- `color` (string, optional): Color (default: `"white"`)

**Example:**
```json
{ "type": "title", "text": "Today's Summary", "color": "orange" }
{ "type": "text", "text": "Total 1,430", "y": 200, "size": 2, "color": "white" }
```

---

### 2. KPI Card

Large metric card with label

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

**Parameters:**
- `label` (string, optional): Label at the top
- `text` (string, optional): Text to display (use instead of value for custom formatting)
- `value` (float, optional): Number to display (use with `decimals`)
- `data` (array, optional): Use instead of `value` to send multiple values (displays first value)
- `decimals` (int, optional): Number of decimal places `0-4` (default: `0`)
- `size` (int, optional): Number size `1-6` (default: `3`)
- `threshold` (float, optional): Threshold value — if value ≥ threshold, switches to `color2`
- `color` (string, optional): Number color (default: `"white"`)
- `color2` (string, optional): Color when threshold exceeded (default: `"red"`)
- `axis` (bool, optional): Draw border (default: `false`)
- `x`, `y`, `w`, `h`: Position and size (default: `5, 40, 230, 60`)

**Example:**
```json
{ "type": "kpi", "label": "COST USD", "value": 18.42, "decimals": 2,
  "x": 125, "y": 40, "w": 110, "h": 62, "size": 2, "color": "yellow",
  "threshold": 15, "color2": "red", "axis": true }
```

---

### 3. Gauge

Horizontal progress bar

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

**Parameters:**
- `value` (float, required): Current value
- `min`, `max` (float, required): Scale bounds
- `threshold` (float, optional): Threshold — if value ≥ threshold, bar switches to `color2`
- `color` (string, optional): Normal bar color (default: `"green"`)
- `color2` (string, optional): Threshold exceeded color (default: `"red"`)
- `x`, `y`, `w`, `h`: Position and size (default: `5, 40, 230, 14`)

---

### 4. Bar Chart (Horizontal Bars)

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

**Parameters:**
- `data` (array, required): Data series (max `DASH_MAX_POINTS` points)
- `threshold` (float, optional): Bars with value ≥ threshold use `color2`
- `values` (bool, optional): Show values at end of bars (default: `false`)
- `decimals` (int, optional): Decimal places to display (default: `0`)
- `color` (string, optional): Normal bar color (default: `"green"`)
- `color2` (string, optional): Threshold exceeded color (default: `"red"`)
- `axis` (bool, optional): Draw border (default: `true`)
- Common: `x`, `y`, `w`, `h`, `min`, `max`, `label`

---

### 5. Column Chart (Vertical Bars)

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

**Parameters:** Same as Bar Chart

---

### 6. Line Chart / Sparkline

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

**Parameters:**
- `data` (array, required): Data series
- `fill` (bool, optional): Fill area under line (default: `false`)
- `color` (string, optional): Line color (default: `"green"`)
- `color2` (string, optional): Shadow color under line (default: `"#0208"` = light gray)
- **sparkline**: `axis` default = `false` (minimal line with no border)
- **line**: `axis` default = `true`
- Common: `decimals`, `min`, `max`, `label`

---

### 7. Candlestick Chart

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

**Parameters:**
- `data` (array of [O,H,L,C], required): Each point is a 4-value array `[open, high, low, close]`
- `color` (string, optional): Bullish candle color (close ≥ open) (default: `"green"`)
- `color2` (string, optional): Bearish candle color (close < open) (default: `"red"`)
- Common: `axis`, `min`, `max`, `decimals`

---

### 8. Donut / Pie Chart

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

**Parameters:**
- `data` (array, required): Proportion of each slice (3 rotating colors: green, yellow, orange)
- `hole` (int, optional): Center hole percentage `0-95`
  - `donut`: default `55`
  - `pie`: default `0`
- `color` (string, optional): Center text color (default: `"white"`)
- Common: `x`, `y`, `w`, `h`

---

### 9. QR Code

> **Note**: QR is not a regular widget — the firmware stores it directly in the Dashboard struct, so:
> - Only **1 QR per frame** (if multiple are sent, the last one wins)
> - QR does **not count** toward `DASH_MAX_WIDGETS`
> - Always drawn **last** (on top of all other widgets)
> - Backlight **auto-dims** when QR is present for easier camera scanning

Draw QR code for URL or PromptPay

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

**Parameters:**
- **Option 1**: General text
  - `text` (string, required): Text to encode (max 160 characters)
- **Option 2**: PromptPay QR
  - `promptpay_id` (string, required): 10-digit phone number or 13-digit ID card number
  - `promptpay_amount` (float, optional): Amount (if not sent = let payer enter amount)
- `color` (string, optional): **Background** / quiet zone color (default: `"black"`)
- `color2` (string, optional): **Module** / dot color (default: `"white"`)
- `x`, `y`, `w`, `h`: Position and size (default: `5, 5, 200, 200`)

**Note**: Firmware uses fixed QRcode Version 7 / ECC-L (45×45 modules)

---

### 10. Primitives (Layout Elements)

#### Rectangle

```json
{
  "type": "rect",
  "x": 0, "y": 0, "w": 240, "h": 30,
  "color": "grey",
  "fill": true
}
```

#### Horizontal Line

```json
{
  "type": "hline",
  "x": 5, "y": 178, "w": 230,
  "color": "grey"
}
```

**Parameters:**
- `fill` (bool, optional): Fill color (rect only, default: `true`)
- `color` (string, optional): Color (default: `DASH_GRID_COLOR`)
- `x`, `y`, `w`, `h`: Position and size

---

## Common Parameters (for multiple widget types)

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| `x`, `y` | int | Top-left corner position (pixels) | `5, 40` |
| `w`, `h` | int | Width × height (pixels) | `230, 140` |
| `label` | string | Label at the top (max 16 characters) | `""` |
| `color` | string | Primary color | widget-specific |
| `color2` | string | Secondary color (threshold, shadow) | widget-specific |
| `axis` | bool | Draw border and axes | `true` (graph), `false` (kpi) |
| `fill` | bool | Fill area (line/rect) | `false` |
| `values` | bool | Show values on chart | `false` |
| `decimals` | int | Number of decimal places `0-4` | `0` |
| `min`, `max` | float | Scale bounds (both required = fixed scale) | autoscale |
| `threshold` | float | Threshold for color switching | none |

---

## Color Values

Supports 3 formats:

1. **Named colors** (case-insensitive):
   - `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`
   - `orange`, `purple`, `pink`, `lime`, `teal`, `brown`
   - `white`, `black`, `grey`/`gray`, `darkgrey`/`darkgray`, `lightgrey`/`lightgray`

2. **RGB565 Hex**: `#RRRRRGGGGGBBBBB` (16-bit) e.g. `#F800` (red), `#07E0` (green)

3. **RGB888 Hex**: `#RRGGBB` (24-bit) e.g. `#FF5733`, `#062808`
   - Automatically converted to RGB565

---

## Response

### Success (200 OK)
```
OK
```

### Errors

| Code | Message | Meaning |
|------|---------|---------|
| 400 | `expected JSON body` | No body or not JSON |
| 400 | `JSON error: ...` | JSON parse failed |
| 400 | `missing widgets array` | Missing `widgets` field |
| 400 | `no drawable widget` | All widgets failed validation (missing data/unknown type) |
| 413 | `body too large` | body > `DASH_MAX_BODY` bytes |
| 503 | `heap too low, try again` | Insufficient RAM, wait and retry |

---

## Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `DASH_MAX_WIDGETS` | 16 | Maximum widgets per frame |
| `DASH_MAX_POINTS` | 255 | Maximum data points per chart |
| `DASH_POOL_SIZE` | 1024 | Total space for all data points |
| `DASH_MAX_BODY` | 8192 | Maximum JSON body size (bytes) |
| `DASH_DOC_SIZE` | 10240 | ArduinoJson document size (bytes) |

---

## Example: Mini Dashboard

```json
{
  "mode": "dashboard",
  "widgets": [
    { "type": "title", "text": "System Overview", "color": "orange" },
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

## Curl Example

```bash
curl -u admin:password -X POST http://192.168.1.100/api/draw \
  -H "Content-Type: application/json" \
  -d @examples/09_mixed.json
```

---

## Related Endpoints

- `GET /api/mode?to=clock|dashboard|toggle` — Switch display mode
- `GET /config` — System settings (Wi-Fi, brightness, API keys)
- `GET /` — Web UI for browser control
