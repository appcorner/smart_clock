#!/usr/bin/env python3
"""ตรวจ payload ใน examples/ กับข้อจำกัดจริงของ firmware ก่อนยิงขึ้นเครื่อง

    python scripts/validate_examples.py
    python scripts/validate_examples.py examples/04_donut.json

ตรวจสิ่งที่ firmware จะ "รับไว้เงียบๆ แล้ววาดเพี้ยน" ไม่ใช่แค่ JSON ถูกไวยากรณ์:
ข้อความตัน char[48], widget เกิน 10 ตัว, จุดเกิน pool, กราฟล้นขอบจอ,
ชนิดที่ parser ไม่รู้จัก (จะถูกข้ามทิ้งโดยไม่มี error), และแท่งที่บางกว่า 1px
ค่าคงที่ทุกตัวอ่านมาจาก smart_clock_esp8266.ino ตรงๆ
"""

import glob
import json
import sys

# ---- ข้อจำกัดจริงของ firmware v3.3.0 ----
SCREEN_W = 240
SCREEN_H = 240
DASH_TEXT_LEN = 48        # char text[48] และ char label[48]
DASH_MAX_WIDGETS = 10
DASH_MAX_POINTS = 40      # จุดต่อกราฟ
DASH_POOL_SIZE = 200      # คลัง float ที่ทุก widget ในเฟรมแชร์กัน
DASH_MAX_BODY = 6144
DASH_AXIS_GUTTER = 44     # ที่เว้นริมขวาเมื่อ axis=true
FONT_W = 8                # ฟอนต์ไทย advance 8 * scale ต่อ cell
FONT_H = 16

SERIES_TYPES = {"candles", "column", "bar", "line", "sparkline", "donut", "pie"}
VALUE_TYPES = {"kpi", "gauge"}
TEXT_TYPES = {"text", "title"}
PRIMITIVES = {"rect", "hline"}
QR_TYPES = {"qr"}
KNOWN = SERIES_TYPES | VALUE_TYPES | TEXT_TYPES | PRIMITIVES | QR_TYPES

QR_TEXT_LEN = 160  # char qrText[160] — บัฟเฟอร์แยกจาก text[48], ไว้สำหรับ payload/PromptPay

# ตรงกับ isThaiCombiningMark() — วรรณยุกต์ merge เข้า cell เดิม ไม่กิน advance
_COMBINING = {0x0E31} | set(range(0x0E34, 0x0E3A + 1)) | set(range(0x0E47, 0x0E4E + 1))


def text_cells(s):
    return sum(1 for ch in s if ord(ch) not in _COMBINING)


