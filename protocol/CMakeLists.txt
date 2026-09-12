cmake_minimum_required(VERSION 3.20)
project(norman_rf_protocol LANGUAGES CXX)

add_library(norman_rf_protocol
  src/protocol.cpp
)
target_include_directories(norman_rf_protocol PUBLIC include)
target_compile_features(norman_rf_protocol PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(norman_rf_protocol PRIVATE /W4 /WX)
else()
  target_compile_options(norman_rf_protocol PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(norman_rf_protocol_tests
  tests/protocol_tests.cpp
)
target_link_libraries(norman_rf_protocol_tests PRIVATE norman_rf_protocol)

enable_testing()
add_test(NAME norman_rf_protocol_tests COMMAND norman_rf_protocol_tests)

add_executable(norman_rf_learned_panel_tests
  tests/learned_panel_tests.cpp
  ../esphome/components/norman_rf_monitor/learned_panel.cpp
)
target_include_directories(norman_rf_learned_panel_tests PRIVATE
  tests/fakes ../esphome/components/norman_rf_monitor)
target_link_libraries(norman_rf_learned_panel_tests PRIVATE norman_rf_protocol)
add_test(NAME norman_rf_learned_panel_tests COMMAND norman_rf_learned_panel_tests)

add_executable(norman_rf_target_batch_tests
  tests/target_batch_tests.cpp
  ../esphome/components/norman_rf_monitor/learned_panel.cpp
)
target_include_directories(norman_rf_target_batch_tests PRIVATE
  tests/fakes ../esphome/components/norman_rf_monitor)
target_link_libraries(norman_rf_target_batch_tests PRIVATE norman_rf_protocol)
add_test(NAME norman_rf_target_batch_tests COMMAND norman_rf_target_batch_tests)

add_executable(norman_rf_command_repeat_tests
  tests/command_repeat_tests.cpp
  ../esphome/components/norman_rf_monitor/learned_panel.cpp
)
target_include_directories(norman_rf_command_repeat_tests PRIVATE
  tests/fakes ../esphome/components/norman_rf_monitor)
target_link_libraries(norman_rf_command_repeat_tests PRIVATE norman_rf_protocol)
add_test(NAME norman_rf_command_repeat_tests COMMAND norman_rf_command_repeat_tests)

add_executable(norman_rf_relay_endpoint_tests
  tests/relay_endpoint_tests.cpp
  ../esphome/components/norman_rf_monitor/learned_panel.cpp
  ../esphome/components/norman_rf_monitor/learned_relay_endpoint.cpp
)
target_include_directories(norman_rf_relay_endpoint_tests PRIVATE
  tests/fakes ../esphome/components/norman_rf_monitor)
target_link_libraries(norman_rf_relay_endpoint_tests PRIVATE norman_rf_protocol)
add_test(NAME norman_rf_relay_endpoint_tests COMMAND norman_rf_relay_endpoint_tests)
