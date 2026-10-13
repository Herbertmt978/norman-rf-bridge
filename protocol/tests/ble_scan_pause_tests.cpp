#include "ble_scan_pause.h"
#include <cstdlib>
#include <iostream>
using namespace esphome::norman_rf_monitor;
void check(bool ok, const char *message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
  BleScanPause p;
  check(p.update(false, true, true, ScanState::RUNNING) == ScanAction::NONE,
        "opt-in disabled leaves scanning alone");
  check(p.update(true, true, true, ScanState::IDLE) == ScanAction::NONE && !p.owned(),
        "idle scanner does not acquire ownership or force it on later");
  check(p.update(true, true, true, ScanState::STARTING) == ScanAction::NONE,
        "STARTING does not issue an invalid stop");
  check(p.update(true, true, true, ScanState::RUNNING) == ScanAction::STOP && p.owned(),
        "RUNNING eventually requests pause without delaying RF");
  check(p.update(true, true, true, ScanState::STOPPING) == ScanAction::KEEP_PAUSED,
        "stop acknowledgement is asynchronous");
  check(p.update(true, true, true, ScanState::IDLE) == ScanAction::KEEP_PAUSED,
        "pause remains owned across HA scanner-mode changes");
  check(p.update(true, true, true, ScanState::RUNNING) == ScanAction::STOP,
        "external restart is stopped again best effort");
  check(p.update(true, false, true, ScanState::STOPPING) == ScanAction::RESUME_CONTINUOUS && !p.owned(),
        "RF end restores policy even before stop callback, without an unbounded wait");
  check(p.update(true, false, true, ScanState::IDLE) == ScanAction::NONE,
        "restoration is requested exactly once");
  check(p.update(true, true, true, ScanState::RUNNING) == ScanAction::STOP,
        "each repeat and relay shares a new independent pause lifecycle");
  check(p.update(false, true, true, ScanState::IDLE) == ScanAction::RESUME_CONTINUOUS,
        "turning switch off restores scanning during RF");
  check(p.update(true, true, true, ScanState::RUNNING) == ScanAction::STOP,
        "later RF can acquire another pause");
  check(p.update(true, true, false, ScanState::STOPPING) == ScanAction::RESUME_CONTINUOUS && !p.owned(),
        "external BLE disable releases owned policy without enabling BLE or starting scan");
  check(p.update(true, true, false, ScanState::IDLE) == ScanAction::NONE,
        "already disabled BLE has no unowned policy changes");
  check(p.update(true, false, true, ScanState::IDLE) == ScanAction::NONE,
        "BLE re-enable is not owned by RF guard");
  check(p.update(true, true, true, ScanState::STARTING) == ScanAction::NONE &&
        p.update(true, false, true, ScanState::STARTING) == ScanAction::NONE,
        "RF ending before a stop request does not alter scanner policy");
  p.update(true, true, true, ScanState::RUNNING);
  check(p.update(true, false, true, ScanState::FAILED) == ScanAction::RESUME_CONTINUOUS,
        "RF failure releases pause without resetting BLE stack");
  p.update(true, true, true, ScanState::RUNNING);
  check(p.update(false, true, true, ScanState::STARTING) == ScanAction::RESUME_CONTINUOUS,
        "shutdown/abort releases ownership in transitional scanner states");
}
