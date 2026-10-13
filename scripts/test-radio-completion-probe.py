"""Generate a bounded native test from the actual RF completion callback.

Use --output DIRECTORY, then CMake configure/build/CTest that scratch directory.
The harness models SPI completion outcomes; it never contacts hardware.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def generate(output):
    source = (ROOT / 'esphome/components/norman_rf_monitor/norman_rf_monitor.cpp').read_text()
    begin = source.index('void NormanRfMonitor::send_test_copy_()')
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    method = source[begin:end]
    prefix = r'''
#include <array>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
uint32_t clock_us=1000;
uint32_t micros() { return clock_us; }
uint32_t millis() { return clock_us/1000; }
void delay_microseconds_safe(uint32_t delay) { clock_us+=delay; }
std::string error_line;
std::vector<char> events;
void error_log(const char*,const char* format,...) {
  char buffer[512]; va_list args; va_start(args,format);
  std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
  error_line=buffer; events.push_back('E');
}
#define ESP_LOGE(...) error_log(__VA_ARGS__)
#define ESP_LOGI(...)
const char *TAG="fixture";
uint32_t ce_low_us=0;
struct Pin { void digital_write(bool high) { if(!high) ce_low_us=clock_us; } };
size_t batch_block_target(size_t round,size_t rounds,size_t count) { return round*count/rounds; }
class NormanRfMonitor {
public:
  enum { kRegisterConfig=0,kRegisterEnableAutoAck=1,kRegisterSetupRetransmit=4,
         kRegisterRfChannel=5,kRegisterRfSetup=6,kRegisterStatus=7,kRegisterFifoStatus=23,
         kStatusClearAllInterrupts=112,kCommandNoOperation=255 };
  bool tx_active_=true,probing=false;
  uint32_t tx_started_ms_=1,transmitted_count_=0;
  uint16_t tx_rounds_=100,tx_remaining_=100;
  bool late_visible=false;
  size_t tx_target_count_=2,payload_attempts=0,failures=0,probe_reads=0;
  uint8_t first_status=0x0e,later_status=0x0e;
  std::array<std::array<uint8_t,32>,8> tx_payloads_{};
  std::map<int,uint8_t> registers{{0,2},{1,0},{4,0},{5,15},{6,0},{23,0x10}};
  Pin pin; Pin* ce_pin_=&pin;
  std::string outcome;
  void send_test_copy_();
  void write_register_(int,int) { events.push_back('W'); }
  void enable() {} void disable() {}
  uint8_t transfer_byte(uint8_t value) { if(value==0xa0) ++payload_attempts; return 0; }
  // late_visible: TX_DS only becomes readable once the 2ms deadline has passed.
  uint8_t issue_command_(int) { return late_visible && clock_us-ce_low_us>=2000 ? later_status : first_status; }
  void read_register_(int address,uint8_t* value) {
    probing=true; ++probe_reads; clock_us+=5; events.push_back('R');
    *value=address==kRegisterStatus?later_status:registers.at(address);
  }
  void finish_test_(const char* reason) {
    events.push_back('C'); tx_active_=false; outcome=reason; ++failures;
  }
};
'''
    suffix = r'''
void need(bool ok,const char* reason) {
  if(!ok) { std::cerr<<reason<<"\n"; std::exit(1); }
}
void reset() { clock_us=1000; error_line.clear(); events.clear(); }
void failure_case(uint8_t original,uint8_t late,bool reset_registers) {
  reset(); NormanRfMonitor radio;
  radio.first_status=original; radio.later_status=late;
  if(reset_registers) radio.registers={{0,8},{1,63},{4,3},{5,2},{6,14},{23,17}};
  radio.send_test_copy_();
  need(radio.failures==1 && radio.outcome=="radio_tx_failed", "original failure decision changed");
  need(radio.payload_attempts==1 && radio.transmitted_count_==0 && radio.tx_remaining_==100,
       "failure retried, counted a success or advanced rounds");
  need(radio.probe_reads==7, "missing or unbounded register snapshot");
  need(events.back()=='C' && events[events.size()-2]=='E', "cleanup preceded snapshot/error log");
  need(error_line.find("target_ordinal=1 targets=2 rounds_remaining=100")!=std::string::npos,
       "failure scope missing");
  if(original==0x0e) need(error_line.find("wait_us=2000")!=std::string::npos, "deadline changed");
  if(late==0x2e) need(error_line.find("first_status=0e later_status=2e")!=std::string::npos,
                     "late completion evidence lost or converted to success");
  if(original==0x1e) need(error_line.find("wait_us=0")!=std::string::npos, "MAX_RT failure delayed");
  if(reset_registers) need(error_line.find("config=08 en_aa=3f retr=03 channel=2 setup=0e")!=std::string::npos,
                          "register reset evidence overwritten before capture");
  std::cout<<error_line<<"\n";
}
void stale_sample_case() {
  reset(); NormanRfMonitor radio;
  radio.first_status=0x0e; radio.later_status=0x2e; radio.late_visible=true;
  radio.send_test_copy_();
  need(radio.failures==0 && radio.probe_reads==0 && error_line.empty(),
       "completion visible on the fresh read was falsely aborted");
  need(radio.payload_attempts==2 && radio.transmitted_count_==2 && radio.tx_remaining_==99,
       "fresh status read resent a payload or skipped a target");
}
int main() {
  reset(); NormanRfMonitor healthy; healthy.first_status=healthy.later_status=0x2e;
  healthy.send_test_copy_();
  need(healthy.failures==0 && healthy.probe_reads==0 && error_line.empty(),
       "healthy path performs diagnostic work");
  need(healthy.payload_attempts==2 && healthy.transmitted_count_==2 && healthy.tx_remaining_==99,
       "healthy batch behaviour changed");
  failure_case(0x0e,0x0e,false);  // missing completion remains missing
  failure_case(0x0e,0x2e,false);  // late completion retains original failure
  failure_case(0x1e,0x1e,false);  // unexpected MAX_RT remains immediate failure
  failure_case(0x0e,0x0e,true);   // unexpected register configuration captured
  stale_sample_case();            // stale pre-deadline sample no longer aborts
  std::cout<<"PASS: actual completion callback snapshot before cleanup; healthy path unchanged; no retry or false success\n";
}
'''
    output.mkdir(parents=True, exist_ok=True)
    (output / 'completion_probe.cpp').write_text(prefix + method + suffix)
    (output / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(norman_completion_probe LANGUAGES CXX)
add_executable(completion_probe completion_probe.cpp)
target_compile_features(completion_probe PRIVATE cxx_std_17)
enable_testing()
add_test(NAME completion_probe COMMAND completion_probe)
''')
    print('Generated actual-method completion probe test; no build or hardware action.')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    generate(parser.parse_args().output)
