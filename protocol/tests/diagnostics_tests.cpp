#include "diagnostics.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using esphome::norman_rf_monitor::ActivityAge;
void check(bool value, const char *message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
  ActivityAge a;
  check(std::isnan(a.seconds(0)), "no activity is unknown, not recent");
  a.mark(0);
  check(a.seconds(0) == 0 && a.seconds(1250) == 1.25f, "boot-zero timestamp is valid");
  a.mark(UINT64_C(4294967290));
  check(a.seconds(UINT64_C(4294968290)) == 1, "64-bit uptime passes millis wrap");
  check(std::isnan(a.seconds(10)), "old evidence cannot survive clock reset");
  a.mark(UINT64_C(6000000000));
  check(a.seconds(UINT64_C(6000002000)) == 2, "later activity replaces old evidence");
  ActivityAge reboot;
  check(std::isnan(reboot.seconds(1000)), "new instance does not invent persisted activity");
}
