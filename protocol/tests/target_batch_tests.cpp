#include "target_batch.h"
#include <iostream>

using namespace esphome::norman_rf_monitor;
int failures = 0;
void check(bool value, const char *message) {
  if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void crc(norman_rf::Frame &f) {
  const auto c = norman_rf::crc16(f.data(), 28);
  f[28] = static_cast<uint8_t>(c >> 8); f[29] = static_cast<uint8_t>(c);
}
int main() {
  for (size_t count=1;count<=8;++count) {
    std::array<int,8> first{}, copies{};
    for (size_t round=0;round<100;++round) {
      std::array<bool,8> seen{};
      ++first[batch_target_index(round,0,count)];
      for (size_t offset=0;offset<count;++offset) {
        const auto index=batch_target_index(round,offset,count);
        check(index<count && !seen[index], "each target appears exactly once per round");
        seen[index]=true; ++copies[index];
      }
    }
    for (size_t i=0;i<count;++i) {
      check(copies[i]==100, "rotating order preserves100copies per target");
      check(first[i]>=100/count && first[i]<=100/count+1, "first positions balanced");
    }
  }
  std::array<LearnedPanel, 32> panels{};
  std::vector<int32_t> slots, positions;
  std::vector<std::string> ids;
  BatchFrames frames{};
  for (int32_t i = 0; i < 9; ++i) {
    panels[i].setup(static_cast<uint8_t>(i));
    norman_rf::Frame o{}; o[0] = 24; o[21] = static_cast<uint8_t>(i); crc(o);
    auto c = o; c[4] = 0xf8; crc(c);
    check(panels[i].configure_target(o,c,255,37,100,"Panel","Room"), "commission synthetic target");
    slots.push_back(i); positions.push_back(100); ids.push_back(panels[i].profile_id());
  }
  check(!prepare_target_batch(panels,slots,positions,ids,frames), "nine-target batch rejected before reservation");
  slots.pop_back(); positions.pop_back(); ids.pop_back();
  check(!prepare_target_batch(panels,{}, {}, {},frames), "empty batch rejected");
  auto badslots=slots; badslots[7]=0;
  check(!prepare_target_batch(panels,badslots,positions,ids,frames), "duplicate rejected");
  badslots[7]=32;
  check(!prepare_target_batch(panels,badslots,positions,ids,frames), "out-of-range rejected");
  badslots[7]=-1;
  check(!prepare_target_batch(panels,badslots,positions,ids,frames), "negative rejected");
  auto badids=ids; badids[7]="ffffffffffffffff";
  check(!prepare_target_batch(panels,slots,positions,badids,frames), "late wrong identity rejected");
  auto badpos=positions; badpos[7]=0;
  check(!prepare_target_batch(panels,slots,badpos,ids,frames), "unlearned opposite endpoint rejected");
  badpos.pop_back();
  check(!prepare_target_batch(panels,slots,badpos,ids,frames), "mismatched vectors rejected");
  for (int i=0;i<9;++i) check(panels[i].last_index()==255, "preflight never consumes any index");
  check(prepare_target_batch(panels,slots,positions,ids,frames), "all eight reservations durable");
  for (int i=0;i<8;++i) {
    LearnedPanel restored; restored.setup(static_cast<uint8_t>(i));
    check(restored.last_index()==0 && frames[i][24]==0 && frames[i][21]==i, "wrap and exact target persist before radio");
  }
  check(panels[8].last_index()==255, "unselected target unchanged");
  esphome::syncs_before_failure=2;
  check(!prepare_target_batch(panels,slots,positions,ids,frames), "second target storage fault rejects whole batch");
  check(panels[0].last_index()==1 && !panels[1].ready() && panels[2].last_index()==0, "no rewind of reserved prefix or reservation of suffix");
  esphome::syncs_before_failure=-1;
  LearnedPanel restored; restored.setup(0);
  check(restored.last_index()==1, "failed batch never loses earlier durable reservation");
  return failures ? 1 : 0;
}