def check(path):
    """คืน (errors, warnings, สรุปสั้น)"""
    errs, warns = [], []

    raw = open(path, encoding="utf-8").read()
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as e:
        return [f"JSON ไม่ผ่าน: {e}"], [], ""

    # ต้องตรวจไบต์ดิบ ไม่ใช่ minified — show_examples.sh ยิงด้วย --data-binary "@file"
    # คือส่งไฟล์ทั้งก้อนรวมช่องว่างและขึ้นบรรทัด เครื่องเทียบ DASH_MAX_BODY กับตัวนี้
    nbytes = len(raw.encode("utf-8"))
    minified = json.dumps(doc, ensure_ascii=False, separators=(",", ":"))
    nmin = len(minified.encode("utf-8"))
    if nbytes > DASH_MAX_BODY:
        errs.append(
            f"ไฟล์ดิบ {nbytes} bytes เกิน DASH_MAX_BODY {DASH_MAX_BODY} "
            f"(minified เหลือ {nmin} — ถ้าจะยิงต้องบีบก่อน)")

    widgets = doc.get("widgets")
    if not isinstance(widgets, list) or not widgets:
        return errs + ["ไม่มี widgets array หรือว่างเปล่า"], warns, ""

    if len(widgets) > DASH_MAX_WIDGETS:
        errs.append(
            f"{len(widgets)} widget เกิน DASH_MAX_WIDGETS {DASH_MAX_WIDGETS} "
            f"— ตัวที่เกินจะถูกตัดทิ้งเงียบๆ")

    pool = 0
    kinds = []

    for i, w in enumerate(widgets):
        tag = f"widget[{i}] {w.get('type', '?')}"
        t = w.get("type", "")
        kinds.append(t)

        if t not in KNOWN:
            errs.append(f"{tag}: parser ไม่รู้จักชนิดนี้ จะถูกข้ามทิ้งโดยไม่มี error")
            continue

        # ---- ข้อความ: text/label ตัน char[48] แล้ว strlcpy จะตัดกลาง UTF-8 ----
        # qr ไม่เข้าเงื่อนนี้ — text ของ qr ไปลง qrText[160] ไม่ใช่ text[48] (เช็คแยกด้านล่าง)
        for field in ("text", "label"):
            if t in QR_TYPES and field == "text":
                continue
            s = w.get(field)
            if not isinstance(s, str):
                continue
            n = len(s.encode("utf-8"))
            if n >= DASH_TEXT_LEN:
                errs.append(
                    f"{tag}: {field} {n} bytes ตัน char[{DASH_TEXT_LEN}] "
                    f"strlcpy จะตัดกลาง UTF-8 กลายเป็นขยะ: {s!r}")

        # ---- qr: ต้องมี text หรือ promptpay_id อย่างน้อยหนึ่งอย่าง, และ text ตัน qrText[160] ----
        if t in QR_TYPES:
            s = w.get("text")
            pp_id = w.get("promptpay_id")
            if not pp_id and not (isinstance(s, str) and s):
                errs.append(f"{tag}: ต้องมี text หรือ promptpay_id อย่างน้อยหนึ่งอย่าง — widget จะถูกข้ามทิ้ง")
            if isinstance(s, str):
                n = len(s.encode("utf-8"))
                if n >= QR_TEXT_LEN:
                    errs.append(
                        f"{tag}: text {n} bytes ตัน char[{QR_TEXT_LEN}] "
                        f"widget จะถูกข้ามทิ้ง (ไม่ตัดกลาง แต่ปฏิเสธทั้ง widget)")
            continue

        if t in TEXT_TYPES:
            scale = w.get("size", 2)
            x = w.get("x", 5)
            y = w.get("y", 6 if t == "title" else 190)
            end = x + text_cells(w.get("text", "")) * FONT_W * scale
            if end > SCREEN_W:
                errs.append(
                    f"{tag}: กว้างถึง {end}px เกินจอ {SCREEN_W}px "
                    f"firmware จะขึ้นบรรทัดใหม่ทับของข้างล่าง")
            if y + FONT_H * scale > SCREEN_H:
                errs.append(f"{tag}: ล่างสุด {y + FONT_H * scale}px เกินจอ {SCREEN_H}px")
            continue

        if t in PRIMITIVES:
            continue

        # ---- กราฟและ KPI/gauge: กรอบต้องอยู่ในจอ ----
        x = w.get("x", 5)
        y = w.get("y", 40)
        ww = w.get("w", 230)
        hh = w.get("h", 60 if t == "kpi" else 14 if t == "gauge" else 140)
        if x < 0 or y < 0:
            errs.append(f"{tag}: พิกัดติดลบ ({x},{y})")
        if x + ww > SCREEN_W:
            errs.append(f"{tag}: ขวาสุด {x + ww}px เกินจอ {SCREEN_W}px")
        if y + hh > SCREEN_H:
            errs.append(f"{tag}: ล่างสุด {y + hh}px เกินจอ {SCREEN_H}px")

        # ---- ชุดข้อมูล ----
        if t in SERIES_TYPES:
            data = w.get("data")
            if not isinstance(data, list) or not data:
                errs.append(f"{tag}: ไม่มี data หรือว่าง — widget จะถูกข้ามทิ้ง")
                continue

            per_point = 4 if t == "candles" else 1
            if t == "candles":
                for j, tup in enumerate(data):
                    if not isinstance(tup, list) or len(tup) < 4:
                        errs.append(f"{tag}: data[{j}] ไม่ใช่ [o,h,l,c] 4 ค่า")

            n = len(data)
            if n > DASH_MAX_POINTS:
                warns.append(
                    f"{tag}: {n} จุด เกิน DASH_MAX_POINTS {DASH_MAX_POINTS} "
                    f"firmware จะเก็บ {DASH_MAX_POINTS} จุดท้ายสุด")
                n = DASH_MAX_POINTS
            pool += n * per_point

            # แท่งบางกว่า 1px อ่านไม่ออก — คิดจาก plotW จริงหลังหัก gutter
            if t in ("column", "candles", "bar"):
                axis = w.get("axis", True)
                if t == "bar":
                    span = hh
                    plot = ww - 40 if (w.get("values") and ww > 60) else ww
                    slot = span // n if n else 0
                else:
                    plot = ww - DASH_AXIS_GUTTER if (axis and ww > 60) else ww
                    slot = plot // n if n else 0
                if slot < 2:
                    warns.append(
                        f"{tag}: {n} แท่งใน {plot}px ได้ slot {slot}px "
                        f"ตัวแท่งจะเหลือ 1px แยกแท่งด้วยตาไม่ออก")

            if t in ("donut", "pie"):
                if any((v or 0) <= 0 for v in data):
                    warns.append(f"{tag}: มีค่า <= 0 firmware จะข้าม segment นั้น")
                if sum(v for v in data if v > 0) <= 0:
                    errs.append(f"{tag}: ยอดรวมเป็น 0 จะไม่วาดอะไรเลย")
                side = min(ww, hh)
                r_out = side // 2
                r_in = r_out * min(w.get("hole", 55), 95) // 100
                if r_out < 4:
                    errs.append(f"{tag}: รัศมี {r_out}px เล็กเกินกว่าจะวาด")
                if w.get("label") and r_in < 14:
                    warns.append(
                        f"{tag}: รูกลาง {r_in}px แคบกว่า 14px "
                        f"firmware จะไม่วาด label กลางวง")

        # ---- KPI / gauge ----
        if t == "kpi":
            has_val = "value" in w or isinstance(w.get("data"), list)
            if not w.get("text") and not has_val:
                errs.append(f"{tag}: ต้องมี text หรือ value อย่างน้อยหนึ่งอย่าง")
            if "value" in w:
                pool += 1
            # ตัวเลขกว้างเกินกรอบ firmware จะย่อ scale ลงเอง แต่เตือนไว้ให้รู้
            shown = w.get("text") or f"{w.get('value', 0):.{w.get('decimals', 0)}f}"
            size = w.get("size", 3)
            if len(shown) * 6 * size > ww - 4:
                warns.append(
                    f"{tag}: {shown!r} ที่ size {size} กว้างเกินกรอบ {ww}px "
                    f"firmware จะย่อ size ลงเองจนพอดี")
            lab = w.get("label", "")
            if lab and len(lab) * 6 > ww:
                warns.append(f"{tag}: label กว้าง {len(lab) * 6}px เกินกรอบ {ww}px")

        if t == "gauge":
            if "value" not in w:
                errs.append(f"{tag}: ไม่มี value — widget จะถูกข้ามทิ้ง")
            else:
                pool += 1
            if ("min" in w) != ("max" in w):
                warns.append(f"{tag}: ส่ง min/max มาไม่ครบคู่ firmware จะใช้ 0-100")

        # ---- min/max ต้องมาคู่กันถึงจะตรึงสเกล ----
        if t in SERIES_TYPES and ("min" in w) != ("max" in w):
            warns.append(
                f"{tag}: ส่ง min/max มาไม่ครบคู่ firmware จะ autoscale "
                f"(ต้องส่งทั้งสองตัวเพื่อตรึงสเกล)")

    if pool > DASH_POOL_SIZE:
        errs.append(
            f"ใช้คลังตัวเลข {pool} ช่อง เกิน DASH_POOL_SIZE {DASH_POOL_SIZE} "
            f"— จุดที่เกินจะหายไปเงียบๆ")

    summary = (f"{nbytes:5d}B raw ({nmin:4d} min)  widgets={len(widgets):2d}  "
               f"pool={pool:3d}/{DASH_POOL_SIZE}")
    return errs, warns, summary


def main():
    paths = sys.argv[1:] or sorted(glob.glob("examples/*.json"))
    if not paths:
        sys.exit("ไม่พบไฟล์ตัวอย่างใน examples/")

    bad = 0
    for p in paths:
        errs, warns, summary = check(p)
        status = "FAIL" if errs else ("warn" if warns else "ok")
        print(f"{p:34s} {summary}  [{status}]")
        for e in errs:
            print(f"    ERROR  {e}")
        for w in warns:
            print(f"    warn   {w}")
        if errs:
            bad += 1

    print()
    if bad:
        sys.exit(f"{bad}/{len(paths)} ไฟล์มีปัญหา")
    print(f"ผ่านทั้ง {len(paths)} ไฟล์")


if __name__ == "__main__":
    main()
