"""Exercise the pinned native tracker loop with the build-copy policy setter.

Peripheral BLE calls and client counts are fakes; the policy setter and entire
loop body are extracted from verified ESPHome source, not reimplemented here.
This checks scheduling contracts, not physical Bluetooth or radio performance.
"""

import argparse
from hashlib import sha256
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CPP_DIGEST = "d64010b05fa5d5f422a6271e532818b077d106ae0a12cf548905bec90a6de370"

PRELUDE = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define ESP_LOGD(...)
#define ESP_LOGW(...)
#define ESP_LOGE(...)
struct Application {
  unsigned now = 0, reboots = 0;
  unsigned get_loop_component_start_time() const { return now; }
  void reboot() { ++reboots; }
} App;
enum class ScannerState { IDLE, STARTING, RUNNING, STOPPING, FAILED };
enum class ScanTimeoutState { INACTIVE, MONITORING, EXCEEDED_WAIT };
struct ClientStateCounts {
  unsigned connecting = 0, disconnecting = 0, discovered = 0;
  bool operator!=(const ClientStateCounts &other) const {
    return connecting != other.connecting || disconnecting != other.disconnecting ||
           discovered != other.discovered;
  }
};
struct Parent { bool active = true; bool is_active() const { return active; } };
class ESP32BLETracker {
 public:
  Parent parent;
  Parent *parent_ = &parent;
  bool ble_was_disabled_ = false, scan_continuous_ = false;
  ScannerState scanner_state_ = ScannerState::IDLE;
  ScanTimeoutState scan_timeout_state_ = ScanTimeoutState::INACTIVE;
  unsigned scan_start_time_ = 0, scan_timeout_ms_ = 1000;
  unsigned scan_set_param_failed_ = 0;
  uint8_t state_version_ = 0, last_processed_version_ = 0;
  ClientStateCounts clients, client_state_counts_;
  unsigned starts = 0, count_reads = 0, promotions = 0, failure_handlers = 0;
  ClientStateCounts count_client_states_() { ++count_reads; return clients; }
  void start_scan_ (bool) { ++starts; scanner_state_ = ScannerState::STARTING; ++state_version_; }
  void start_scan() { start_scan_(true); }
  void try_promote_discovered_clients_() { ++promotions; }
  void handle_scanner_failure_() {
    ++failure_handlers; scanner_state_ = ScannerState::STOPPING; ++state_version_;
  }
  void scanner_event(ScannerState state) { scanner_state_ = state; ++state_version_; }
  void clients_changed() { ++state_version_; }
  void loop();
"""

CASES = r"""
void check(bool ok, const char *message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
  ESP32BLETracker idle;
  idle.loop();
  check(idle.count_reads == 0, "unchanged native fast path must skip idle processing");
  idle.set_scan_continuous(true);
  check(idle.state_version_ == 1, "restoration must wake the already-processed idle loop");
  idle.loop();
  check(idle.starts == 1, "idle scanner must restart through native loop");
  const auto version = idle.state_version_;
  idle.set_scan_continuous(true);
  check(idle.state_version_ == version, "unchanged policy must not repeatedly wake loop");

  for (int transition = 0; transition < 3; ++transition) {
    ESP32BLETracker busy;
    if (transition == 0) busy.clients.connecting = 1;
    if (transition == 1) busy.clients.disconnecting = 1;
    if (transition == 2) busy.clients.discovered = 1;
    busy.set_scan_continuous(true);
    busy.loop();
    check(busy.starts == 0, "native client-transition guard must prevent restart");
    check(busy.promotions == (transition == 2 ? 1u : 0u),
          "native discovered-client promotion must remain available");
    busy.clients = {};
    busy.clients_changed();
    busy.loop();
    check(busy.starts == 1, "client completion must allow deferred restart");
  }

  ESP32BLETracker stopping;
  stopping.scanner_state_ = ScannerState::STOPPING;
  stopping.set_scan_continuous(true);
  stopping.loop();
  check(stopping.starts == 0, "restoration must wait for asynchronous stop callback");
  stopping.scanner_event(ScannerState::IDLE);
  stopping.loop();
  check(stopping.starts == 1, "stop completion must allow restored policy to restart");

  ESP32BLETracker starting;
  starting.scanner_state_ = ScannerState::STARTING;
  starting.set_scan_continuous(true);
  starting.loop();
  check(starting.starts == 0, "restoration must not start an already-starting scanner");

  ESP32BLETracker disabled;
  disabled.parent.active = false;
  disabled.set_scan_continuous(true);
  disabled.loop();
  check(!disabled.parent.active && disabled.starts == 0 && disabled.count_reads == 0,
        "restoring owned policy must not enable BLE or start while externally disabled");
  disabled.parent.active = true;
  disabled.loop();
  check(disabled.starts == 1, "external BLE re-enable must retain restored continuous policy");

  ESP32BLETracker failed;
  failed.scanner_state_ = ScannerState::FAILED;
  failed.set_scan_continuous(true);
  failed.loop();
  check(failed.failure_handlers == 1 && failed.starts == 0,
        "scanner failure must stay under native recovery ownership");
  failed.scanner_event(ScannerState::IDLE);
  failed.loop();
  check(failed.starts == 1, "native recovery completion must retain restored policy");
  check(App.reboots == 0, "contract scenarios must not trigger watchdog reboot");
  std::puts("Native tracker scheduling contracts passed (8 scenarios).");
}
"""


def load_compat():
    path = ROOT / "esphome/components/norman_rf_monitor/ble_tracker_compat.py"
    spec = importlib.util.spec_from_file_location("ble_tracker_compat", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pinned_source(root, compat):
    if not (root / "const.py").is_file():
        raise RuntimeError(
            "Supply --esphome-source pointing to the ESPHome 2026.4.1 package directory containing const.py"
        )
    version = (root / "const.py").read_text(encoding="utf-8")
    if not re.search(r'^__version__ = "2026\.4\.1"$', version, re.M):
        raise RuntimeError("Runtime contract requires exactly ESPHome 2026.4.1 source")
    directory = root / "components/esp32_ble_tracker"
    header = (directory / "esp32_ble_tracker.h").read_text(encoding="utf-8")
    source = (directory / "esp32_ble_tracker.cpp").read_text(encoding="utf-8")
    if sha256(header.encode()).hexdigest() != compat.EXPECTED:
        raise RuntimeError(
            "Runtime contract requires the unmodified pinned tracker header"
        )
    if sha256(source.encode()).hexdigest() != CPP_DIGEST:
        raise RuntimeError(
            "Runtime contract refuses unexpected pinned tracker loop source"
        )
    begin = source.index("void ESP32BLETracker::loop() {")
    end = source.index("\nvoid ESP32BLETracker::start_scan()", begin)
    loop = source[begin:end]
    patched_header = header.replace(compat.ORIGINAL, compat.REPLACEMENT)
    if sha256(patched_header.encode()).hexdigest() != compat.PATCHED:
        raise RuntimeError("Runtime contract produced an unexpected patched header")
    # Extract the setter from the actual patched header used by the build hook.
    setter_begin = patched_header.index(
        "  void set_scan_continuous(bool scan_continuous)"
    )
    setter_end = patched_header.index("\n\n  /// Setup", setter_begin)
    return loop, patched_header[setter_begin:setter_end]


def compile_program(compiler, directory, name, source):
    cpp = directory / f"{name}.cpp"
    executable = directory / (f"{name}.exe" if os.name == "nt" else name)
    cpp.write_text(source)
    if Path(compiler).name.lower() in {"cl", "cl.exe"}:
        command = [
            compiler,
            "/nologo",
            "/std:c++17",
            "/EHsc",
            str(cpp),
            f"/Fe:{executable}",
        ]
    else:
        command = [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(cpp),
            "-o",
            str(executable),
        ]
    subprocess.run(command, cwd=directory, check=True, capture_output=True, text=True)
    return executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--esphome-source",
        type=Path,
        help="Pinned package directory containing const.py",
    )
    parser.add_argument(
        "--compiler", help="Existing C++17 compiler executable (g++, clang++ or cl)"
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        help="Existing parent directory for disposable compiler output",
    )
    args = parser.parse_args()
    try:
        root = args.esphome_source
        if root is None:
            spec = importlib.util.find_spec("esphome")
            if spec is None or not spec.origin:
                raise RuntimeError(
                    "Supply --esphome-source pointing to installed ESPHome 2026.4.1"
                )
            root = Path(spec.origin).parent
        compiler = args.compiler or next(
            (
                shutil.which(name)
                for name in ("g++", "clang++", "cl")
                if shutil.which(name)
            ),
            None,
        )
        if compiler is None or shutil.which(compiler) is None:
            raise RuntimeError(
                "An existing C++17 compiler is required; supply --compiler (no dependencies installed)"
            )
        compat = load_compat()
        loop, setter = pinned_source(root, compat)
        with tempfile.TemporaryDirectory(
            prefix="norman-ble-contract-", dir=args.work_dir
        ) as temporary:
            directory = Path(temporary)
            patched = PRELUDE + setter + "\n};\n" + loop + CASES
            executable = compile_program(compiler, directory, "patched", patched)
            subprocess.run([str(executable)], check=True)
            original = PRELUDE + compat.ORIGINAL + "\n};\n" + loop + CASES
            baseline = compile_program(compiler, directory, "original", original)
            result = subprocess.run([str(baseline)], capture_output=True, text=True)
            if result.returncode != 1 or "restoration must wake" not in result.stderr:
                raise RuntimeError(
                    "Regression control failed: unpatched setter did not reproduce the stranded idle scanner"
                )
            print(
                "Regression control confirmed: original setter strands an already-processed idle scanner."
            )
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(
            1, f"BLE runtime contract failed: {error}\n{getattr(error, 'stderr', '')}"
        )


if __name__ == "__main__":
    main()
