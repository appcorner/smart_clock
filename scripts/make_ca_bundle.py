"""
สร้าง CA bundle ที่รวม root CA จาก Windows certificate store เข้ากับ certifi

จำเป็นเมื่อเครื่องอยู่หลัง TLS inspection (เช่น Norton Web/Mail Shield, Zscaler)
เพราะไลบรารี requests ที่ PlatformIO ใช้เชื่อแค่ certifi ไม่ได้อ่าน Windows store
ทำให้ pio ดาวน์โหลด platform ไม่ได้ด้วย CERTIFICATE_VERIFY_FAILED

วิธีใช้:
    python scripts/make_ca_bundle.py
แล้วชี้ REQUESTS_CA_BUNDLE ไปที่ไฟล์ผลลัพธ์ตามที่สคริปต์บอก
"""

import ssl
import sys
from pathlib import Path

import certifi

OUT = Path.home() / ".platformio" / "win-ca-bundle.pem"


def windows_root_pems():
    """คืน PEM ของทุกใบใน Windows store ROOT และ CA (ไม่ซ้ำ)"""
    seen = set()
    pems = []
    for store in ("ROOT", "CA"):
        try:
            entries = ssl.enum_certificates(store)
        except (OSError, AttributeError) as exc:
            print("  ข้าม store %s: %s" % (store, exc))
            continue
        for der, _enc, _trust in entries:
            if der in seen:
                continue
            seen.add(der)
            pems.append(ssl.DER_cert_to_PEM_cert(der))
    return pems


def main():
    if not hasattr(ssl, "enum_certificates"):
        print("สคริปต์นี้ใช้ได้บน Windows เท่านั้น")
        return 1

    base = Path(certifi.where()).read_text(encoding="utf-8")
    extra = windows_root_pems()

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(base + "\n" + "".join(extra), encoding="utf-8")

    print("สร้าง bundle แล้ว: %s" % OUT)
    print("  certifi %d bytes + %d ใบจาก Windows store" % (len(base), len(extra)))
    print()
    print("ตั้ง environment variable ก่อนรัน pio (bash):")
    print('  export REQUESTS_CA_BUNDLE="%s"' % OUT.as_posix())
    print("PowerShell:")
    print('  $env:REQUESTS_CA_BUNDLE = "%s"' % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
