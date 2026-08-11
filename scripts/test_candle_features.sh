#!/bin/bash
# Test script for candle last-price line and scale reference features

if [ $# -lt 3 ]; then
    echo "Usage: $0 <device-ip> <username> <password>"
    echo "Example: $0 192.168.1.50 admin smartclock"
    exit 1
fi

DEVICE_IP=$1
USERNAME=$2
PASSWORD=$3
BASE_URL="http://${DEVICE_IP}"

echo "=== Test 1: Candle chart with automatic last-price line ==="
echo "Expected: Yellow horizontal line at Y-coordinate matching close price 2618"
curl --user "${USERNAME}:${PASSWORD}" -X POST "${BASE_URL}/api/draw" \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {
        "type": "title",
        "text": "Gold XAU/USD"
      },
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
  }'
echo -e "\n"
sleep 3

echo "=== Test 2: Scale-referenced hline ==="
echo "Expected: Red horizontal line at Y-coordinate where price 2615 appears on candle scale"
curl --user "${USERNAME}:${PASSWORD}" -X POST "${BASE_URL}/api/draw" \
  -H 'Content-Type: application/json' \
  --data-raw '{
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
  }'
echo -e "\n"
sleep 3

echo "=== Test 3: Multiple reference lines ==="
echo "Expected: Yellow last-price line + red line at 2615 + cyan line at 2610"
curl --user "${USERNAME}:${PASSWORD}" -X POST "${BASE_URL}/api/draw" \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {
        "type": "title",
        "text": "ทอง + เส้นอ้างอิง"
      },
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
  }'
echo -e "\n"
sleep 3

echo "=== Test 4: Backward compatibility - absolute positioning ==="
echo "Expected: Cyan line at pixel Y=100 (no scale reference)"
curl --user "${USERNAME}:${PASSWORD}" -X POST "${BASE_URL}/api/draw" \
  -H 'Content-Type: application/json' \
  --data-raw '{
    "widgets": [
      {
        "type": "hline",
        "x": 10, "y": 100, "w": 220,
        "color": "cyan"
      },
      {
        "type": "text",
        "text": "พิกัด Y=100",
        "x": 20, "y": 110,
        "size": 1,
        "color": "white"
      }
    ]
  }'
echo -e "\n"

echo "=== All tests sent. Check device display for visual confirmation. ==="
echo "To return to clock mode:"
echo "  curl --user ${USERNAME}:${PASSWORD} '${BASE_URL}/api/mode?to=clock'"
