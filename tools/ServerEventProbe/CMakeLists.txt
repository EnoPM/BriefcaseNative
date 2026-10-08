cmake_minimum_required(VERSION 3.20)
project(BriefcaseServerEventProbe LANGUAGES CXX)

add_executable(Briefcase.ServerEventProbe main.cpp)
target_compile_features(Briefcase.ServerEventProbe PRIVATE cxx_std_17)

enable_testing()
add_test(NAME ServerEventProbe.Replay
  COMMAND Briefcase.ServerEventProbe --check-fixture
    "${CMAKE_CURRENT_SOURCE_DIR}/test-fixture.log")
