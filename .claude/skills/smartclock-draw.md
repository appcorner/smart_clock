---
name: smartclock-draw
description: Send dashboard widgets to GeekMagic SmallTV smart clock (ESP8266 ST7789 240x240px display) via /api/draw HTTP POST
skill_type: user-invocable
---

# SmartClock Draw Skill

Use this skill to send dashboard visualizations to a GeekMagic SmallTV smart clock running the ESP8266 firmware. The device has a 240×240px ST7789 display and accepts JSON payloads describing widgets to draw.

## When to Use

- User asks to "show X on the smart clock", "display dashboard", "send to clock", or "visualize on smalltv"
- User wants to display metrics, charts, KPIs, QR codes on the physical device
- User provides data that should be visualized on the 240×240px screen

## How It Works

1. Read the device IP and credentials from environment or config
2. Construct a JSON payload with appropriate widgets based on user's data
3. Send POST request to `http://{DEVICE_IP}/api/draw` with HTTP Basic Auth
4. Return success/error status to user

## Device Configuration

The skill reads configuration from `.claude/settings.local.json` (preferred) or `.claude/settings.json` (template):

```json
{
  "smartclock": {
    "ip": "192.168.1.100",
    "user": "admin",
    "pass": "yourpassword"
  }
}
```

**Reading priority**:
1. Read `.claude/settings.local.json` first (user's actual device config, not committed to git)
2. If not found, read `.claude/settings.json` (template with example values)
3. If not found, check environment variables: `$SMARTCLOCK_IP`, `$SMARTCLOCK_USER`, `$SMARTCLOCK_PASS`
4. If none found, ask user for device IP and credentials

**Setup instructions for user**:
- Copy `.claude/settings.json` to `.claude/settings.local.json`
- Edit `.claude/settings.local.json` with actual device IP and password
- File is already in `.gitignore` so credentials won't be committed

## Supported Widget Types

### Text & Titles
- `title` — Header at top (default y=6, size=2)
- `text` — Plain text anywhere (max 48 chars)

### Metrics
- `kpi` — Large number card with label (supports threshold coloring)
- `gauge` — Horizontal progress bar (with threshold)

### Charts
- `bar` — Horizontal bars (comparative ranking)
- `column` — Vertical bars (time series)
- `line` — Line chart (trend over time, can fill area)
- `sparkline` — Minimal line (no axis by default)
- `candles` — Candlestick chart (OHLC data)
- `donut` / `pie` — Circular proportion chart

### Special
- `qr` — QR code (text or PromptPay payment)

### Layout
- `rect` — Filled/outlined rectangle
- `hline` — Horizontal divider line

## JSON Payload Format

```json
{
  "mode": "dashboard",
  "widgets": [
    { "type": "title", "text": "Dashboard Title", "color": "orange" },
    { "type": "kpi", "label": "METRIC", "value": 123.45, "decimals": 2,
      "x": 5, "y": 40, "w": 230, "h": 60, "size": 3, "color": "cyan" },
    { "type": "line", "data": [10, 20, 15, 25, 30],
      "x": 5, "y": 110, "w": 230, "h": 100, "color": "green", "fill": true }
  ]
}
```

## Common Parameters

- **Position & Size**: `x`, `y`, `w`, `h` (pixels, screen is 240×240)
- **Colors**: Named colors (`red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `orange`, `purple`, `pink`, `white`, `black`, `grey`) or hex (`#RRGGBB`, `#RGB565`)
- **Styling**: `color` (main), `color2` (secondary/threshold), `axis` (draw border), `fill` (fill area)
- **Data**: `value` (single number), `data` (array), `text` (string)
- **Formatting**: `decimals` (0-4), `size` (text scale 1-6), `label` (up to 16 chars)
- **Thresholds**: `threshold` (value), `min`, `max` (scale bounds)

## Layout Guidelines for 240×240px Screen

- **Title**: `y: 6` (top header)
- **Content area**: `y: 30-200` (main widgets)
- **Footer**: `y: 200-230` (status/totals)
- **Margins**: Use `x: 5`, leave ~5px from edges
- **Default widget**: `x: 5, y: 40, w: 230, h: 140`

## Color Scheme Best Practices

- **Titles**: `orange` (brand accent)
- **Positive metrics**: `green`, `cyan`
- **Warnings**: `yellow`
- **Errors/alerts**: `red`
- **Neutral**: `white`, `grey`
- **Background elements**: `darkgrey`, low-opacity colors

## Widget Design Patterns

### KPI Grid (2 columns)
```json
{ "type": "kpi", "label": "LEFT", "value": 99, "x": 5, "y": 40, "w": 112, "h": 60 },
{ "type": "kpi", "label": "RIGHT", "value": 42, "x": 123, "y": 40, "w": 112, "h": 60 }
```

### Chart + Summary
```json
{ "type": "title", "text": "Sales 7 Days", "color": "orange" },
{ "type": "column", "data": [420, 615, 580, 940, 1020, 760, 350],
  "x": 5, "y": 40, "w": 230, "h": 150, "color": "cyan", "threshold": 900, "color2": "orange" },
{ "type": "text", "text": "Total 4,685", "y": 200, "size": 2, "color": "white" }
```

### Sparkline + KPI
```json
{ "type": "kpi", "label": "LATENCY ms", "value": 148, "x": 5, "y": 38, "w": 230, "h": 60, "size": 5 },
{ "type": "sparkline", "data": [132, 140, 138, 151, 149, 160], "x": 5, "y": 104, "w": 230, "h": 40, "color": "cyan" }
```

### Status Dashboard
```json
{ "type": "title", "text": "System Status", "color": "orange" },
{ "type": "kpi", "label": "UPTIME %", "value": 99.8, "x": 5, "y": 34, "w": 112, "h": 52 },
{ "type": "kpi", "label": "ERRORS", "value": 12, "x": 123, "y": 34, "w": 112, "h": 52,
  "threshold": 10, "color2": "red" },
{ "type": "gauge", "value": 61, "min": 0, "max": 100, "x": 5, "y": 188, "w": 230, "h": 14,
  "threshold": 85, "color2": "red" }
```

### PromptPay QR
```json
{ "type": "title", "text": "PROMPTPAY", "color": "orange" },
{ "type": "qr", "promptpay_id": "0812345678", "promptpay_amount": 150.00,
  "x": 20, "y": 30, "w": 200, "h": 200 }
```

## Implementation Steps

1. **Get device credentials**:
   ```bash
   # Try reading from settings.local.json first
   cat .claude/settings.local.json
   
   # If not found, try settings.json (template)
   cat .claude/settings.json
   
   # If not found, check environment variables
   echo $SMARTCLOCK_IP $SMARTCLOCK_USER $SMARTCLOCK_PASS
   
   # If none found, ask user for device IP and credentials
   ```

2. **Analyze user's data** — determine appropriate widgets:
   - Single number → `kpi`
   - List of numbers → `line`, `column`, `bar`, `sparkline`
   - OHLC data → `candles`
   - Proportions → `donut` / `pie`
   - URL / PromptPay → `qr`

3. **Design layout** based on data:
   - 1 metric: Large KPI (size=5-6) centered
   - 2-4 metrics: KPI grid (2 columns)
   - Time series: Chart + summary text
   - Multi-metric: Title + KPI cards + gauge/chart

4. **Build JSON payload**:
   - Start with `title` widget
   - Add main content widgets
   - Add footer text/gauge if needed
   - Use appropriate colors (green=good, red=alert, orange=accent)

5. **Send POST request**:
   ```bash
   curl -u "${SMARTCLOCK_USER}:${SMARTCLOCK_PASS}" \
     -X POST "http://${SMARTCLOCK_IP}/api/draw" \
     -H "Content-Type: application/json" \
     -d '{...}'
   ```

6. **Handle response**:
   - 200 OK → Success, device switched to dashboard mode
   - 400 → JSON error, check payload format
   - 413 → Payload too large (max 8192 bytes)
   - 503 → Device heap low, retry in a few seconds

## Limits & Constraints

- **Max widgets**: 16 per frame
- **Max data points**: 255 per chart (shared pool of 1024 total)
- **Max body size**: 8192 bytes JSON
- **Text limits**: 48 chars (text/label), 160 chars (QR)
- **Screen resolution**: 240×240 pixels
- **Color depth**: RGB565 (16-bit)

## Error Handling

- If heap is low (503), suggest: "Device is busy, retrying in 3s..." then retry once
- If JSON too large (413), suggest: "Too much data, reducing data points..." then truncate arrays
- If auth fails (401), ask user for correct credentials
- If device unreachable, check: "Cannot reach device at {IP}. Is it powered on and connected to Wi-Fi?"

## Examples

### Example 1: Token Usage Dashboard
```json
{
  "mode": "dashboard",
  "widgets": [
    { "type": "title", "text": "AI Usage Today", "color": "orange" },
    { "type": "kpi", "label": "TOKENS", "text": "1.24M",
      "x": 5, "y": 40, "w": 110, "h": 62, "size": 3, "color": "cyan", "axis": true },
    { "type": "kpi", "label": "COST USD", "value": 18.42, "decimals": 2,
      "x": 125, "y": 40, "w": 110, "h": 62, "size": 2, "color": "yellow",
      "threshold": 15, "color2": "red", "axis": true },
    { "type": "kpi", "label": "REQUESTS", "value": 1284,
      "x": 5, "y": 112, "w": 230, "h": 70, "size": 5, "color": "green" },
    { "type": "gauge", "value": 72, "min": 0, "max": 100,
      "x": 5, "y": 194, "w": 230, "h": 16, "color": "green",
      "threshold": 80, "color2": "red" }
  ]
}
```

### Example 2: Temperature Trend
```json
{
  "mode": "dashboard",
  "widgets": [
    { "type": "title", "text": "Temperature 24h", "color": "orange" },
    { "type": "line", "data": [26.4, 26.1, 25.8, 25.5, 25.2, 25.0, 25.3, 26.2,
                                27.8, 29.4, 31.0, 32.3, 33.1, 33.6, 33.2, 32.4,
                                31.1, 29.8, 28.9, 28.2, 27.6, 27.1, 26.8, 26.5],
      "x": 5, "y": 40, "w": 230, "h": 150,
      "axis": true, "fill": true, "decimals": 1,
      "color": "green", "color2": "#062808" }
  ]
}
```

### Example 3: Sales Comparison (Bar Chart)
```json
{
  "mode": "dashboard",
  "widgets": [
    { "type": "title", "text": "Sales by Region", "color": "orange" },
    { "type": "bar", "data": [520, 410, 260, 150, 90],
      "x": 5, "y": 40, "w": 230, "h": 150,
      "values": true, "decimals": 0, "color": "cyan",
      "threshold": 400, "color2": "orange" },
    { "type": "text", "text": "Total 1,430", "y": 200, "size": 2, "color": "white" }
  ]
}
```

## Tips for AI

- **Auto-scale by default**: Don't send `min`/`max` unless user specifies exact range
- **Use thresholds wisely**: Add `threshold` + `color2` when there's a clear alert level
- **Prioritize readability**: On 240px screen, less is more — max 3-4 widgets per frame
- **Color semantics**: Green=good, Yellow=warning, Red=critical, Cyan=neutral, Orange=accent
- **Title always first**: Start widgets array with `title` for context
- **Test with examples**: If unsure, base layout on similar example from `examples/` directory

## Reference

Full API specification: `docs/API_DRAW_SPEC.md`
Example payloads: `examples/*.json`
Firmware source: `smart_clock_esp8266/smart_clock_esp8266.ino` (see `handleApiDraw()` function)
