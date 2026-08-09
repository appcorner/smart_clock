#!/usr/bin/env python3
"""ส่ง dashboard รายงานการใช้ AI token ขึ้นจอ SmartClock ผ่าน POST /api/draw

อ่าน transcript ของ Claude Code ที่ ~/.claude/projects/*/*.jsonl รวมยอด token ต่อวัน
แล้วยิงเป็น JSON widget ให้เครื่องวาดเอง (ไม่ส่งภาพ ไม่เขียนแฟลช ไม่ต้องแฟลช firmware ใหม่)

    python scripts/ai_tokens_dashboard.py --dry-run             # ดู payload ก่อนยิง
    python scripts/ai_tokens_dashboard.py 192.168.1.139 admin 'PASSWORD'
    python scripts/ai_tokens_dashboard.py --metric total ...    # เปลี่ยนตัวชี้วัด

ตั้งแต่ firmware v3.3.0 มี widget `column` (แท่งตั้ง) ในตัวแล้ว จึงส่งตัวเลขดิบ
ไม่ต้องยืม `candles` แล้วหลอกด้วย OHLC [0,v,0,v] เหมือนเวอร์ชันก่อน
เครื่อง autoscale เอง และตรึงฐานที่ 0 ให้อัตโนมัติเมื่อค่าเป็นบวกล้วน
"""

import argparse
import base64
import glob
import json
import os
import sys
import urllib.error
import urllib.request
from collections import defaultdict
from datetime import datetime, timedelta

# ---- ข้อจำกัดจริงของ firmware v3.3.0 (อ่านจาก smart_clock_esp8266.ino) ----
SCREEN_W = 240
SCREEN_H = 240
DASH_TEXT_LEN = 48       # char text[48] — strlcpy จะตัดกลาง UTF-8 ถ้าเกิน (ไทย 3 ไบต์/ตัว)
DASH_MAX_WIDGETS = 10    # widget รวมทุกชนิดต่อ frame (เดิมนับ text แยก 4 ตัว)
DASH_MAX_POINTS = 40     # จุดต่อกราฟหนึ่งตัว
DASH_MAX_BODY = 6144
FONT_W = 8               # THAIFONT_WIDTH — advance = 8 * scale ต่อ 1 cell
FONT_H = 16              # THAIFONT_HEIGHT

# ---- layout จอ 240x240: title / กราฟ / สองบรรทัดสรุป ----
# ทุกบล็อกคำนวณให้ไม่ทับกัน สูงของข้อความ scale 2 = 32px
TITLE_Y = 2
CHART = {"x": 5, "y": 38, "w": 230, "h": 110}   # 38 + 110 = 148
LINE1_Y = 152                                    # 152 + 32 = 184
LINE2_Y = 188                                    # 188 + 32 = 220 < 240
DAYS = 14        # plotW = 230 - 44 = 186px ÷ 14 = แท่งละ 13px (ตัวแท่ง 12px)
# column ตรึงฐาน 0 ให้เองเมื่อค่าเป็นบวกล้วน จึงไม่ต้องส่ง min/max มาตรึงสเกล
# ปล่อย autoscale เพื่อให้วันที่ยอดต่ำยังเห็นความต่างของแท่ง

TRANSCRIPTS = os.path.expanduser("~/.claude/projects/*/*.jsonl")

METRICS = {
    # ตัดโทเคนราคาถูกออก เหลือส่วนที่คิดเงินเต็มราคา — ใกล้ค่าใช้จ่ายจริงที่สุด
    "billable": ("จ่าย", ("input_tokens", "output_tokens",
                          "cache_creation_input_tokens")),
    # ทุกโทเคนที่ไหลผ่าน model รวม cache read — ตัวเลขใหญ่สุด cache read ครองยอด
    "total": ("รวม", ("input_tokens", "output_tokens",
                      "cache_creation_input_tokens", "cache_read_input_tokens")),
    # เฉพาะที่ model เขียนออกมา — สะท้อนปริมาณงานที่สั่งจริง
    "output": ("ออก", ("output_tokens",)),
}

