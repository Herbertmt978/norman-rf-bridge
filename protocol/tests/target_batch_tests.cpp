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
  for (size_t copies : {size_t{100}, size_t{20}}) {
    for (size_t count=1;count<=8;++count) {
      const size_t total=sequential_total_ticks(copies,count);
      check(total==copies*count+kTargetGapTicks*(count-1), "schedule length is bursts plus gaps");
      std::array<size_t,8> sent{}, first{}, last{};
      size_t previous=0, gap_run=0, sends=0;
      bool seen_any=false;
      for (size_t tick=0;tick<total;++tick) {
        size_t target=99;
        if (!sequential_target(tick,copies,target)) {
          ++gap_run;
          continue;
        }
        check(target<count, "scheduled target within batch");
        check(target>=previous, "targets never interleave or return");
        if (seen_any && target!=previous)
          check(gap_run==kTargetGapTicks, "full idle gap separates consecutive targets");
        check(!seen_any || target!=previous || gap_run==0, "a target's own burst has no gaps");
        if (sent[target]==0) first[target]=tick;
        last[target]=tick;
        ++sent[target]; ++sends;
        previous=target; gap_run=0; seen_any=true;
      }
      check(sends==copies*count, "one copy per non-gap tick");
      for (size_t i=0;i<count;++i) {
        check(sent[i]==copies, "every target receives its complete burst");
        check(last[i]-first[i]+1==copies, "each burst is contiguous");
      }
      check(sequential_target(total-1,copies,previous) && previous==count-1, "schedule ends on the last target");
    }
  }
  check(sequential_total_ticks(100,1)==100, "single target keeps the original 100-tick burst");
  check(sequential_total_ticks(100,4)*55==27940, "four-section room lasts about 27.9 s");
  check(sequential_total_ticks(100,8)<=65535, "largest room fits the 16-bit tick counter");
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
