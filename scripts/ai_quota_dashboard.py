#!/usr/bin/env python3
"""ส่ง dashboard โควต้าคงเหลือของผู้ช่วย AI ขึ้นจอ SmartClock ผ่าน POST /api/draw

อ่านจากที่แต่ละเครื่องมือเก็บไว้บนเครื่องนี้เท่านั้น ไม่ยิง API ไม่แตะ credential

    python scripts/ai_quota_dashboard.py --dry-run
    python scripts/ai_quota_dashboard.py 192.168.1.139 admin 'PASSWORD'

แหล่งข้อมูลจริงที่ตรวจแล้ว:
  codex        ~/.codex/sessions/**/*.jsonl → event token_count.rate_limits
               มี used_percent + window_minutes + resets_at ครบ ใช้ค่าที่ snapshot ใหม่สุด
  claude       ไม่เก็บโควต้าไว้เครื่อง (rateLimits ใน transcript เป็น null ทุกรายการ)
  antigravity  ~/.antigravity-agent/cloud_accounts.db มีคอลัมน์ quota_json
               แต่เข้ารหัสไว้ (iv:salt:ciphertext) เท่ากับ token_json — ไม่แกะ

ตัวไหนไม่มีข้อมูลจะแสดง n/a ไม่เดาค่าให้

ตั้งแต่ firmware v3.3.0 ใช้ widget `bar` (แท่งนอน) ตรงๆ เลิกยืม `candles`
แท่งนอนเหมาะกับงานนี้กว่าแท่งตั้ง เพราะแต่ละแท่งคือรายการ (codex 5h / codex 7d)
ไม่ใช่ลำดับเวลา และมีที่ริมขวาให้พิมพ์เปอร์เซ็นต์กำกับด้วย values=true
ตรึงสเกล min/max 0-100 เพราะเป็นเปอร์เซ็นต์ ปล่อย autoscale จะทำให้แท่ง 94%
กับ 100% สูงเท่ากันเมื่อเป็นค่าสูงสุดทั้งคู่
"""

import argparse
import base64
import glob
import json
import os
import sqlite3
import sys
import urllib.error
import urllib.request
from datetime import datetime

# ---- ข้อจำกัดจริงของ firmware v3.3.0 (อ่านจาก smart_clock_esp8266.ino) ----
SCREEN_W = 240
DASH_TEXT_LEN = 48       # char text[48] — strlcpy ตัดกลาง UTF-8 ถ้าเกิน (ไทย 3 ไบต์/ตัว)
DASH_MAX_WIDGETS = 10    # widget รวมทุกชนิดต่อ frame
DASH_MAX_POINTS = 40     # จุดต่อกราฟหนึ่งตัว
DASH_MAX_BODY = 6144
FONT_W = 8               # THAIFONT_WIDTH — advance = 8 * scale ต่อ 1 cell

# ---- layout จอ 240x240 ----
TITLE_Y = 2
CHART = {"x": 5, "y": 38, "w": 230, "h": 110}   # 38 + 110 = 148
LINE1_Y = 152                                    # + 32 = 184
LINE2_Y = 188                                    # + 32 = 220 < 240

CODEX_SESSIONS = os.path.expanduser("~/.codex/sessions/**/*.jsonl")
ANTIGRAVITY_DB = os.path.expanduser("~/.antigravity-agent/cloud_accounts.db")

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
    problems = []
    n = len(s.encode("utf-8"))
    if n >= DASH_TEXT_LEN:
        problems.append(
            f"{n} bytes ตัน char[{DASH_TEXT_LEN}] strlcpy จะตัดกลาง UTF-8 → ขยะ: {s!r}")
    end = x + text_cells(s) * FONT_W * scale
    if end > SCREEN_W:
        problems.append(
            f"กว้าง {end}px เกินจอ {SCREEN_W}px จะขึ้นบรรทัดใหม่ทับของข้างล่าง: {s!r}")
    return problems


# ---------------------------------------------------------------- codex