# วรรณยุกต์/สระที่ firmware merge เข้า cell เดิม ไม่กิน advance
# ตรงกับ isThaiCombiningMark() ใน .ino
_COMBINING = (
    {0x0E31}
    | set(range(0x0E34, 0x0E3A + 1))
    | set(range(0x0E47, 0x0E4E + 1))
)


def text_cells(s):
    """จำนวน cell ที่ firmware จะวาด — นับ codepoint ที่ไม่ใช่วรรณยุกต์"""
    return sum(1 for ch in s if ord(ch) not in _COMBINING)


def check_text(s, scale, x):
    """คืน list ของปัญหา ถ้าข้อความจะเพี้ยนบนเครื่อง"""
    problems = []
    n = len(s.encode("utf-8"))
    if n >= DASH_TEXT_LEN:
        problems.append(
            f"{n} bytes ตัน char[{DASH_TEXT_LEN}] จะถูก strlcpy ตัดกลาง UTF-8 → กลายเป็นขยะ: {s!r}")
    end = x + text_cells(s) * FONT_W * scale
    if end > SCREEN_W:
        problems.append(f"กว้างถึง {end}px เกินจอ {SCREEN_W}px จะตัดบรรทัดเองแล้วทับของข้างล่าง: {s!r}")
    return problems


def collect(metric_fields):
    """คืน (ยอดต่อวัน, จำนวน request ต่อวัน) นับตาม local date"""
    per_day = defaultdict(int)
    reqs = defaultdict(int)
    seen = set()
    files = sorted(glob.glob(TRANSCRIPTS))
    if not files:
        sys.exit(f"ไม่พบ transcript ที่ {TRANSCRIPTS}")

    for path in files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                msg = rec.get("message")
                if not isinstance(msg, dict):
                    continue
                usage = msg.get("usage")
                if not isinstance(usage, dict):
                    continue
                # transcript ซ้ำได้จาก resume/compact — กันนับซ้ำด้วย message.id
                key = msg.get("id") or rec.get("uuid")
                if key:
                    if key in seen:
                        continue
                    seen.add(key)
                ts = rec.get("timestamp")
                if not ts:
                    continue
                # timestamp เป็น UTC ต้องแปลงเป็นเวลาเครื่องก่อนตัดวัน
                when = datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone()
                day = when.date().isoformat()
                per_day[day] += sum(int(usage.get(f) or 0) for f in metric_fields)
                reqs[day] += 1
    return per_day, reqs


def pick_unit(peak):
    """เลือกหน่วยให้ป้ายแกนขวาอ่านออก — firmware พิมพ์ด้วย %.0f ทศนิยมหายหมด
    ค่าสูงสุดต้องกลายเป็นจำนวนเต็มอย่างน้อย 2 หลัก ไม่ใช่ 0"""
    if peak >= 10_000_000:
        return 1_000_000, "M"
    if peak >= 10_000:
        return 1_000, "K"
    return 1, ""


def fmt(n):
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M"
    if n >= 1_000:
        return f"{n / 1_000:.0f}K"
    return str(n)


