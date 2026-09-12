#include "norman_rf_monitor.h"

namespace esphome::norman_rf_monitor {

void NormanRfMonitor::learning_request(const std::string &request, JsonObject response) {
  bool ok = false;
  std::string error = "invalid_request";
  const uint32_t now = millis();
  if (request.size() <= 512) json::parse_json(request, [&](JsonObject input) -> bool {
    if (!input["operation"].is<const char *>() || !input["session"].is<const char *>()) return false;
    const std::string op = input["operation"].as<std::string>();
    const std::string token = input["session"].as<std::string>();
    if (op == "profiles") {
      auto profiles = response["profiles"].to<JsonArray>();
      for (int i = 0; i < 32; ++i) {
        if (panels_[i].ready()) {
          auto item = profiles.add<JsonObject>();
          item["kind"] = "panel"; item["slot"] = i; item["profile_id"] = panels_[i].profile_id();
          item["name"] = panels_[i].name(); item["room"] = panels_[i].room();
        }
        if (relay_endpoints_[i].ready()) {
          auto item = profiles.add<JsonObject>();
          item["kind"] = "relay"; item["slot"] = i; item["profile_id"] = relay_endpoints_[i].profile_id();
          item["name"] = relay_endpoints_[i].name(); item["room"] = "";
        }
      }
      ok = true; return true;
    }
    if (op == "remove" || op == "rename") {
      if (tx_active_ || learning_.active(now)) { error = "learning_busy"; return false; }
      if (!input["kind"].is<const char *>() || !input["slot"].is<int>() ||
          !input["profile_id"].is<const char *>()) return false;
      const int slot = input["slot"].as<int>();
      const std::string kind = input["kind"].as<std::string>();
      const std::string id = input["profile_id"].as<std::string>();
      if (slot < 0 || slot >= 32 || (kind != "panel" && kind != "relay")) return false;
      command_repeat_.clear();
      if (op == "remove") {
        if (!input["confirmed"].is<bool>() || !input["confirmed"].as<bool>()) return false;
        ok = kind == "panel" ? panels_[slot].remove(id) : relay_endpoints_[slot].remove(id);
      } else {
        if (!input["name"].is<const char *>() || (kind == "panel" && !input["room"].is<const char *>())) return false;
        ok = kind == "panel" ? panels_[slot].rename(id, input["name"].as<std::string>(), input["room"].as<std::string>())
                             : relay_endpoints_[slot].rename(id, input["name"].as<std::string>());
      }
      if (!ok) error = "storage_or_profile_error";
      return ok;
    }
    if (op == "begin") {
      if (!input["kind"].is<const char *>()) return false;
      const std::string kind = input["kind"].as<std::string>();
      if (kind != "panel" && kind != "relay") return false;
      if (!radio_ready_ || tx_active_ || relay_fault_) { error = "radio_unavailable"; return false; }
      if (!learning_.begin(token, kind == "relay", now)) { error = "learning_busy"; return false; }
      command_repeat_.clear();
      ok = true;
      return true;
    }
    if (!learning_.owns(token)) { error = "session_mismatch"; return false; }
    if (op == "cancel") { learning_.cancel(); ok = true; return true; }
    if (op == "status") { learning_.active(now); ok = true; return true; }
    if (op == "capture" && input["endpoint"].is<int>()) {
      ok = learning_.capture(token, input["endpoint"].as<int>(), now);
    } else if (op == "accept") {
      ok = learning_.accept(token, now);
    } else if (op == "commit") {
      // An uncertain response can be retried with the same session, not a new slot.
      if (learning_.saved_slot() >= 0) { ok = true; return true; }
      if (!learning_.ready(now) || tx_active_ || !radio_ready_ ||
          !input["name"].is<const char *>() || !input["confirmed"].is<bool>() ||
          !input["confirmed"].as<bool>()) return false;
      const std::string name = input["name"].as<std::string>();
      int slot = -1;
      if (learning_.relay()) {
        for (const auto &p : panels_) if (p.accepts(learning_.frame(0))) { error = "already_learned"; return false; }
        for (size_t i = 0; i < relay_endpoints_.size(); ++i) {
          if (relay_endpoints_[i].accepts(learning_.frame(0))) { error = "already_learned"; return false; }
          if (slot < 0 && !relay_endpoints_[i].ready()) slot = static_cast<int>(i);
        }
        if (slot >= 0) ok = relay_endpoints_[slot].configure(learning_.frame(0), name);
      } else {
        if (!input["room"].is<const char *>() || !input["close_position"].is<int>()) return false;
        for (size_t i = 0; i < panels_.size(); ++i) {
          if (panels_[i].same_target(learning_.frame(0))) { error = "already_learned"; return false; }
          if (slot < 0 && !panels_[i].ready()) slot = static_cast<int>(i);
        }
        for (const auto &ep : relay_endpoints_) for (int i = 0; i < (learning_.has_opposite() ? 3 : 2); ++i)
          if (ep.accepts(learning_.frame(i))) { error = "already_learned"; return false; }
        if (slot >= 0) ok = panels_[slot].configure_target(
          learning_.frame(0), learning_.frame(1), learning_.last_index(), 37,
          input["close_position"].as<int>(), name, input["room"].as<std::string>(),
          learning_.has_opposite() ? &learning_.frame(2) : nullptr);
      }
      if (ok) learning_.saved(slot);
      else error = slot < 0 ? "storage_full" : "storage_or_profile_error";
      return ok;
    }
    if (!ok && !learning_.error().empty()) error = learning_.error();
    return ok;
  });
  response["learning_version"] = 1;
  response["success"] = ok;
  response["error"] = ok ? "" : error;
  // No raw captures or rolling state cross the HA setup boundary.
  response["active"] = learning_.active(now);
  response["capturing"] = learning_.capturing();
  response["unique_presses"] = learning_.unique();
  response["packets"] = learning_.packets();
  response["saved_slot"] = learning_.saved_slot();
}

}  // namespace esphome::norman_rf_monitor
