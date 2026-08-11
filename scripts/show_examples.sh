#!/usr/bin/env bash
# ยิงตัวอย่าง dashboard ใน examples/ ขึ้นเครื่องจริงทีละไฟล์ เว้นช่วงให้ดูด้วยตา
#
#   ./scripts/show_examples.sh 192.168.1.139 admin 'PASSWORD'
#   ./scripts/show_examples.sh 192.168.1.139 admin 'PASSWORD' 8   # เว้น 8 วินาที
#   ./scripts/show_examples.sh 192.168.1.139 admin 'PASSWORD' 0 examples/04_donut.json
#
# ทุกรอบพิมพ์ heap ที่เหลือหลังวาด เพื่อจับ widget ที่กินหน่วยความจำผิดปกติ
set -euo pipefail

HOST="${1:-}"
USER="${2:-admin}"
PASS="${3:-smartclock}"
DWELL="${4:-6}"
# shift 4 ตรงๆ ใช้ไม่ได้ — ถ้าส่งมาน้อยกว่า 4 ตัว bash จะไม่ shift อะไรเลยแล้วคืน error
# ทำให้ IP/user/pass ตกไปอยู่ใน FILES แล้วสคริปต์เอา IP ไปอ่านเป็นชื่อไฟล์
shift $(( $# < 4 ? $# : 4 ))
FILES=("$@")

if [ -z "$HOST" ]; then
    echo "usage: $0 <device-ip> [user] [pass] [dwell-seconds] [files...]" >&2
    exit 1
fi

if [ ${#FILES[@]} -eq 0 ]; then
    # อ้างจากที่ตั้งของสคริปต์ ไม่ใช่ cwd — เรียกจากโฟลเดอร์ไหนก็ได้ผลเดิม
    EX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/examples"
    # glob เรียงตามพจนานุกรมให้อยู่แล้ว ชื่อไฟล์จึงนำหน้าด้วยเลขเพื่อคุมลำดับที่ฉาย
    FILES=("$EX_DIR"/*.json)
    # glob ที่ไม่ match จะคืน pattern ตัวเองมาเป็นสตริง ต้องเช็คว่ามีไฟล์จริง
    if [ ! -e "${FILES[0]}" ]; then
        echo "ไม่พบไฟล์ตัวอย่างใน $EX_DIR" >&2
        exit 1
    fi
fi

AUTH=(--user "$USER:$PASS" --silent --show-error --fail-with-body)

# heap ที่ /config รายงาน — ใช้จับว่า frame ไหนทำ heap ตก
heap_now() {
    curl "${AUTH[@]}" "http://$HOST/config" 2>/dev/null \
        | tr ',' '\n' | grep -i heap | tr -d ' "' || echo "heap=?"
}

fail=0
for f in "${FILES[@]}"; do
    bytes=$(wc -c < "$f" | tr -d ' ')
    printf '==> %-28s %5s bytes  ' "$(basename "$f")" "$bytes"

    if resp=$(curl "${AUTH[@]}" -X POST "http://$HOST/api/draw" \
                -H 'Content-Type: application/json' \
                --data-binary "@$f" 2>&1); then
        printf '%s  %s\n' "$resp" "$(heap_now)"
    else
        printf 'FAILED: %s\n' "$resp"
        fail=$((fail + 1))
    fi

    # ต้องเป็น if ไม่ใช่ `[ ... ] && sleep` — ตัวหลังคืนค่าเท็จเมื่อ DWELL=0
    # แล้ว set -e จะฆ่าสคริปต์ทิ้งทั้งที่ยังยิงไม่ครบ
    if [ "$DWELL" != "0" ]; then
        sleep "$DWELL"
    fi
done

echo
if [ "$fail" -gt 0 ]; then
    echo "มี $fail ไฟล์ที่เครื่องไม่รับ" >&2
    exit 1
fi
echo "ยิงครบ ${#FILES[@]} ไฟล์ เครื่องรับทุกไฟล์"
# echo "frame สุดท้ายยังอยู่ใน RAM จนครบ TTL 10 นาที — /api/mode?to=clock เพื่อกลับหน้านาฬิกา"

sleep "$DWELL"
echo "ยิง /api/mode?to=clock เพื่อกลับหน้านาฬิกา"
echo
curl "${AUTH[@]}" "http://$HOST/api/mode?to=clock"
