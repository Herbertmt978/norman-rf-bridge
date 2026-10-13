#pragma once

namespace esphome::norman_rf_monitor {

// Best-effort advertisement scan pause only: never disable BLE or delay RF.
// Public tracker APIs cannot reveal its prior continuous-scan policy. Restore
// the configured continuous policy only after this guard actually stopped a scan.
enum class ScanState { IDLE, STARTING, RUNNING, STOPPING, FAILED };
enum class ScanAction { NONE, STOP, KEEP_PAUSED, RESUME_CONTINUOUS };

class BleScanPause {
 public:
  ScanAction update(bool enabled, bool transmitting, bool ble_active, ScanState state) {
    if (!ble_active) {
      const bool restore = owned_;
      owned_ = false;
      // Preserve disabled BLE, but release our continuous-policy override so an
      // external future enable is not stranded with scanning permanently off.
      return restore ? ScanAction::RESUME_CONTINUOUS : ScanAction::NONE;
    }
    if (enabled && transmitting) {
      if (state == ScanState::RUNNING) {
        owned_ = true;
        return ScanAction::STOP;
      }
      // STARTING cannot be stopped yet. The caller retries on later loops.
      // Do not take ownership of an already idle/stopped scanner.
      return owned_ ? ScanAction::KEEP_PAUSED : ScanAction::NONE;
    }
    if (!owned_) return ScanAction::NONE;
    owned_ = false;
    // Restore policy immediately even if STOPPING: the tracker resumes once
    // its asynchronous stop completes. Do not wait or block the RF scheduler.
    return ScanAction::RESUME_CONTINUOUS;
  }
  bool owned() const { return owned_; }

 private:
  bool owned_{false};
};

}  // namespace esphome::norman_rf_monitor
