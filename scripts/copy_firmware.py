"""
Post-build hook: copy firmware.bin ไปไว้ที่ build_esp8266/ ตามที่ README อ้างถึง

ตั้งชื่อไฟล์จากค่า FW_VERSION ในซอร์ส เพื่อให้ชื่อไบนารีตรงกับเวอร์ชันจริงเสมอ
"""

import re
import shutil
from pathlib import Path

Import("env")  # noqa: F821  (ตัวแปรที่ PlatformIO ฉีดเข้ามาให้)

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
OUT_DIR = PROJECT_DIR / "build_esp8266"
SKETCH = PROJECT_DIR / "smart_clock_esp8266" / "smart_clock_esp8266.ino"


def detect_version() -> str:
    """อ่าน #define FW_VERSION "x.y.z" จากซอร์ส ถ้าหาไม่เจอใช้ unknown"""
    try:
        text = SKETCH.read_text(encoding="utf-8")
    except OSError:
        return "unknown"
    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', text)
    return m.group(1) if m else "unknown"


def after_build(source, target, env):
    firmware = Path(str(target[0]))
    if not firmware.exists():
        print("[copy_firmware] ไม่พบ %s ข้ามขั้นตอน copy" % firmware)
        return

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    dest = OUT_DIR / ("SDP_v%s.bin" % detect_version())
    shutil.copyfile(firmware, dest)

    size_kb = dest.stat().st_size / 1024
    print("[copy_firmware] -> %s (%.1f KB)" % (dest, size_kb))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)  # noqa: F821