def build_payload(per_day, reqs, label, days=DAYS):
    today = datetime.now().date()
    window = [(today - timedelta(days=i)).isoformat() for i in range(days - 1, -1, -1)]
    values = [per_day.get(d, 0) for d in window]

    div, unit = pick_unit(max(values) if values else 0)
    # ปัดเป็นจำนวนเต็มในหน่วยที่เลือก เพราะป้ายแกนก็ปัดทิ้งอยู่แล้ว
    bars = [round(v / div, 1) for v in values]

    day_total = values[-1]
    week_total = sum(values[-7:])

    suffix = f" ({unit})" if unit else ""
    widgets = [
        # ใส่ชื่อ metric ไว้บนจอด้วย ไม่งั้นดูแล้วแยกไม่ออกว่ากำลังดู billable หรือ total
        {"type": "title", "text": f"AI {label}{suffix}",
         "x": 5, "y": TITLE_Y, "size": 2, "color": "orange"},
        # decimals=1 เพราะค่าถูกหารเป็นหน่วย K/M แล้ว ปัดเป็นจำนวนเต็มจะเสียความละเอียด
        {"type": "column", **CHART, "data": bars,
         "color": "green", "decimals": 1},
        {"type": "text", "text": f"วันนี้ {fmt(day_total)}",
         "x": 5, "y": LINE1_Y, "size": 2, "color": "green"},
        {"type": "text", "text": f"7วัน {fmt(week_total)}",
         "x": 5, "y": LINE2_Y, "size": 2, "color": "cyan"},
    ]

    # ตรวจข้อความทุกตัวกับข้อจำกัดจริงของเครื่อง ก่อนยิงออกไป
    problems = []
    for w in widgets:
        if w["type"] in ("text", "title"):
            problems += check_text(w["text"], w["size"], w["x"])
    if problems:
        sys.exit("ข้อความเกินข้อจำกัด firmware:\n  - " + "\n  - ".join(problems))

    # v3.3.0 นับ widget รวมทุกชนิดในเพดานเดียว ไม่แยกโควตาข้อความออกมาแล้ว
    if len(widgets) > DASH_MAX_WIDGETS:
        sys.exit(f"widget {len(widgets)} ตัว เกิน DASH_MAX_WIDGETS {DASH_MAX_WIDGETS}")
    if len(bars) > DASH_MAX_POINTS:
        sys.exit(f"แท่ง {len(bars)} เกิน DASH_MAX_POINTS {DASH_MAX_POINTS}")

    return {"mode": "dashboard", "widgets": widgets}, window, values, unit


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", help="IP ของเครื่อง เช่น 192.168.1.139")
    ap.add_argument("user", nargs="?", default="admin")
    ap.add_argument("password", nargs="?")
    ap.add_argument("--metric", choices=METRICS, default="billable")
    ap.add_argument("--days", type=int, default=DAYS,
                    help=f"จำนวนวันย้อนหลัง 1-{DASH_MAX_POINTS} (ค่าตั้งต้น {DAYS})")
    ap.add_argument("--dry-run", action="store_true", help="พิมพ์ payload แล้วจบ ไม่ยิงออก")
    args = ap.parse_args()

    if not 1 <= args.days <= DASH_MAX_POINTS:
        sys.exit(f"--days ต้องอยู่ในช่วง 1-{DASH_MAX_POINTS}")

    label, fields = METRICS[args.metric]
    per_day, reqs = collect(fields)
    payload, window, values, unit = build_payload(per_day, reqs, label, args.days)
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")

    print(f"metric={args.metric} วันที่มีข้อมูล={len(per_day)} "
          f"หน่วยแกน={unit or '1'} payload={len(body)} bytes", file=sys.stderr)
    for d, v in zip(window, values):
        if v:
            print(f"  {d}  {fmt(v):>8}  ({reqs.get(d, 0)} req)", file=sys.stderr)
    if len(body) > DASH_MAX_BODY:
        sys.exit(f"payload {len(body)} bytes เกินเพดาน DASH_MAX_BODY {DASH_MAX_BODY}")

    if args.dry_run or not args.host:
        print(json.dumps(payload, ensure_ascii=False, indent=1))
        return

    if not args.password:
        sys.exit("ต้องใส่ password ของหน้าเว็บเครื่อง")

    req = urllib.request.Request(
        f"http://{args.host}/api/draw", data=body, method="POST",
        headers={
            "Content-Type": "application/json",
            "Authorization": "Basic " + base64.b64encode(
                f"{args.user}:{args.password}".encode()).decode(),
        })
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            print(f"{resp.status} {resp.read().decode(errors='replace').strip()}")
    except urllib.error.HTTPError as e:
        sys.exit(f"HTTP {e.code}: {e.read().decode(errors='replace').strip()}")
    except urllib.error.URLError as e:
        sys.exit(f"ต่อไม่ติด: {e.reason}")


if __name__ == "__main__":
    main()
