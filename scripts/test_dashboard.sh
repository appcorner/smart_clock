#!/usr/bin/env bash
# ทดสอบ /api/draw บนเครื่องจริง โดยไม่ต้องตั้ง Home Assistant
#
#   ./scripts/test_dashboard.sh 192.168.1.50
#   ./scripts/test_dashboard.sh 192.168.1.50 admin mypassword
#
# ยิง candlestick 24 แท่ง + หัวเรื่อง + ราคาไทย แล้วสลับกลับหน้านาฬิกาให้ดู
set -euo pipefail

HOST="${1:-}"
USER="${2:-admin}"
PASS="${3:-smartclock}"

if [ -z "$HOST" ]; then
    echo "usage: $0 <device-ip> [web-user] [web-pass]" >&2
    exit 1
fi

AUTH=(--user "$USER:$PASS" --silent --show-error --fail-with-body)

# ข้อมูลนิ่ง (ไม่สุ่ม) เพื่อให้เทียบผลซ้ำได้ทุกครั้ง — [open, high, low, close]
read -r -d '' PAYLOAD <<'JSON' || true
{
  "mode": "dashboard",
  "widgets": [
    {"type": "title", "text": "ทองคำ XAU/USD", "color": "orange"},
    {"type": "candles", "x": 5, "y": 40, "w": 230, "h": 140,
     "data": [
       [2610,2618,2604,2615], [2615,2622,2611,2612], [2612,2620,2606,2618],
       [2618,2631,2616,2629], [2629,2634,2621,2624], [2624,2628,2612,2615],
       [2615,2619,2601,2605], [2605,2612,2599,2610], [2610,2625,2608,2623],
       [2623,2640,2621,2638], [2638,2645,2632,2635], [2635,2641,2626,2630],
       [2630,2636,2618,2621], [2621,2628,2615,2626], [2626,2643,2624,2641],
       [2641,2656,2639,2653], [2653,2661,2645,2648], [2648,2652,2634,2637],
       [2637,2644,2630,2642], [2642,2658,2640,2655], [2655,2668,2652,2666],
       [2666,2672,2658,2661], [2661,2665,2649,2652], [2652,2670,2650,2668]
     ]},
    {"type": "text", "text": "$2,668.00", "y": 195, "size": 2, "color": "green"}
  ]
}
JSON

echo "==> POST /api/draw ($(printf '%s' "$PAYLOAD" | wc -c) bytes)"
curl "${AUTH[@]}" -X POST "http://$HOST/api/draw" \
    -H 'Content-Type: application/json' \
    --data-raw "$PAYLOAD"
echo

echo "==> heap ที่เหลือหลังวาด"
curl "${AUTH[@]}" "http://$HOST/config" | tr ',' '\n' | grep -i heap || true

echo "==> ดู dashboard 10 วินาที แล้วจะสลับกลับหน้านาฬิกา"
sleep 10

echo "==> GET /api/mode?to=clock"
curl "${AUTH[@]}" "http://$HOST/api/mode?to=clock"
echo

echo "==> GET /api/mode?to=dashboard (วาด frame เดิมซ้ำจาก RAM)"
curl "${AUTH[@]}" "http://$HOST/api/mode?to=dashboard"
echo
echo "เสร็จ — frame ยังอยู่ใน RAM จนกว่าจะครบ TTL 10 นาที หรือรีบูต"
