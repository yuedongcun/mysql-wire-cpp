cmake_minimum_required(VERSION 3.16)

project(mysql-wire-cpp VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(MYSQL_WIRE_BUILD_EXAMPLES "Build the standalone demo server" ON)
option(MYSQL_WIRE_BUILD_TESTS "Build protocol tests" ON)

add_library(
  mysql_wire
  STATIC
  src/packet.cpp
  src/result_encoder.cpp
  src/server.cpp
  src/session.cpp
  src/sql_executor.cpp)
add_library(mysql-wire-cpp::mysql_wire ALIAS mysql_wire)

target_compile_features(mysql_wire PUBLIC cxx_std_17)
target_include_directories(
  mysql_wire
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mysql_wire PUBLIC Threads::Threads)

if(MYSQL_WIRE_BUILD_EXAMPLES)
  add_executable(mysql-wire-demo examples/demo_server.cpp)
  target_link_libraries(mysql-wire-demo PRIVATE mysql_wire)
endif()

if(MYSQL_WIRE_BUILD_TESTS)
  enable_testing()

  add_executable(mysql_wire_packet_test tests/packet_test.cpp)
  target_include_directories(mysql_wire_packet_test PRIVATE src)
  target_link_libraries(mysql_wire_packet_test PRIVATE mysql_wire)
  add_test(NAME mysql_wire_packet_test COMMAND mysql_wire_packet_test)

  add_executable(mysql_wire_session_test tests/session_test.cpp)
  target_include_directories(mysql_wire_session_test PRIVATE src)
  target_link_libraries(mysql_wire_session_test PRIVATE mysql_wire)
  add_test(NAME mysql_wire_session_test COMMAND mysql_wire_session_test)
endif()

install(
  TARGETS mysql_wire
  EXPORT mysql-wire-cpp-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
  FILES include/mysql_wire/mysql_wire.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mysql_wire)
install(
  EXPORT mysql-wire-cpp-targets
  FILE mysql-wire-cpp-targets.cmake
  NAMESPACE mysql-wire-cpp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mysql-wire-cpp)
