"""Pinned build-copy compatibility; never edit the installed ESPHome package."""
from hashlib import sha256
from pathlib import Path
import re

EXPECTED = '1db316fe1babffc4f0800c9137236e0170c048fa68d96531e9020ab4f25925ea'
PATCHED = '11158f4b3e85ce3c0a026b5491e4b9ea3f180ec84aae55bd4a91950276c24876'
ORIGINAL = '  void set_scan_continuous(bool scan_continuous) { scan_continuous_ = scan_continuous; }'
REPLACEMENT = '  // Norman generated-build compatibility: wake native loop on policy changes.\n#define NORMAN_RF_TRACKER_POLICY_WAKEUP 1\n  void set_scan_continuous(bool scan_continuous) {\n    if (this->scan_continuous_ != scan_continuous) {\n      this->scan_continuous_ = scan_continuous;\n      ++this->state_version_;\n    }\n  }'


def patch_project(project):
    root = Path(project).resolve()
    version = (root / "src/esphome/core/version.h").read_text()
    if not re.search(r'^#define ESPHOME_VERSION "2026\.4\.1"$', version, re.M):
        raise RuntimeError("Norman BLE compatibility requires exactly ESPHome 2026.4.1")
    path = root / "src/esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
    text = path.read_text()
    digest = sha256(text.encode()).hexdigest()
    if digest == PATCHED:
        return False
    if digest != EXPECTED or text.count(ORIGINAL) != 1:
        raise RuntimeError("Unexpected ESPHome tracker source; refusing compatibility patch")
    path.write_text(text.replace(ORIGINAL, REPLACEMENT))
    return True


# PlatformIO supplies Import/env only when executing this registered pre-script.
if "Import" in globals():
    globals()["Import"]("env")
    patch_project(globals()["env"].subst("$PROJECT_DIR"))