def _walk_rate_limits(obj):
    """เดินหา dict rate_limits ทุกชั้น เพราะ schema ห่อไม่เท่ากันในแต่ละเวอร์ชัน"""
    if isinstance(obj, dict):
        rl = obj.get("rate_limits")
        if isinstance(rl, dict):
            yield rl
        for v in obj.values():
            yield from _walk_rate_limits(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from _walk_rate_limits(v)


def read_codex():
    """คืน list ของ window ที่พบ เรียงตาม window_minutes
    เก็บเฉพาะ snapshot ใหม่สุดของแต่ละ (slot, window) เพราะไฟล์เก่ามีค่าค้าง"""
    best = {}
    for path in glob.glob(CODEX_SESSIONS, recursive=True):
        try:
            fh = open(path, encoding="utf-8", errors="replace")
        except OSError:
            continue
        with fh:
            for line in fh:
                # กรองด้วย substring ก่อน parse — session รวมกันหลายแสนบรรทัด
                if "rate_limits" not in line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                ts = rec.get("timestamp") or ""
                for rl in _walk_rate_limits(rec):
                    plan = rl.get("plan_type")
                    for slot in ("primary", "secondary"):
                        d = rl.get(slot)
                        if not isinstance(d, dict):
                            continue
                        if d.get("used_percent") is None:
                            continue
                        key = (slot, d.get("window_minutes"))
                        cur = best.get(key)
                        if cur is None or ts > cur["ts"]:
                            best[key] = {
                                "ts": ts,
                                "slot": slot,
                                "window_minutes": d.get("window_minutes"),
                                "used_percent": float(d["used_percent"]),
                                "resets_at": d.get("resets_at"),
                                "plan": plan,
                            }
    return sorted(best.values(), key=lambda w: w["window_minutes"] or 0)


def read_claude():
    """Claude Code ไม่ cache โควต้าไว้เครื่อง — ตรวจแล้วว่า rateLimits เป็น null ทุกครั้ง
    คืน None เพื่อให้จอแสดง n/a ตามความจริง"""
    return None


def read_antigravity():
    """quota_json มีอยู่จริงแต่เข้ารหัส — คืนสถานะว่าเจอ/ไม่เจอ ไม่พยายามถอด"""
    if not os.path.exists(ANTIGRAVITY_DB):
        return None
    try:
        con = sqlite3.connect(f"file:{ANTIGRAVITY_DB}?mode=ro", uri=True)
    except sqlite3.Error:
        return None
    try:
        rows = con.execute(
            "SELECT quota_json FROM accounts WHERE is_active = 1").fetchall()
    except sqlite3.Error:
        return None
    finally:
        con.close()
    for (blob,) in rows:
        if not blob:
            continue
        try:
            data = json.loads(blob)
        except (TypeError, json.JSONDecodeError):
            # รูปแบบ iv:salt:ciphertext — เข้ารหัสด้วยคีย์ใน keystore ของแอป
            return {"encrypted": True}
        if isinstance(data, dict):
            return {"encrypted": False, "raw": data}
    return None


# ---------------------------------------------------------------- payload

def human_reset(epoch):
    if not epoch:
        return "?"
    try:
        dt = datetime.fromtimestamp(int(epoch))
    except (OSError, OverflowError, ValueError):
        return "?"
    delta = dt - datetime.now()
    days = delta.days
    if days >= 1:
        return f"{days}d"
    hours = max(0, int(delta.total_seconds() // 3600))
    return f"{hours}h"


def win_label(minutes):
    hrs = (minutes or 0) / 60
    return f"{hrs / 24:.0f}d" if hrs >= 24 else f"{hrs:.0f}h"


def snapshot_age_hours(ts):
    """อายุของ snapshot เป็นชั่วโมง — Codex เขียนค่าตอนคุยเท่านั้น
    ไม่ได้อัปเดตเองตอนเราไม่ใช้ ต้องรู้ว่าเลขเก่าแค่ไหน"""
    if not ts:
        return None
    try:
        dt = datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone()
    except ValueError:
        return None
    return max(0.0, (datetime.now().astimezone() - dt).total_seconds() / 3600)


def is_fresh(w):
    """snapshot ที่เก่ากว่าความยาว window ของตัวเอง ถือว่าไร้ความหมาย
    เช่นหน้าต่าง 5 ชม. ที่บันทึกไว้เดือนก่อน reset ไปหลายรอบแล้ว"""
    age = snapshot_age_hours(w["ts"])
    if age is None:
        return False
    return age <= (w["window_minutes"] or 0) / 60


DANGER_PCT = 90     # ใช้ไปเกินนี้ถือว่าใกล้ตัน แท่งเป็นแดง


def used_color(pct):
    """สีข้อความตามระดับที่ใช้ไป — 94% ต้องไม่ขึ้นเขียว
    ข้อความมีส้มให้ใช้ ต่างจากแท่งที่ firmware มีแค่เขียว/แดง"""
    if pct >= DANGER_PCT:
        return "red"
    if pct >= 70:
        return "orange"
    return "green"


def clamp_pct(pct):
    """ตัดค่าให้อยู่ในช่วง 0-100 — เปอร์เซ็นต์ที่เกินนั้นไม่มีความหมาย

    ไม่ต้องแปลงเป็น OHLC แล้วตั้งแต่ v3.3.0: widget `bar` รับตัวเลขดิบ
    ตรึงสเกลด้วย min/max และคุมสีด้วย threshold ได้ตรงๆ ไม่ต้องหลอกด้วยทิศแท่ง
    """
    return round(max(0.0, min(100.0, pct)), 1)


def build_payload(codex_windows, claude, antigravity):
    """หนึ่งแท่งต่อหนึ่ง window ที่ยังสดจริง สูง = เปอร์เซ็นต์ที่ใช้ไป (0-100)
    แท่งกับตัวเลขบนจอต้องเป็นตัวเดียวกันคือ used_percent ไม่สลับไปโชว์ remaining
    แหล่งที่ไม่มีข้อมูลจะไม่ใส่แท่ง 0 เพราะแท่ง 0 อ่านว่า "ใช้ไป 0%" ซึ่งผิด"""
    fresh = [w for w in codex_windows if is_fresh(w)]
    labels = [f"codex {win_label(w['window_minutes'])}" for w in fresh]
    bars = [w["used_percent"] for w in fresh]

    if not fresh:
        # ไม่มีอะไรสดพอ — กรอบเปล่าสเกล 0-100 พร้อมข้อความบอกตรงๆ
        values = [0.0]
        line1 = "no fresh data"
        line2 = "codex/claude/antigrav n/a"
        color1 = "grey"
    else:
        values = [clamp_pct(v) for v in bars]
        worst = max(fresh, key=lambda w: w["used_percent"])
        used = worst["used_percent"]
        # scale 2 วาดได้ 14 cell ต่อบรรทัด — คุมความยาวไว้ใต้นั้น
        line1 = f"codex {used:.0f}% /{win_label(worst['window_minutes'])}"
        color1 = used_color(used)
        age = snapshot_age_hours(worst["ts"])
        age_txt = f"{age / 24:.0f}d" if age and age >= 24 else f"{age or 0:.0f}h"
        # scale 1 วาดได้ 29 cell — พอใส่ทั้ง reset อายุ snapshot และสถานะอีกสองตัว
        line2 = f"reset {human_reset(worst['resets_at'])} snap {age_txt} cl/ag n/a"

    widgets = [
        {"type": "title", "text": "โควต้า AI", "x": 5, "y": TITLE_Y,
         "size": 2, "color": "orange"},
        # แท่งนอน: หน้าต่างโควต้าเป็นรายการที่เอามาเทียบกัน ไม่ใช่ไทม์ไลน์
        # min/max ตรึง 0-100 เพราะเปอร์เซ็นต์ต้องอ่านเทียบเพดานเดียวกันทุกแท่ง
        # ถ้าปล่อย autoscale แท่ง 94% กับ 100% จะยาวเท่ากันเพราะเป็นค่าสูงสุดทั้งคู่
        {"type": "bar", **CHART, "data": values,
         "min": 0, "max": 100, "values": True,
         "color": "green", "color2": "red", "threshold": DANGER_PCT},
        {"type": "text", "text": line1, "x": 5, "y": LINE1_Y,
         "size": 2, "color": color1},
        {"type": "text", "text": line2, "x": 5, "y": LINE2_Y,
         "size": 1, "color": "grey"},
    ]

    problems = []
    for w in widgets:
        if w["type"] in ("text", "title"):
            problems += check_text(w["text"], w["size"], w["x"])
    if problems:
        sys.exit("ข้อความเกินข้อจำกัด firmware:\n  - " + "\n  - ".join(problems))

    if len(widgets) > DASH_MAX_WIDGETS:
        sys.exit(f"widget {len(widgets)} ตัว เกิน DASH_MAX_WIDGETS {DASH_MAX_WIDGETS}")
    if len(values) > DASH_MAX_POINTS:
        sys.exit(f"แท่ง {len(values)} เกิน DASH_MAX_POINTS {DASH_MAX_POINTS}")

    return {"mode": "dashboard", "widgets": widgets}, labels, bars


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", help="IP ของเครื่อง เช่น 192.168.1.139")
    ap.add_argument("user", nargs="?", default="admin")
    ap.add_argument("password", nargs="?")
    ap.add_argument("--dry-run", action="store_true",
                    help="พิมพ์ payload แล้วจบ ไม่ยิงออก")
    args = ap.parse_args()

    codex_windows = read_codex()
    claude = read_claude()
    antigravity = read_antigravity()

    print("=== แหล่งข้อมูลที่อ่านได้จริงบนเครื่องนี้ ===", file=sys.stderr)
    if codex_windows:
        for w in codex_windows:
            hrs = (w["window_minutes"] or 0) / 60
            print(f"  codex       {w['slot']:9} window={hrs:6.1f}h "
                  f"used={w['used_percent']:5.1f}% "
                  f"reset={human_reset(w['resets_at'])} "
                  f"plan={w['plan']} snapshot={w['ts']}", file=sys.stderr)
    else:
        print("  codex       ไม่พบ rate_limits ใน session", file=sys.stderr)
    print("  claude      ไม่เก็บโควต้าไว้เครื่อง (rateLimits = null)", file=sys.stderr)
    if antigravity is None:
        print("  antigravity ไม่พบ quota_json", file=sys.stderr)
    elif antigravity.get("encrypted"):
        print("  antigravity quota_json เข้ารหัสไว้ ต้องใช้คีย์จาก keystore — ไม่แกะ",
              file=sys.stderr)
    else:
        print(f"  antigravity อ่านได้: {antigravity['raw']}", file=sys.stderr)

    payload, labels, bars = build_payload(codex_windows, claude, antigravity)
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")

    print(f"=== แท่งบนจอ: {list(zip(labels, bars))} payload={len(body)} bytes",
          file=sys.stderr)
    if len(body) > DASH_MAX_BODY:
        sys.exit(f"payload {len(body)} bytes เกินเพดาน {DASH_MAX_BODY}")

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
